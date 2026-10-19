#!/usr/bin/env python3
"""Generate the adb_coexist variant of the reconstructed iphone.c (source of profiles/adb-coexist.patch).

    kernel/gen-adb-coexist.py <iphone.c with usb-adb applied> <out iphone.c>
    diff -u a/drivers/usb/gadget/legacy/iphone.c b/drivers/usb/gadget/legacy/iphone.c > profiles/adb-coexist.patch
"""
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
s = src.read_text()


def rep(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'anchor count {s.count(old)}: {old[:100]!r}')
    s = s.replace(old, new)


# 1. FunctionFS internals; legacy/Makefile adds drivers/usb/gadget/function/ to the include path.
rep('#include "g_zero.h"\n', '#include "g_zero.h"\n#include "u_fs.h"\n')

# 2. g_iphone always includes ADB; only the CarPlay watchdog time is fixed.
rep('#include "u_fs.h"\n', '#include "u_fs.h"\n\n#define ADB_WAIT_MS 5000\n')

# 2b. The vendor code never initializes autoresume_step_ms, so the first
# remote-wakeup delay is 0 instead of autoresume seconds.
rep('autoresume_cdev=d; timer_setup(&autoresume_timer,zero_autoresume,0);',
    'autoresume_cdev=d; timer_setup(&autoresume_timer,zero_autoresume,0);\n'
    '\tautoresume_step_ms=autoresume*1000;')

# 3. State and the per-configuration helper, after add_acm().
anchor = 'static void zero_autoresume(struct timer_list *timer)'
helper = '''static struct usb_function_instance *func_inst_ffs;
static struct usb_function *func_ffs[3];
static bool adb_ffs_ready;
static bool adb_probed;
static DEFINE_MUTEX(adb_probe_lock);
static struct delayed_work adb_probe_work;

static int add_ffs(struct usb_configuration *c, int idx)
{
\tstruct usb_function *f;
\tint ret;

\tif (!adb_ffs_ready || !func_inst_ffs || func_ffs[idx])
\t\treturn 0;
\tf = usb_get_function(func_inst_ffs);
\tif (IS_ERR(f)) {
\t\tdev_err(&c->cdev->gadget->dev, "adb: ffs function unavailable: %ld\\n", PTR_ERR(f));
\t\treturn 0;
\t}
\tret = usb_add_function(c, f);
\tif (ret) {
\t\tdev_err(&c->cdev->gadget->dev, "adb: cannot add ffs to config %d: %d\\n",
\t\t\tc->bConfigurationValue, ret);
\t\tusb_put_function(f);
\t\treturn 0;
\t}
\tfunc_ffs[idx] = f;
\tdev_info(&c->cdev->gadget->dev, "adb: FunctionFS enabled in configuration %d\\n",
\t\t c->bConfigurationValue);
\treturn 0;
}

''' + anchor
rep(anchor, helper)

# 4. FFS last in configurations 1, 3 and 4 (never 2).
rep('ret=add_ncm(&ptp_driver); if (ret) goto err_func_lb2;',
    'ret=add_ncm(&ptp_driver); if (ret) goto err_func_lb2;\n\tadd_ffs(&ptp_driver, 0);')
rep('ret=usb_add_function(&ptp_amd_driver,func_ss2); if (ret) goto err_func_lb2;',
    'ret=usb_add_function(&ptp_amd_driver,func_ss2); if (ret) goto err_func_lb2;\n'
    '\tadd_ffs(&ptp_amd_driver, 1);')
rep('ret=add_acm(&ptp_amd_aue_driver); if (ret) goto err_func_lb2;',
    'ret=add_acm(&ptp_amd_aue_driver); if (ret) goto err_func_lb2;\n'
    '\tadd_ffs(&ptp_amd_aue_driver, 2);')

# 5. Release the per-configuration FFS functions in zero_unbind().
rep('\tfunc_ss=func_ss2=func_lb=func_lb2=NULL; func_inst_ss=func_inst_lb=NULL; return 0;',
    '\t{\n'
    '\t\tint i;\n'
    '\t\tfor (i = 0; i < ARRAY_SIZE(func_ffs); i++) {\n'
    '\t\t\tif (!IS_ERR_OR_NULL(func_ffs[i])) usb_put_function(func_ffs[i]);\n'
    '\t\t\tfunc_ffs[i] = NULL;\n'
    '\t\t}\n'
    '\t}\n'
    '\tfunc_ss=func_ss2=func_lb=func_lb2=NULL; func_inst_ss=func_inst_lb=NULL; return 0;')

# 6. Ready/closed callbacks and the timeout fallback, after zero_driver.
anchor2 = 'static int __init iphone_init(void) { return usb_composite_probe(&zero_driver); }'
callbacks = '''static void iphone_adb_probe_work(struct work_struct *work)
{
\tint ret;

\tmutex_lock(&adb_probe_lock);
\tif (adb_probed) {
\t\tmutex_unlock(&adb_probe_lock);
\t\treturn;
\t}
\tpr_warn("adb: adbd not ready in %u ms; probing without ADB\\n", ADB_WAIT_MS);
\tret = usb_composite_probe(&zero_driver);
\tif (!ret)
\t\tadb_probed = true;
\telse
\t\tpr_err("adb_coexist: probe failed: %d\\n", ret);
\tmutex_unlock(&adb_probe_lock);
}

static int iphone_ffs_ready(struct ffs_data *ffs)
{
\tint ret;

\tmutex_lock(&adb_probe_lock);
\tif (adb_probed) {
\t\tmutex_unlock(&adb_probe_lock);
\t\treturn 0;
\t}
\tadb_ffs_ready = true;
\tret = usb_composite_probe(&zero_driver);
\tif (ret < 0) {
\t\tadb_ffs_ready = false;
\t\tpr_err("adb_coexist: probe failed: %d\\n", ret);
\t} else {
\t\tadb_probed = true;
\t}
\tmutex_unlock(&adb_probe_lock);
\treturn ret;
}

static void iphone_ffs_closed(struct ffs_data *ffs)
{
\tmutex_lock(&adb_probe_lock);
\tif (adb_probed) {
\t\tadb_probed = false;
\t\tadb_ffs_ready = false;
\t\tusb_composite_unregister(&zero_driver);
\t}
\tmutex_unlock(&adb_probe_lock);
}

/*
 * Hold a reference on g_iphone while adbd owns FunctionFS.  The vendor
 * userspace runs "rmmod g_iphone" when CarPlay times out; without this
 * reference the unload starts, tears the composite down (killing NCM and
 * ADB on the same UDC) and then hangs.  With the reference rmmod fails
 * with EBUSY before anything is unbound.
 */
static void *iphone_acquire_dev(struct ffs_dev *dev)
{
\tif (!try_module_get(THIS_MODULE))
\t\treturn ERR_PTR(-ENOENT);
\treturn NULL;
}

static void iphone_release_dev(struct ffs_dev *dev)
{
\tmodule_put(THIS_MODULE);
}

''' + anchor2
rep(anchor2, callbacks)

# 7. Deferred init and matching cleanup.
rep(anchor2, '''static int __init iphone_init(void)
{
\tstruct f_fs_opts *opts;
\tint ret;

\tINIT_DELAYED_WORK(&adb_probe_work, iphone_adb_probe_work);
\tfunc_inst_ffs = usb_get_function_instance("ffs");
\tif (IS_ERR(func_inst_ffs)) {
\t\tpr_err("adb: ffs instance unavailable: %ld\\n", PTR_ERR(func_inst_ffs));
\t\tfunc_inst_ffs = NULL;
\t\tret = usb_composite_probe(&zero_driver);
\t\tadb_probed = !ret;
\t\treturn ret;
\t}
\topts = to_f_fs_opts(func_inst_ffs);
\tret = ffs_name_dev(opts->dev, "adb");
\tif (ret) {
\t\tpr_err("adb: cannot name ffs device: %d\\n", ret);
\t\tusb_put_function_instance(func_inst_ffs);
\t\tfunc_inst_ffs = NULL;
\t\tret = usb_composite_probe(&zero_driver);
\t\tadb_probed = !ret;
\t\treturn ret;
\t}
\topts->dev->ffs_ready_callback = iphone_ffs_ready;
\topts->dev->ffs_closed_callback = iphone_ffs_closed;
\topts->dev->ffs_acquire_dev_callback = iphone_acquire_dev;
\topts->dev->ffs_release_dev_callback = iphone_release_dev;
\topts->no_configfs = true;
\tschedule_delayed_work(&adb_probe_work, msecs_to_jiffies(ADB_WAIT_MS));
\tpr_info("adb: waiting up to %u ms for adbd descriptors\\n", ADB_WAIT_MS);
\treturn 0;
}
''')
rep('static void __exit iphone_cleanup(void) { usb_composite_unregister(&zero_driver); }',
    '''static void __exit iphone_cleanup(void)
{
\tcancel_delayed_work_sync(&adb_probe_work);
\tif (adb_probed)
\t\tusb_composite_unregister(&zero_driver);
\tif (func_inst_ffs) {
\t\tusb_put_function_instance(func_inst_ffs);
\t\tfunc_inst_ffs = NULL;
\t}
}
''')

out.write_text(s)
