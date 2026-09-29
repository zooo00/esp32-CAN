#pragma once
// Wire format shared by Node A (BLE server) and Node B (BLE client).
//
// One BLE notification = gsa_hdr_t followed by N frame_t records.

#include <stdint.h>

#define GSA_PROTO_VERSION 1
#define GSA_DEVICE_NAME   "GSA-CAN"

// 128-bit UUIDs, byte order as NimBLE's BLE_UUID128_INIT() expects (little-endian).
// Service:        6a1c0001-5f3e-4b8a-9c2d-7e4f1a2b3c4d
// Characteristic: 6a1c0002-5f3e-4b8a-9c2d-7e4f1a2b3c4d (notify)
#define GSA_SVC_UUID 0x4d, 0x3c, 0x2b, 0x1a, 0x4f, 0x7e, 0x2d, 0x9c, 0x8a, 0x4b, 0x3e, 0x5f, 0x01, 0x00, 0x1c, 0x6a
#define GSA_CHR_UUID 0x4d, 0x3c, 0x2b, 0x1a, 0x4f, 0x7e, 0x2d, 0x9c, 0x8a, 0x4b, 0x3e, 0x5f, 0x02, 0x00, 0x1c, 0x6a

// Where the frames come from. STUB data must never be mistaken for real bike data.
enum { GSA_SRC_REAL = 1, GSA_SRC_STUB = 2 };

// Flag bits in frame_t.id
#define GSA_ID_EXT (1u << 31) // 29-bit identifier
#define GSA_ID_RTR (1u << 30) // remote frame
#define GSA_ID_MASK 0x1FFFFFFFu

typedef struct __attribute__((packed)) {
    uint8_t version; // GSA_PROTO_VERSION
    uint8_t source;  // GSA_SRC_*
} gsa_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t ts_ms; // Node A's clock at reception/generation
    uint32_t id;    // CAN ID | GSA_ID_* flags
    uint8_t dlc;
    uint8_t d[8];
} frame_t;

_Static_assert(sizeof(gsa_hdr_t) == 2, "header must be 2 bytes");
_Static_assert(sizeof(frame_t) == 17, "frame_t must be 17 bytes");

#define GSA_MAX_NOTIFY 244 // ATT MTU 247 - 3
