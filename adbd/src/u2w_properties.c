/*
 * U2W replacement for Android system properties. There is no property service on the
 * dongle, so adbd's few lookups are answered here and nothing can be set at runtime.
 *
 *   ro.secure=0, ro.debuggable=1  adbd keeps root; this private development
 *                                 image does not rely on the RSA key check
 *   ro.adb.secure=0               ADB authentication disabled by request:
 *                                 any client is authorized (private dev image)
 *   anything else                 unset (adbd has no TCP transport to configure)
 */
#include <string.h>
#include <cutils/properties.h>

static const char *lookup(const char *key)
{
    static const struct { const char *key, *value; } fixed[] = {
        { "ro.secure", "0" },
        { "ro.debuggable", "1" },
        { "ro.adb.secure", "0" },
        { "ro.kernel.qemu", "0" },
        /* Human-friendly banner for `adb devices -l` (the USB serial stays the
         * vendor device_serial because the Apple/iAP2 identity uses it). */
        { "ro.product.name", "u2w" },
        { "ro.product.model", "CarlinKit_U2W" },
        { "ro.product.device", "u2w" },
    };
    size_t i;
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
        if (!strcmp(key, fixed[i].key)) return fixed[i].value;
    return NULL;
}

int property_get(const char *key, char *value, const char *default_value)
{
    const char *v = lookup(key);
    if (v == NULL) v = default_value ? default_value : "";
    strncpy(value, v, PROPERTY_VALUE_MAX - 1);
    value[PROPERTY_VALUE_MAX - 1] = '\0';
    return strlen(value);
}

/* "adb root/tcpip/usb/reboot" request property changes; refuse them. */
int property_set(const char *key, const char *value)
{
    (void)key; (void)value;
    return -1;
}
