/* Local little-endian AArch64 IPC, versioned independently of the SDK ABI. */
#ifndef TIDESENSE_CAMERA_PROTOCOL_H
#define TIDESENSE_CAMERA_PROTOCOL_H
#include <stdint.h>
#define CAMERA_MAGIC 0x54444341u
#define CAMERA_VERSION 1u
#define CAMERA_FSIN 1u
struct CameraHeader {
 uint32_t magic, version, width, height;
 uint64_t sequence, left_ns, right_ns;
 uint32_t left_ref, right_ref, flags, reserved;
};
#endif
