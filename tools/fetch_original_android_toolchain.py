"""Download official Android native compiler tools, locally and checksum-checked."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.toolchain'


def fetch(package):
    name, url, checksum = package['name'], package['url'], package['sha1']
    archive = OUT / name
    if not archive.exists():
        with urllib.request.urlopen(url, timeout=90) as response, archive.with_suffix('.download').open('wb') as destination:
            last, written = time.monotonic(), 0
            while block := response.read(4 * 1024 * 1024):
                destination.write(block)
                written += len(block)
                if time.monotonic() - last > 10:
                    print(f'{name}: {written // 1048576} MiB', flush=True)
                    last = time.monotonic()
        archive.with_suffix('.download').rename(archive)
    digest = hashlib.sha1()
    with archive.open('rb') as source:
        while block := source.read(8 * 1024 * 1024):
            digest.update(block)
    assert digest.hexdigest() == checksum, f'Official checksum mismatch: {name}'
    target = OUT / package['destination']
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        for item in bundle.infolist():
            relative = Path(item.filename)
            if package.get('strip_root'):
                relative = Path(*relative.parts[1:])
            final = (target / relative).resolve()
            assert final.is_relative_to(target.resolve())
            if item.is_dir():
                final.mkdir(parents=True, exist_ok=True)
            else:
                final.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(item) as source, final.open('wb') as output:
                    output.write(source.read())
    print(f'Ready: {target}', flush=True)
    return package


def main():
    with urllib.request.urlopen('https://dl.google.com/android/repository/repository2-3.xml', timeout=60) as response:
        root = ET.fromstring(response.read())
    desired = {'ndk;30.0.16248370': 'android-ndk', 'cmake;3.22.1': 'android-cmake'}
    packages = []
    for identifier, destination in desired.items():
        remote = next(node for node in root if node.tag.endswith('remotePackage') and node.attrib['path'] == identifier)
        archive = next(a for a in remote.findall('.//archive') if a.findtext('host-os') == 'windows')
        complete = archive.find('complete')
        name = complete.findtext('url')
        packages.append({'identifier': identifier, 'name': name, 'url': 'https://dl.google.com/android/repository/' + name,
                         'sha1': complete.findtext('checksum'), 'destination': destination, 'strip_root': identifier.startswith('ndk;')})
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(fetch, packages))
    (OUT / 'original-android-toolchain.json').write_text(json.dumps(results, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
