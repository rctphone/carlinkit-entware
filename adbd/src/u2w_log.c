/* U2W: there is no Android logd; liblog calls go to stderr (the init script's log). */
#include <stdarg.h>
#include <stdio.h>

int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    va_list ap;
    int n;
    fprintf(stderr, "%s(%d): ", tag ? tag : "adbd", prio);
    va_start(ap, fmt);
    n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return n;
}
