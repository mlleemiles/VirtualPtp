// VirtualPtpShared.h - definitions shared by the VirtualPtp driver and the MiddlePtp app.
#pragma once

#include <windows.h>

// Identity of the virtual device (arbitrary values; only used to find it again).
#define VPTP_VENDOR_ID          0xFEED
#define VPTP_PRODUCT_ID         0x7054
#define VPTP_VERSION            0x0100

// Vendor-defined collection the app writes finger frames to.
#define VPTP_VENDOR_USAGE_PAGE  0xFF00
#define VPTP_VENDOR_USAGE       0x0001

// Report IDs
#define REPORTID_TOUCHPAD        0x01   // PTP input report
#define REPORTID_MAX_COUNT       0x02   // PTP feature: contact count max + pad type
#define REPORTID_PTPHQA          0x03   // PTP feature: 256-byte certification blob
#define REPORTID_INPUT_MODE      0x04   // Config feature: input mode (host sets 3 = PTP)
#define REPORTID_FUNCTION_SWITCH 0x05   // Config feature: surface / button switch
#define REPORTID_MOUSE           0x06   // Mouse TLC required by the spec (never sent)
#define REPORTID_INJECT          0x07   // Vendor output report: app -> driver

// Touchpad geometry. Physical size is in 1/100 inch (unit exponent -2, inch),
// so this is a 6.30" x 3.94" (16 cm x 10 cm) pad with 1000 logical units per inch.
#define PTP_LOGICAL_MAX_X       6300
#define PTP_LOGICAL_MAX_Y       3940
#define PTP_PHYSICAL_MAX_X      630
#define PTP_PHYSICAL_MAX_Y      394

#define PTP_MAX_CONTACTS        5
#define PTP_PAD_TYPE            0       // 0 = depressible (click-pad)

// PTP_CONTACT.Flags layout (matches the report descriptor bit order)
#define PTP_FLAG_CONFIDENCE     0x01
#define PTP_FLAG_TIP            0x02
#define PTP_CONTACT_ID_SHIFT    2       // 3-bit contact id in bits 2..4

#include <pshpack1.h>
typedef struct _PTP_CONTACT {
    UCHAR  Flags;
    USHORT X;
    USHORT Y;
} PTP_CONTACT;

// Touchpad input report payload (everything after the report ID).
// The app sends exactly this after REPORTID_INJECT; the driver re-emits it after REPORTID_TOUCHPAD.
typedef struct _PTP_FRAME {
    PTP_CONTACT Contacts[PTP_MAX_CONTACTS];
    USHORT      ScanTime;       // 100 us units, wraps at 65536
    UCHAR       ContactCount;   // contacts in this frame (including ones lifting)
    UCHAR       Buttons;        // bit0 = button 1
} PTP_FRAME;
#include <poppack.h>

C_ASSERT(sizeof(PTP_CONTACT) == 5);
C_ASSERT(sizeof(PTP_FRAME) == 29);   // must match REPORT_COUNT (0x1D) of the inject report

#define VPTP_REPORT_SIZE (1 + sizeof(PTP_FRAME))   // 30 bytes incl. report ID
