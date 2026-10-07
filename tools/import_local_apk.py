"""Restore build inputs from the matching local APK; these stay Git-ignored."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT=Path(__file__).resolve().parents[1]
EXPECTED='9a6df89af423769878c1d8032d297b0930b914d7153483ca643d889cd62fa410'
ORIGINAL={'bin.dat','xuanyuan.mod','map.hkm','mapb.hkm','mapc.hkm','mbxres.bin','mbxresb.bin','mbxresc.bin','save.dat'}

def restore(apk,replace=False):
    if hashlib.sha256(apk.read_bytes()).hexdigest()!=EXPECTED:
        raise ValueError('APK does not match the recorded v0.7 build; nothing imported.')
    pending=[]
    with zipfile.ZipFile(apk) as archive:
        if archive.testzip() is not None:raise ValueError('Corrupt APK archive')
        for info in archive.infolist():
            name=info.filename
            if name.endswith('/'):continue
            if name.startswith('assets/original/') and name.removeprefix('assets/original/') in ORIGINAL:
                destination=ROOT/'original'/name.removeprefix('assets/original/')
            elif name.startswith('assets/hd/'):
                destination=ROOT/'native_hd/assets'/name.removeprefix('assets/hd/')
            elif name=='res/drawable-nodpi-v4/ic_launcher.png':
                destination=ROOT/'native_hd/android/res/drawable-nodpi/ic_launcher.png'
            else:continue
            if not destination.resolve().is_relative_to(ROOT.resolve()):raise ValueError('Unsafe archive path')
            data=archive.read(info)
            if destination.exists() and destination.read_bytes()!=data and not replace:
                raise ValueError(f'Local file differs: {destination.relative_to(ROOT)}. Back it up before using --replace.')
            pending.append((destination,data))
        names={path.name for path,_ in pending if path.parent==ROOT/'original'}
        if names!=ORIGINAL:raise ValueError('Missing original game files')
        manifest=json.loads(archive.read('assets/hd/manifest.json'))
        if len(manifest['textures'])!=320 or manifest['physical_viewport']!=[1440,1920]:
            raise ValueError('Unexpected HD resource manifest')
    for destination,data in pending:
        destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data)
    print(f'Restored {len(pending)} local files. Original game inputs remain excluded from Git.')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk',type=Path);parser.add_argument('--replace',action='store_true')
    args=parser.parse_args()
    try:restore(args.apk,args.replace)
    except (ValueError,OSError,zipfile.BadZipFile) as e:parser.exit(1,str(e)+'\n')
