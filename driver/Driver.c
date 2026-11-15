/*
 * VirtualPtp - virtual Windows Precision Touchpad, UMDF2 HID minidriver.
 *
 * Stack: HIDClass -> mshidumdf.sys (inbox) -> WUDFRd.sys -> this DLL (in WUDFHost.exe)
 *
 * The device exposes four top-level collections (see HidDescriptor.h):
 *   1. Precision Touchpad  - what Windows' gesture engine consumes
 *   2. Configuration       - input mode / function switch feature reports
 *   3. Mouse               - required by the PTP spec, never reported
 *   4. Vendor "inject"     - the MiddlePtp app writes finger frames here
 *
 * Every frame written to (4) is re-emitted unchanged as a touchpad input report on (1).
 * All gesture logic lives in the app; the driver is a dumb, low-risk relay.
 */

#include <windows.h>
#include <wdf.h>
#include <hidport.h>
#include <string.h>

#include "VirtualPtpShared.h"
#include "HidDescriptor.h"

#define FIFO_LEN 16

typedef struct _DEVICE_CONTEXT {
    WDFQUEUE       ReadQueue;       // manual queue holding pending IOCTL_HID_READ_REPORTs
    WDFSPINLOCK    Lock;            // protects Fifo* and ReadQueue retrieval
    HID_DESCRIPTOR HidDescriptor;
    UCHAR          InputMode;
    UCHAR          FunctionSwitch;
    ULONG          FifoHead;
    ULONG          FifoCount;
    UCHAR          Fifo[FIFO_LEN][VPTP_REPORT_SIZE];
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

DRIVER_INITIALIZE                   DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD           EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL  EvtIoDeviceControl;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static NTSTATUS
CopyToRequest(_In_ WDFREQUEST Request, _In_reads_bytes_(Length) const VOID* Source, _In_ size_t Length)
{
    WDFMEMORY memory;
    size_t    outLength;
    NTSTATUS  status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    WdfMemoryGetBuffer(memory, &outLength);
    if (outLength < Length) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    status = WdfMemoryCopyFromBuffer(memory, 0, (PVOID)Source, Length);
    if (NT_SUCCESS(status)) {
        WdfRequestSetInformation(Request, Length);
    }
    return status;
}

// UMDF HID "write"-style requests (WRITE_REPORT, SET_FEATURE, SET_OUTPUT_REPORT):
// the report (starting with its report ID byte) is in the input buffer.
static NTSTATUS
GetWriteBuffer(_In_ WDFREQUEST Request, _Out_ PUCHAR* Buffer, _Out_ size_t* Length)
{
    WDFMEMORY memory;
    NTSTATUS  status = WdfRequestRetrieveInputMemory(Request, &memory);
    *Buffer = NULL;
    *Length = 0;
    if (!NT_SUCCESS(status)) {
        return status;
    }
    *Buffer = (PUCHAR)WdfMemoryGetBuffer(memory, Length);
    return (*Length >= 1) ? STATUS_SUCCESS : STATUS_INVALID_BUFFER_SIZE;
}

// UMDF HID "read"-style requests (GET_FEATURE, GET_INPUT_REPORT):
// report ID is the first byte of the input buffer, the report goes to the output buffer.
static NTSTATUS
GetReadBuffer(_In_ WDFREQUEST Request, _Out_ UCHAR* ReportId, _Out_ PUCHAR* Buffer, _Out_ size_t* Length)
{
    WDFMEMORY memory;
    size_t    inLength;
    PUCHAR    in;
    NTSTATUS  status;

    *ReportId = 0;
    *Buffer   = NULL;
    *Length   = 0;

    status = WdfRequestRetrieveInputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    in = (PUCHAR)WdfMemoryGetBuffer(memory, &inLength);
    if (inLength < 1) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    *ReportId = in[0];

    status = WdfRequestRetrieveOutputMemory(Request, &memory);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    *Buffer = (PUCHAR)WdfMemoryGetBuffer(memory, Length);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Input report delivery                                                      */
/* ------------------------------------------------------------------------- */

static VOID
DeliverReport(_In_ PDEVICE_CONTEXT Context, _In_reads_bytes_(VPTP_REPORT_SIZE) const UCHAR* Report)
{
    WDFREQUEST request = NULL;
    NTSTATUS   status;

    WdfSpinLockAcquire(Context->Lock);
    status = WdfIoQueueRetrieveNextRequest(Context->ReadQueue, &request);
    if (!NT_SUCCESS(status)) {
        // No read pending: buffer it, dropping the oldest frame if full.
        ULONG index;
        request = NULL;
        if (Context->FifoCount == FIFO_LEN) {
            Context->FifoHead = (Context->FifoHead + 1) % FIFO_LEN;
            Context->FifoCount--;
        }
        index = (Context->FifoHead + Context->FifoCount) % FIFO_LEN;
        memcpy(Context->Fifo[index], Report, VPTP_REPORT_SIZE);
        Context->FifoCount++;
    }
    WdfSpinLockRelease(Context->Lock);

    if (request != NULL) {
        status = CopyToRequest(request, Report, VPTP_REPORT_SIZE);
        WdfRequestComplete(request, status);
    }
}

static VOID
HandleReadReport(_In_ PDEVICE_CONTEXT Context, _In_ WDFREQUEST Request)
{
    UCHAR    report[VPTP_REPORT_SIZE];
    BOOLEAN  haveReport = FALSE;
    NTSTATUS status = STATUS_SUCCESS;

    WdfSpinLockAcquire(Context->Lock);
    if (Context->FifoCount > 0) {
        memcpy(report, Context->Fifo[Context->FifoHead], VPTP_REPORT_SIZE);
        Context->FifoHead = (Context->FifoHead + 1) % FIFO_LEN;
        Context->FifoCount--;
        haveReport = TRUE;
    } else {
        status = WdfRequestForwardToIoQueue(Request, Context->ReadQueue);
    }
    WdfSpinLockRelease(Context->Lock);

    if (haveReport) {
        status = CopyToRequest(Request, report, VPTP_REPORT_SIZE);
        WdfRequestComplete(Request, status);
    } else if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
    // else: parked in ReadQueue, completed later by DeliverReport
}

/* ------------------------------------------------------------------------- */
/* IOCTL handlers                                                             */
/* ------------------------------------------------------------------------- */

static NTSTATUS
HandleWriteReport(_In_ PDEVICE_CONTEXT Context, _In_ WDFREQUEST Request)
{
    PUCHAR   buffer;
    size_t   length;
    UCHAR    report[VPTP_REPORT_SIZE];
    NTSTATUS status = GetWriteBuffer(Request, &buffer, &length);

    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (buffer[0] != REPORTID_INJECT || length < VPTP_REPORT_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }

    report[0] = REPORTID_TOUCHPAD;
    memcpy(&report[1], &buffer[1], sizeof(PTP_FRAME));
    DeliverReport(Context, report);

    WdfRequestSetInformation(Request, length);
    return STATUS_SUCCESS;
}

static NTSTATUS
HandleGetFeature(_In_ PDEVICE_CONTEXT Context, _In_ WDFREQUEST Request)
{
    UCHAR    reportId;
    PUCHAR   out;
    size_t   outLength;
    size_t   needed;
    NTSTATUS status = GetReadBuffer(Request, &reportId, &out, &outLength);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    switch (reportId) {
    case REPORTID_MAX_COUNT:      needed = 2; break;
    case REPORTID_PTPHQA:         needed = 1 + sizeof(g_PtpHqaBlob); break;
    case REPORTID_INPUT_MODE:     needed = 2; break;
    case REPORTID_FUNCTION_SWITCH:needed = 2; break;
    default:                      return STATUS_INVALID_PARAMETER;
    }
    if (outLength < needed) {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    out[0] = reportId;
    switch (reportId) {
    case REPORTID_MAX_COUNT:
        // Contact Count Maximum is the low nibble, Pad Type the high nibble.
        out[1] = (UCHAR)((PTP_PAD_TYPE << 4) | PTP_MAX_CONTACTS);
        break;
    case REPORTID_PTPHQA:
        memcpy(&out[1], g_PtpHqaBlob, sizeof(g_PtpHqaBlob));
        break;
    case REPORTID_INPUT_MODE:
        out[1] = Context->InputMode;
        break;
    case REPORTID_FUNCTION_SWITCH:
        out[1] = Context->FunctionSwitch;
        break;
    }

    WdfRequestSetInformation(Request, needed);
    return STATUS_SUCCESS;
}

static NTSTATUS
HandleSetFeature(_In_ PDEVICE_CONTEXT Context, _In_ WDFREQUEST Request)
{
    PUCHAR   buffer;
    size_t   length;
    NTSTATUS status = GetWriteBuffer(Request, &buffer, &length);

    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (length >= 2) {
        switch (buffer[0]) {
        case REPORTID_INPUT_MODE:      Context->InputMode = buffer[1];      break; // 3 = PTP mode
        case REPORTID_FUNCTION_SWITCH: Context->FunctionSwitch = buffer[1]; break;
        default: break;   // accept and ignore anything else (latency mode etc.)
        }
    }
    WdfRequestSetInformation(Request, length);
    return STATUS_SUCCESS;
}

static NTSTATUS
HandleGetString(_In_ WDFREQUEST Request)
{
    WDFMEMORY memory;
    size_t    inLength;
    PVOID     in;
    PCWSTR    text;
    ULONG     stringId;
    NTSTATUS  status = WdfRequestRetrieveInputMemory(Request, &memory);

    if (!NT_SUCCESS(status)) {
        return status;
    }
    in = WdfMemoryGetBuffer(memory, &inLength);
    if (inLength < sizeof(ULONG)) {
        return STATUS_INVALID_BUFFER_SIZE;
    }
    stringId = (*(const ULONG*)in) & 0xFFFF;   // high word is the language id

    switch (stringId) {
    case HID_STRING_ID_IMANUFACTURER: text = L"VirtualPtp";                 break;
    case HID_STRING_ID_IPRODUCT:      text = L"Virtual Precision Touchpad"; break;
    case HID_STRING_ID_ISERIALNUMBER: text = L"VPTP0001";                   break;
    default:                          return STATUS_INVALID_PARAMETER;
    }
    return CopyToRequest(Request, text, (wcslen(text) + 1) * sizeof(WCHAR));
}

VOID
EvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode)
{
    PDEVICE_CONTEXT context = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS        status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
        status = CopyToRequest(Request, &context->HidDescriptor, context->HidDescriptor.bLength);
        break;

    case IOCTL_HID_GET_DEVICE_ATTRIBUTES: {
        HID_DEVICE_ATTRIBUTES attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.Size          = sizeof(attributes);
        attributes.VendorID      = VPTP_VENDOR_ID;
        attributes.ProductID     = VPTP_PRODUCT_ID;
        attributes.VersionNumber = VPTP_VERSION;
        status = CopyToRequest(Request, &attributes, sizeof(attributes));
        break;
    }

    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
        status = CopyToRequest(Request, g_ReportDescriptor, sizeof(g_ReportDescriptor));
        break;

    case IOCTL_HID_READ_REPORT:
        HandleReadReport(context, Request);
        return;   // completed (now or later) by the handler

    case IOCTL_HID_WRITE_REPORT:
    case IOCTL_UMDF_HID_SET_OUTPUT_REPORT:
        status = HandleWriteReport(context, Request);
        break;

    case IOCTL_UMDF_HID_GET_FEATURE:
        status = HandleGetFeature(context, Request);
        break;

    case IOCTL_UMDF_HID_SET_FEATURE:
        status = HandleSetFeature(context, Request);
        break;

    case IOCTL_HID_GET_STRING:
        status = HandleGetString(Request);
        break;

    default:
        // GET_INPUT_REPORT, ACTIVATE/DEACTIVATE, idle notification, indexed strings, ...
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    WdfRequestComplete(Request, status);
}

/* ------------------------------------------------------------------------- */
/* Driver / device setup                                                      */
/* ------------------------------------------------------------------------- */

NTSTATUS
EvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG   queueConfig;
    WDFDEVICE             device;
    PDEVICE_CONTEXT       context;
    NTSTATUS              status;

    UNREFERENCED_PARAMETER(Driver);

    // mshidumdf.sys is the function driver / power policy owner; we sit as a filter.
    WdfFdoInitSetFilter(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    context = GetDeviceContext(device);
    context->InputMode      = 0;
    context->FunctionSwitch = 0x03;   // surface + button reporting on
    context->FifoHead       = 0;
    context->FifoCount      = 0;

    context->HidDescriptor.bLength         = sizeof(HID_DESCRIPTOR);
    context->HidDescriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
    context->HidDescriptor.bcdHID          = 0x0101;
    context->HidDescriptor.bCountry        = 0;
    context->HidDescriptor.bNumDescriptors = 1;
    context->HidDescriptor.DescriptorList[0].bReportType   = HID_REPORT_DESCRIPTOR_TYPE;
    context->HidDescriptor.DescriptorList[0].wReportLength = (USHORT)sizeof(g_ReportDescriptor);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &context->Lock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
    queueConfig.PowerManaged       = WdfFalse;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &context->ReadQueue);
    return status;
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}
