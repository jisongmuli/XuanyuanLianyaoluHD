"""Check the recovered release sources and integrated public asset checksums."""
from pathlib import Path
import hashlib,json,re,xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
def main():
    android=ROOT/'native_hd/android'
    ns='{http://schemas.android.com/apk/res/android}'
    manifest=ET.parse(android/'AndroidManifest.xml').getroot()
    assert manifest.attrib[ns+'versionName']=='0.7'
    assert manifest.attrib[ns+'versionCode']=='7'
    assert not manifest.findall('uses-permission'), 'Unexpected Android permissions'
    java=(android/'java/org/xuanyuan/originalhd/MainActivity.java').read_text(encoding='utf8')
    cpp=(android/'runtime.cpp').read_text(encoding='utf8')
    methods=re.findall(r'static native \w+ (\w+)\(',java)
    for name in methods:
        assert 'Java_org_xuanyuan_originalhd_MainActivity_'+name in cpp, name
    report=json.loads((ROOT/'docs/recovery-v0.7.json').read_text(encoding='utf8'))
    for name,digest in report['source_files'].items():
        assert hashlib.sha256((ROOT/name).read_bytes().replace(b'\r\n',b'\n')).hexdigest()==digest, name
    audio=json.loads((ROOT/'native_hd/assets/audio/manifest.json').read_text(encoding='utf8'))
    assert len(audio['music'])==7 and len(audio['effects'])==12
    for asset in audio['music']+audio['effects']:
        file=ROOT/'native_hd/assets'/asset['file']
        assert file.resolve().is_relative_to((ROOT/'native_hd/assets').resolve())
        assert hashlib.sha256(file.read_bytes()).hexdigest()==asset['sha256'], asset['file']
        data=file.read_bytes()
        assert data[:4]==(b'OggS' if file.suffix=='.ogg' else b'RIFF')
    presentation=json.loads((ROOT/'native_hd/assets/presentation/manifest.json').read_text(encoding='utf8'))
    assert len(presentation['files'])==7
    for asset in presentation['files']:
        file=ROOT/'native_hd/assets/presentation'/asset['file']
        assert file.resolve().is_relative_to((ROOT/'native_hd/assets/presentation').resolve())
        assert hashlib.sha256(file.read_bytes()).hexdigest()==asset['sha256'], asset['file']
    assert set(re.findall(r'command-(\d+)',java))=={str(ord(c)) for c in '战技物收商破'}
    print(f'v0.7 source checks passed: {len(methods)} JNI methods, 19 audio files and 7 presentation assets.')

if __name__=='__main__':main()
