#!/usr/bin/env python3
"""Apply the pinned bundle, retaining NXP's newer role-switch guard."""
import pathlib,re,subprocess,sys
def recount_hunks(patch):
    # The distributed 0024 patch has incorrect unified-diff hunk counts.
    # Correct headers only; preserve every source/context line unchanged.
    lines=patch.splitlines(keepends=True)
    for i,line in enumerate(lines):
        match=re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)',line)
        if not match:continue
        old=new=0
        for item in lines[i+1:]:
            if item.startswith(('@@','diff --git','--- ','+++ ')):break
            if item.startswith(' '):old+=1;new+=1
            elif item.startswith('-'):old+=1
            elif item.startswith('+'):new+=1
            elif item.startswith('\\'):continue
            else:break
        lines[i]=f'@@ -{match[1]},{old} +{match[2]},{new} @@{match[3]}\n'
    return ''.join(lines)
source,bundle=map(pathlib.Path,sys.argv[1:])
for name in (bundle/'patches/series').read_text().splitlines():
    if not name or name.startswith('#'):continue
    patch=(bundle/'patches'/name).read_text()
    if name in ['0023-realtek-use-netdev-mac-address-helper.patch','0024-realtek-preserve-board-mac-address.patch']:
        # The pinned new-files overlay already contains the final combined
        # versions of these two changes. Check their actual code before skipping.
        radio=(source/'drivers/net/wireless/rtl8822bs/os_dep/linux/os_intfs.c').read_text()
        expected=['eth_hw_addr_set(pnetdev, mac);','static const u8 rtl_fallback_mac[ETH_ALEN]',
                  'mac = adapter_mac_addr(padapter);','rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR, mac);']
        if all(item in radio for item in expected):
            print('Verified already present in overlay:',name,flush=True)
            continue
    if name=='0010-usb-chipidea-skip-vbus-low-role-switch-wait.patch':
        old=' \t\tif (role == CI_ROLE_GADGET &&\n-\t\t\t\tIS_ERR(ci->platdata->vbus_extcon.edev))\n'
        new=' \t\tif (role == CI_ROLE_GADGET && !ci->role_switch &&\n-\t\t    IS_ERR(ci->platdata->vbus_extcon.edev))\n'
        if patch.count(old)!=1:raise SystemExit('Upstream patch 0010 changed; review required')
        patch=patch.replace(old,new)
    # Several distributed patches have stale counts (including 0023/0024).
    patch=recount_hunks(patch)
    print('Applying',name,flush=True)
    subprocess.run(['patch','-d',str(source),'-p1','--batch','--forward'],input=patch,text=True,check=True)
