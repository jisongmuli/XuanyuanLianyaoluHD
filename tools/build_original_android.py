"""Build the original ARM game HD Android carrier with official local tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
TC=ROOT/'.toolchain'
PROJECT=ROOT/'native_hd/android'
SDK=Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or TC/'android-sdk')
NDK=Path(os.environ.get('ANDROID_NDK_HOME') or TC/'android-ndk')
if os.environ.get('JAVA_HOME'):
    JAVA=Path(os.environ['JAVA_HOME'])/'bin'
else:
    java_dirs=sorted((TC/'java').glob('jdk*/bin'))
    JAVA=java_dirs[-1] if java_dirs else Path(shutil.which('javac') or 'javac').resolve().parent
EXE='.exe' if os.name=='nt' else ''
CMAKE=TC/'android-cmake/bin'/('cmake'+EXE)
if not CMAKE.exists():CMAKE=Path(shutil.which('cmake') or 'cmake')
NINJA=TC/'android-cmake/bin'/('ninja'+EXE)
if not NINJA.exists():NINJA=Path(shutil.which('ninja') or 'ninja')
BUILD=ROOT/'native_hd/android-build'
BT=SDK/'build-tools/36.0.0'
if os.name=='nt':os.environ['PATH']=r'C:\Program Files\Git\usr\bin'+os.pathsep+os.environ['PATH']
# The bundled GLib replacement does not require pkg-config packages. The
# configure script still probes for a command, so use the existing POSIX false.
os.environ['PKG_CONFIG']='false'
host='windows-x86_64' if os.name=='nt' else 'linux-x86_64'
os.environ['STRINGS']=str(NDK/'toolchains/llvm/prebuilt'/host/'bin'/('llvm-strings'+EXE)).replace('\\','/')
def run(args):
    print('Running:',Path(str(args[0])).name,flush=True)
    # aapt2 on Windows still uses narrow path handling. Relative paths avoid
    # Chinese workspace names in the file arguments without moving the project.
    command=[str(args[0])]
    for arg in args[1:]:
        if isinstance(arg,Path) and arg.is_absolute() and arg.is_relative_to(ROOT):
            arg=arg.relative_to(ROOT)
        command.append(str(arg))
    subprocess.run(command,check=True,cwd=ROOT)

def native(abi):
    target=BUILD/abi
    run([CMAKE,'-S',PROJECT,'-B',target,'-G','Ninja',
         '-DCMAKE_BUILD_TYPE=Release',f'-DCMAKE_MAKE_PROGRAM={NINJA}',
         f'-DCMAKE_TOOLCHAIN_FILE={NDK / "build/cmake/android.toolchain.cmake"}',
         f'-DANDROID_ABI={abi}','-DANDROID_PLATFORM=android-28','-DANDROID_STL=c++_static'])
    run([CMAKE,'--build',target,'-j','6'])

def compile_java():
    classes=BUILD/'classes';classes.mkdir(parents=True,exist_ok=True)
    java_files=list((PROJECT/'java').rglob('*.java'))
    run([JAVA/('javac'+EXE),'-encoding','UTF-8','-source','8','-target','8','-classpath',SDK/'platforms/android-36/android.jar','-d',classes,*java_files])
    return classes

def package(abis):
    BUILD.mkdir(parents=True,exist_ok=True)
    assets=BUILD/'assets'
    (assets/'original').mkdir(parents=True,exist_ok=True)
    for name in ['bin.dat','xuanyuan.mod','map.hkm','mapb.hkm','mapc.hkm','mbxres.bin','mbxresb.bin','mbxresc.bin','save.dat']:
        shutil.copy2(ROOT/'original'/name,assets/'original'/name)
    shutil.copytree(ROOT/'native_hd/assets',assets/'hd',dirs_exist_ok=True)
    for name in ['COPYING','COPYING.LGPL2','AUTHORS.TXT']:
        src=TC/'unicorn-source'/name
        if src.exists():
            (assets/'licenses').mkdir(exist_ok=True)
            shutil.copy2(src,assets/'licenses'/name)
    font_license=ROOT/'remaster/assets/fonts/OFL.txt'
    if not font_license.exists():font_license=ROOT/'licenses/Noto-OFL.txt'
    shutil.copy2(font_license,assets/'licenses/Noto-OFL.txt')
    classes=compile_java()
    dex=BUILD/'dex';dex.mkdir(exist_ok=True)
    run([JAVA/('java'+EXE),'-cp',BT/'lib/d8.jar','com.android.tools.r8.D8','--lib',SDK/'platforms/android-36/android.jar','--min-api','28','--output',dex,*classes.rglob('*.class')])
    raw=BUILD/'unsigned.apk'
    manifest_apk=BUILD/'manifest.apk'
    resource_dir=PROJECT/'res'
    resource_args=[]
    if resource_dir.exists():
        compiled_res=BUILD/'compiled-res.zip'
        run([BT/('aapt2'+EXE),'compile','--dir',resource_dir,'-o',compiled_res])
        resource_args=['-R',compiled_res]
    run([BT/('aapt2'+EXE),'link','--manifest',PROJECT/'AndroidManifest.xml','-I',SDK/'platforms/android-36/android.jar',*resource_args,'-o',manifest_apk])
    # Recreate one consistent ZIP rather than mixing aapt2 and Python ZIP headers.
    with zipfile.ZipFile(raw,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        with zipfile.ZipFile(manifest_apk) as compiled:
            for entry in compiled.infolist():
                archive.writestr(entry.filename,compiled.read(entry.filename),compress_type=entry.compress_type)
        for path in assets.rglob('*'):
            if path.is_file():archive.write(path,'assets/'+path.relative_to(assets).as_posix(),compress_type=zipfile.ZIP_STORED if path.suffix.lower() in ('.ogg','.wav') else zipfile.ZIP_DEFLATED)
        archive.write(dex/'classes.dex','classes.dex')
        for abi in abis: archive.write(BUILD/abi/'libxuanyuan.so',f'lib/{abi}/libxuanyuan.so')
    aligned=BUILD/'aligned.apk'
    run([BT/('zipalign'+EXE),'-f','-P','16','4',raw,aligned])
    key=TC/'original-hd-debug.keystore'
    if not key.exists():
        run([JAVA/('keytool'+EXE),'-genkeypair','-keystore',key,'-storepass','android','-keypass','android','-alias','originalhd','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=Xuanyuan Original HD Development'])
    out=ROOT/'build/xuanyuan-original-hd-android-test.apk'
    out.parent.mkdir(parents=True,exist_ok=True)
    run([JAVA/('java'+EXE),'-jar',BT/'lib/apksigner.jar','sign','--ks',key,'--ks-key-alias','originalhd','--ks-pass','pass:android','--key-pass','pass:android','--out',out,aligned])
    run([JAVA/('java'+EXE),'-jar',BT/'lib/apksigner.jar','verify','--verbose',out])
    print(json.dumps({'apk':str(out),'sha256':hashlib.sha256(out.read_bytes()).hexdigest(),'abis':abis,'status':'test build, validation pending'},ensure_ascii=False),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--abis',nargs='+',default=['arm64-v8a','x86_64']);parser.add_argument('--package-only',action='store_true');parser.add_argument('--build-only',action='store_true');parser.add_argument('--compile-only',action='store_true');args=parser.parse_args()
    if not args.package_only:
        for abi in args.abis:
            if args.build_only:run([CMAKE,'--build',BUILD/abi,'-j','6'])
            else:native(abi)
    if args.compile_only:compile_java()
    else:package(args.abis)
