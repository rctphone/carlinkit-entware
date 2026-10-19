#!/usr/bin/env python3
"""Audit exported IPKs without executing payloads; report uncompressed installed bytes."""
import argparse, hashlib, io, json, pathlib, re, struct, subprocess, tarfile


def member_blob(p, prefix):
    if p.read_bytes().startswith(b'!<arch>\n'):
        members=subprocess.check_output(['ar','t',str(p)],text=True).splitlines()
        name=next((n for n in members if n.startswith(prefix)),None)
        if name is None:raise ValueError(f'missing {prefix}')
        return subprocess.check_output(['ar','p',str(p),name])
    with tarfile.open(p,mode='r:*') as outer:
        member=next((m for m in outer if pathlib.PurePosixPath(m.name).name.startswith(prefix)),None)
        if member is None:raise ValueError(f'missing {prefix}')
        return outer.extractfile(member).read()


# soname -> symbols the vendor binaries import from it (vendor-imports.txt).
VENDOR_IMPORTS={}
for line in pathlib.Path(__file__).with_name('vendor-imports.txt').read_text().splitlines():
    if line and not line.startswith('#'):
        soname,sym=line.split();VENDOR_IMPORTS.setdefault(soname,set()).add(sym)


def exports(data):
    """Defined GLOBAL/WEAK .dynsym names of an ELF32 little-endian file."""
    shoff=struct.unpack_from('<I',data,32)[0];shentsize,shnum=struct.unpack_from('<HH',data,46)
    sh=[struct.unpack_from('<10I',data,shoff+i*shentsize) for i in range(shnum)]
    out=set()
    for _,typ,_,_,off,size,link,_,_,_ in sh:
        if typ!=11:continue
        strtab=sh[link][4]
        for i in range(1,size//16):
            name,_,_,info,_,shndx=struct.unpack_from('<IIIBBH',data,off+16*i)
            if shndx and info>>4 in (1,2):
                out.add(data[strtab+name:data.index(b'\0',strtab+name)].decode())
    return out


def audit(p):
    size=0;elfs=[];adbd=None
    with tarfile.open(fileobj=io.BytesIO(member_blob(p,'data.tar')),mode='r:*') as t:
        for m in t:
            n=m.name.removeprefix('./').lstrip('/');base=pathlib.PurePosixPath(n).name
            if n=='opt' or n.startswith('opt/') or '..' in pathlib.PurePosixPath(n).parts:
                raise ValueError(f'forbidden path {m.name}')
            protected=('libc.so','libc-','ld-linux','ld-','libpthread','libdl.','librt.','libm.','libcrypt.so','libssl','libcrypto')
            # Allowed crypto drop-in: the fork's OpenSSL 1.1 pair under the vendor 2024 sonames.
            vendor_crypto=n in ('usr/lib/libcrypto.so.1.1','usr/lib/libssl.so.1.1')
            if not vendor_crypto and (((n.startswith('lib/') or n.startswith('usr/lib/')) and base.startswith(protected)) or base.startswith(('libssl','libcrypto'))):
                raise ValueError(f'vendor runtime/library replacement {m.name}')
            if n in ('bin/busybox','usr/bin/busybox','bin/sh','bin/ash','usr/bin/scp'):
                raise ValueError(f'protected command replacement {m.name}')
            if base.endswith('.ko') or 'host_key' in base or base in ('authorized_keys','adb_keys'):
                raise ValueError(f'module or shipped identity/key {m.name}')
            if (m.issym() or m.islnk()) and ('/opt' in m.linkname or '..' in pathlib.PurePosixPath(m.linkname).parts and m.islnk()):
                raise ValueError(f'forbidden link {m.name}')
            if not m.isfile():continue
            size+=m.size;data=t.extractfile(m).read()
            if data.startswith(b'\x7fELF'):
                if len(data)<52 or data[4:6]!=b'\x01\x01' or struct.unpack_from('<H',data,18)[0]!=40:
                    raise ValueError(f'not ELF32 little-endian ARM: {m.name}')
                flags=struct.unpack_from('<I',data,36)[0]
                if flags&0x400:raise ValueError(f'hard-float ARM ABI: {m.name}')
                if b'UPX!' in data or b'U"U"' in data:raise ValueError(f'UPX/vendor-packed executable: {m.name}')
                elfs.append({'path':n,'arm_flags':hex(flags)})
                for soname,need in VENDOR_IMPORTS.items():
                    if n.startswith('usr/lib/') and base.startswith(soname):
                        missing=need-exports(data)
                        if missing:raise ValueError(f'{n} lacks vendor imports: {" ".join(sorted(missing))}')
                if n=='usr/sbin/adbd':adbd=data
            elif data.startswith(b'#!') and re.search(rb'dropbear[^\n]*\s-B(?:\s|$)',data):
                raise ValueError(f'blank-password SSH invocation: {m.name}')
    metadata={}
    with tarfile.open(fileobj=io.BytesIO(member_blob(p,'control.tar')),mode='r:*') as t:
        control=next(m for m in t if pathlib.PurePosixPath(m.name).name=='control')
        for line in t.extractfile(control).read().decode().splitlines():
            if ': ' in line and not line.startswith(' '):
                k,v=line.split(': ',1);metadata[k]=v
    if metadata.get('Package')=='dropbear' and not metadata.get('Version','').endswith('-6'):
        raise ValueError('Dropbear is not the key-only service variant (release 6)')
    if metadata.get('Package')=='adbd':
        if adbd is None:raise ValueError('adbd package lacks usr/sbin/adbd')
        if b'/data/misc/adb/adb_keys\0' not in adbd or b'/system/bin/sh' in adbd:
            raise ValueError('adbd is not the U2W key-path/shell variant')
    return {'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'package':metadata.get('Package'),'depends':metadata.get('Depends',''),
            'ipk_bytes':p.stat().st_size,'installed_payload_bytes':size,
            'control_installed_size':metadata.get('Installed-Size'), 'elf_files':elfs,
            'limits':'Uncompressed regular-file bytes, not JFFS2 allocation; excludes dependencies. ELF flags only, not full imported-symbol/runtime compatibility.'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=pathlib.Path)
    parser.add_argument('packages',nargs='+',type=pathlib.Path)
    args=parser.parse_args();reports=[]
    for p in args.packages:
        try:report=audit(p)
        except (ValueError,tarfile.TarError) as exc:raise SystemExit(f'{p}: {exc}')
        reports.append(report)
        print(f'{p.name}: installed payload {report["installed_payload_bytes"]} bytes; package/ABI policy passed')
    if args.report:args.report.write_text(json.dumps(reports,indent=2)+'\n')
