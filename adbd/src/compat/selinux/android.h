/* U2W: the dongle kernel has no SELinux. */
#ifndef U2W_SELINUX_ANDROID_H
#define U2W_SELINUX_ANDROID_H
static inline int selinux_android_restorecon(const char *path, unsigned int flags) { (void)path; (void)flags; return 0; }
#endif
