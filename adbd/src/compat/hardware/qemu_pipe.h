/* U2W: not an emulator; adbd treats a failed qemu pipe as "no emulator". */
#ifndef U2W_HARDWARE_QEMU_PIPE_H
#define U2W_HARDWARE_QEMU_PIPE_H
#include <errno.h>
static inline int qemu_pipe_open(const char *name) { (void)name; errno = ENOENT; return -1; }
#endif
