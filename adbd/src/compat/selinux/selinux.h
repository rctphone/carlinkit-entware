/* U2W: the dongle kernel has no SELinux. */
#ifndef U2W_SELINUX_SELINUX_H
#define U2W_SELINUX_SELINUX_H
static inline int is_selinux_enabled(void) { return 0; }
static inline int setcon(char *context) { (void)context; return 0; }
#endif
