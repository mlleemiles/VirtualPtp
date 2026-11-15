// HidDescriptor.h - HID report descriptor for the virtual Precision Touchpad.
// Layout follows Microsoft's "Sample Report Descriptors" for Windows Precision Touchpad,
// extended to 5 parallel finger slots plus a vendor collection used for injection.
#pragma once

#include "VirtualPtpShared.h"

#define VPTP_LO(v) ((UCHAR)((v) & 0xFF))
#define VPTP_HI(v) ((UCHAR)(((v) >> 8) & 0xFF))

// One finger slot = 5 bytes: [conf:1 tip:1 id:3 pad:3] [X:16] [Y:16]
// Units / physical extents are reset at the start of each slot so they don't leak
// from the X/Y items of the previous slot into the next slot's switches.
#define PTP_FINGER_SLOT                                                              \
    0x05, 0x0D,                         /*   USAGE_PAGE (Digitizers)             */ \
    0x09, 0x22,                         /*   USAGE (Finger)                      */ \
    0xA1, 0x02,                         /*   COLLECTION (Logical)                */ \
    0x55, 0x00,                         /*     UNIT_EXPONENT (0)                 */ \
    0x65, 0x00,                         /*     UNIT (None)                       */ \
    0x35, 0x00,                         /*     PHYSICAL_MINIMUM (0)              */ \
    0x45, 0x00,                         /*     PHYSICAL_MAXIMUM (0)              */ \
    0x15, 0x00,                         /*     LOGICAL_MINIMUM (0)               */ \
    0x25, 0x01,                         /*     LOGICAL_MAXIMUM (1)               */ \
    0x09, 0x47,                         /*     USAGE (Confidence)                */ \
    0x09, 0x42,                         /*     USAGE (Tip Switch)                */ \
    0x95, 0x02,                         /*     REPORT_COUNT (2)                  */ \
    0x75, 0x01,                         /*     REPORT_SIZE (1)                   */ \
    0x81, 0x02,                         /*     INPUT (Data,Var,Abs)              */ \
    0x25, 0x04,                         /*     LOGICAL_MAXIMUM (4)               */ \
    0x09, 0x51,                         /*     USAGE (Contact Identifier)        */ \
    0x95, 0x01,                         /*     REPORT_COUNT (1)                  */ \
    0x75, 0x03,                         /*     REPORT_SIZE (3)                   */ \
    0x81, 0x02,                         /*     INPUT (Data,Var,Abs)              */ \
    0x75, 0x01,                         /*     REPORT_SIZE (1)                   */ \
    0x95, 0x03,                         /*     REPORT_COUNT (3)                  */ \
    0x81, 0x03,                         /*     INPUT (Cnst,Var,Abs) padding      */ \
    0x05, 0x01,                         /*     USAGE_PAGE (Generic Desktop)      */ \
    0x75, 0x10,                         /*     REPORT_SIZE (16)                  */ \
    0x95, 0x01,                         /*     REPORT_COUNT (1)                  */ \
    0x55, 0x0E,                         /*     UNIT_EXPONENT (-2)                */ \
    0x65, 0x13,                         /*     UNIT (Inch, EngLinear)            */ \
    0x26, VPTP_LO(PTP_LOGICAL_MAX_X), VPTP_HI(PTP_LOGICAL_MAX_X),                  \
                                        /*     LOGICAL_MAXIMUM (X)               */ \
    0x46, VPTP_LO(PTP_PHYSICAL_MAX_X), VPTP_HI(PTP_PHYSICAL_MAX_X),                \
                                        /*     PHYSICAL_MAXIMUM (X)              */ \
    0x09, 0x30,                         /*     USAGE (X)                         */ \
    0x81, 0x02,                         /*     INPUT (Data,Var,Abs)              */ \
    0x26, VPTP_LO(PTP_LOGICAL_MAX_Y), VPTP_HI(PTP_LOGICAL_MAX_Y),                  \
                                        /*     LOGICAL_MAXIMUM (Y)               */ \
    0x46, VPTP_LO(PTP_PHYSICAL_MAX_Y), VPTP_HI(PTP_PHYSICAL_MAX_Y),                \
                                        /*     PHYSICAL_MAXIMUM (Y)              */ \
    0x09, 0x31,                         /*     USAGE (Y)                         */ \
    0x81, 0x02,                         /*     INPUT (Data,Var,Abs)              */ \
    0xC0                                /*   END_COLLECTION                      */

static const UCHAR g_ReportDescriptor[] = {
    // ---------------- Precision Touchpad TLC ----------------
    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x05,                         // USAGE (Touch Pad)
    0xA1, 0x01,                         // COLLECTION (Application)
    0x85, REPORTID_TOUCHPAD,            //   REPORT_ID (Touchpad)

    PTP_FINGER_SLOT,
    PTP_FINGER_SLOT,
    PTP_FINGER_SLOT,
    PTP_FINGER_SLOT,
    PTP_FINGER_SLOT,

    0x55, 0x0C,                         //   UNIT_EXPONENT (-4)
    0x66, 0x01, 0x10,                   //   UNIT (Seconds)
    0x47, 0xFF, 0xFF, 0x00, 0x00,       //   PHYSICAL_MAXIMUM (65535)
    0x27, 0xFF, 0xFF, 0x00, 0x00,       //   LOGICAL_MAXIMUM (65535)
    0x75, 0x10,                         //   REPORT_SIZE (16)
    0x95, 0x01,                         //   REPORT_COUNT (1)
    0x05, 0x0D,                         //   USAGE_PAGE (Digitizers)
    0x09, 0x56,                         //   USAGE (Scan Time)
    0x81, 0x02,                         //   INPUT (Data,Var,Abs)
    0x09, 0x54,                         //   USAGE (Contact Count)
    0x25, 0x7F,                         //   LOGICAL_MAXIMUM (127)
    0x95, 0x01,                         //   REPORT_COUNT (1)
    0x75, 0x08,                         //   REPORT_SIZE (8)
    0x81, 0x02,                         //   INPUT (Data,Var,Abs)
    0x05, 0x09,                         //   USAGE_PAGE (Button)
    0x09, 0x01,                         //   USAGE (Button 1)
    0x25, 0x01,                         //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         //   REPORT_SIZE (1)
    0x95, 0x01,                         //   REPORT_COUNT (1)
    0x81, 0x02,                         //   INPUT (Data,Var,Abs)
    0x95, 0x07,                         //   REPORT_COUNT (7)
    0x81, 0x03,                         //   INPUT (Cnst,Var,Abs) padding

    0x05, 0x0D,                         //   USAGE_PAGE (Digitizers)
    0x85, REPORTID_MAX_COUNT,           //   REPORT_ID (Device capabilities)
    0x09, 0x55,                         //   USAGE (Contact Count Maximum)
    0x09, 0x59,                         //   USAGE (Pad Type)
    0x75, 0x04,                         //   REPORT_SIZE (4)
    0x95, 0x02,                         //   REPORT_COUNT (2)
    0x25, 0x0F,                         //   LOGICAL_MAXIMUM (15)
    0xB1, 0x02,                         //   FEATURE (Data,Var,Abs)

    0x06, 0x00, 0xFF,                   //   USAGE_PAGE (Vendor Defined)
    0x85, REPORTID_PTPHQA,              //   REPORT_ID (PTPHQA)
    0x09, 0xC5,                         //   USAGE (Vendor Usage 0xC5)
    0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                         //   REPORT_SIZE (8)
    0x96, 0x00, 0x01,                   //   REPORT_COUNT (256)
    0xB1, 0x02,                         //   FEATURE (Data,Var,Abs)
    0xC0,                               // END_COLLECTION

    // ---------------- Configuration TLC ----------------
    0x05, 0x0D,                         // USAGE_PAGE (Digitizers)
    0x09, 0x0E,                         // USAGE (Configuration)
    0xA1, 0x01,                         // COLLECTION (Application)
    0x55, 0x00,                         //   UNIT_EXPONENT (0)
    0x65, 0x00,                         //   UNIT (None)
    0x35, 0x00,                         //   PHYSICAL_MINIMUM (0)
    0x45, 0x00,                         //   PHYSICAL_MAXIMUM (0)
    0x85, REPORTID_INPUT_MODE,          //   REPORT_ID (Input mode)
    0x09, 0x22,                         //   USAGE (Finger)
    0xA1, 0x02,                         //   COLLECTION (Logical)
    0x09, 0x52,                         //     USAGE (Input Mode)
    0x15, 0x00,                         //     LOGICAL_MINIMUM (0)
    0x25, 0x0A,                         //     LOGICAL_MAXIMUM (10)
    0x75, 0x08,                         //     REPORT_SIZE (8)
    0x95, 0x01,                         //     REPORT_COUNT (1)
    0xB1, 0x02,                         //     FEATURE (Data,Var,Abs)
    0xC0,                               //   END_COLLECTION
    0x09, 0x22,                         //   USAGE (Finger)
    0xA1, 0x00,                         //   COLLECTION (Physical)
    0x85, REPORTID_FUNCTION_SWITCH,     //     REPORT_ID (Function switch)
    0x09, 0x57,                         //     USAGE (Surface Switch)
    0x09, 0x58,                         //     USAGE (Button Switch)
    0x75, 0x01,                         //     REPORT_SIZE (1)
    0x95, 0x02,                         //     REPORT_COUNT (2)
    0x25, 0x01,                         //     LOGICAL_MAXIMUM (1)
    0xB1, 0x02,                         //     FEATURE (Data,Var,Abs)
    0x95, 0x06,                         //     REPORT_COUNT (6)
    0xB1, 0x03,                         //     FEATURE (Cnst,Var,Abs) padding
    0xC0,                               //   END_COLLECTION
    0xC0,                               // END_COLLECTION

    // ---------------- Mouse TLC (required by the spec, never reported) ----------------
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                         // USAGE (Mouse)
    0xA1, 0x01,                         // COLLECTION (Application)
    0x85, REPORTID_MOUSE,               //   REPORT_ID (Mouse)
    0x09, 0x01,                         //   USAGE (Pointer)
    0xA1, 0x00,                         //   COLLECTION (Physical)
    0x05, 0x09,                         //     USAGE_PAGE (Button)
    0x19, 0x01,                         //     USAGE_MINIMUM (Button 1)
    0x29, 0x02,                         //     USAGE_MAXIMUM (Button 2)
    0x15, 0x00,                         //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                         //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                         //     REPORT_SIZE (1)
    0x95, 0x02,                         //     REPORT_COUNT (2)
    0x81, 0x02,                         //     INPUT (Data,Var,Abs)
    0x95, 0x06,                         //     REPORT_COUNT (6)
    0x81, 0x03,                         //     INPUT (Cnst,Var,Abs) padding
    0x05, 0x01,                         //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                         //     USAGE (X)
    0x09, 0x31,                         //     USAGE (Y)
    0x15, 0x81,                         //     LOGICAL_MINIMUM (-127)
    0x25, 0x7F,                         //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                         //     REPORT_SIZE (8)
    0x95, 0x02,                         //     REPORT_COUNT (2)
    0x81, 0x06,                         //     INPUT (Data,Var,Rel)
    0xC0,                               //   END_COLLECTION
    0xC0,                               // END_COLLECTION

    // ---------------- Vendor injection TLC (app -> driver) ----------------
    0x06, 0x00, 0xFF,                   // USAGE_PAGE (Vendor Defined 0xFF00)
    0x09, 0x01,                         // USAGE (0x01)
    0xA1, 0x01,                         // COLLECTION (Application)
    0x85, REPORTID_INJECT,              //   REPORT_ID (Inject)
    0x09, 0x02,                         //   USAGE (0x02)
    0x15, 0x00,                         //   LOGICAL_MINIMUM (0)
    0x26, 0xFF, 0x00,                   //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                         //   REPORT_SIZE (8)
    0x95, 0x1D,                         //   REPORT_COUNT (29) == sizeof(PTP_FRAME)
    0x91, 0x02,                         //   OUTPUT (Data,Var,Abs)
    0xC0,                               // END_COLLECTION
};

// Default (uncertified) PTPHQA blob from Microsoft's PTP collection docs.
// Windows 10/11 only check that 256 bytes are reported, not their contents.
static const UCHAR g_PtpHqaBlob[256] = {
    0xfc, 0x28, 0xfe, 0x84, 0x40, 0xcb, 0x9a, 0x87, 0x0d, 0xbe, 0x57, 0x3c, 0xb6, 0x70, 0x09, 0x88, 0x07,
    0x97, 0x2d, 0x2b, 0xe3, 0x38, 0x34, 0xb6, 0x6c, 0xed, 0xb0, 0xf7, 0xe5, 0x9c, 0xf6, 0xc2, 0x2e, 0x84,
    0x1b, 0xe8, 0xb4, 0x51, 0x78, 0x43, 0x1f, 0x28, 0x4b, 0x7c, 0x2d, 0x53, 0xaf, 0xfc, 0x47, 0x70, 0x1b,
    0x59, 0x6f, 0x74, 0x43, 0xc4, 0xf3, 0x47, 0x18, 0x53, 0x1a, 0xa2, 0xa1, 0x71, 0xc7, 0x95, 0x0e, 0x31,
    0x55, 0x21, 0xd3, 0xb5, 0x1e, 0xe9, 0x0c, 0xba, 0xec, 0xb8, 0x89, 0x19, 0x3e, 0xb3, 0xaf, 0x75, 0x81,
    0x9d, 0x53, 0xb9, 0x41, 0x57, 0xf4, 0x6d, 0x39, 0x25, 0x29, 0x7c, 0x87, 0xd9, 0xb4, 0x98, 0x45, 0x7d,
    0xa7, 0x26, 0x9c, 0x65, 0x3b, 0x85, 0x68, 0x89, 0xd7, 0x3b, 0xbd, 0xff, 0x14, 0x67, 0xf2, 0x2b, 0xf0,
    0x2a, 0x41, 0x54, 0xf0, 0xfd, 0x2c, 0x66, 0x7c, 0xf8, 0xc0, 0x8f, 0x33, 0x13, 0x03, 0xf1, 0xd3, 0xc1, 0x0b,
    0x89, 0xd9, 0x1b, 0x62, 0xcd, 0x51, 0xb7, 0x80, 0xb8, 0xaf, 0x3a, 0x10, 0xc1, 0x8a, 0x5b, 0xe8, 0x8a,
    0x56, 0xf0, 0x8c, 0xaa, 0xfa, 0x35, 0xe9, 0x42, 0xc4, 0xd8, 0x55, 0xc3, 0x38, 0xcc, 0x2b, 0x53, 0x5c,
    0x69, 0x52, 0xd5, 0xc8, 0x73, 0x02, 0x38, 0x7c, 0x73, 0xb6, 0x41, 0xe7, 0xff, 0x05, 0xd8, 0x2b, 0x79,
    0x9a, 0xe2, 0x34, 0x60, 0x8f, 0xa3, 0x32, 0x1f, 0x09, 0x78, 0x62, 0xbc, 0x80, 0xe3, 0x0f, 0xbd, 0x65,
    0x20, 0x08, 0x13, 0xc1, 0xe2, 0xee, 0x53, 0x2d, 0x86, 0x7e, 0xa7, 0x5a, 0xc5, 0xd3, 0x7d, 0x98, 0xbe,
    0x31, 0x48, 0x1f, 0xfb, 0xda, 0xaf, 0xa2, 0xa8, 0x6a, 0x89, 0xd6, 0xbf, 0xf2, 0xd3, 0x32, 0x2a, 0x9a,
    0xe4, 0xcf, 0x17, 0xb7, 0xb8, 0xf4, 0xe1, 0x33, 0x08, 0x24, 0x8b, 0xc4, 0x43, 0xa5, 0xe5, 0x24, 0xc2,
};
