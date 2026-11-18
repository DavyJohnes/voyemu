#ifndef VOYEMU_SCREEN_CAPTURE_H
#define VOYEMU_SCREEN_CAPTURE_H
#include <stddef.h>
/* The sink must consume the complete chunk synchronously; zero means success.
 * finish receives complete=0 after a failed write and must remove partial output.
 * The complete BMP must be closed/published before destroy_pixmap is called,
 * because the existing stream hook reads that artifact during destruction. */
typedef int (*VoyemuCaptureWrite)(void *, const unsigned char *, size_t);
typedef int (*VoyemuCaptureFinish)(void *, int complete);
/* Requires initialized QNX libc and Screen; does not implement a process CRT.
 * Returns zero only after a complete fixed-size frame and successful cleanup. */
int voyemu_screen_capture(VoyemuCaptureWrite write, VoyemuCaptureFinish finish, void *sink);
#endif
