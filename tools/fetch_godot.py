"""Download a pinned official engine, and Android/Web templates via ZIP ranges.

Toolchain files stay local to this workspace. This script never launches the
legacy game, changes PATH, or replaces a user's existing editor installation.
"""
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import struct
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".toolchain"
OUT.mkdir(exist_ok=True)
VERSION = "4.6.3-stable"


def download(url, path, digest=None):
    if path.exists() and (not digest or hashlib.sha256(path.read_bytes()).hexdigest() == digest):
        return
    partial = path.with_suffix(path.suffix + ".partial")
    with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as target:
        total = 0
        while chunk := response.read(1024 * 1024):
            target.write(chunk)
            total += len(chunk)
            if total % (16 * 1024 * 1024) == 0:
                print(f"{path.name}: {total // 1024 // 1024} MiB", flush=True)
    if digest:
        assert hashlib.sha256(partial.read_bytes()).hexdigest() == digest, "download checksum mismatch"
    partial.replace(path)
    print(f"Downloaded {path.name}", flush=True)


class RemoteZip(io.RawIOBase):
    def __init__(self, url, size):
        self.url, self.size, self.position = url, size, 0

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=0):
        self.position = offset if whence == 0 else (self.position + offset if whence == 1 else self.size + offset)
        return self.position

    def read(self, size=-1):
        if size < 0:
            size = self.size - self.position
        if size == 0:
            return b""
        start = self.position
        end = min(self.size - 1, start + size - 1)
        request = urllib.request.Request(self.url, headers={"Range": f"bytes={start}-{end}"})
        with urllib.request.urlopen(request, timeout=180) as response:
            assert response.status == 206, "server does not support ZIP range downloads"
            content_range = response.headers.get("Content-Range", "")
            assert content_range.startswith(f"bytes {start}-"), f"incorrect range {content_range}"
            result = response.read()
        assert len(result) == end - start + 1, "truncated range download"
        self.position += len(result)
        return result


def engine(asset):
    path = OUT / asset["name"]
    digest = (asset.get("digest") or "").removeprefix("sha256:") or None
    download(asset["browser_download_url"], path, digest)
    folder = OUT / "godot"
    folder.mkdir(exist_ok=True)
    with zipfile.ZipFile(path) as archive:
        archive.extractall(folder)


def templates(asset):
    folder = OUT / "templates"
    folder.mkdir(exist_ok=True)
    desired = ["android_debug.apk", "android_release.apk", "web_nothreads_debug.zip", "web_nothreads_release.zip", "version.txt"]
    with zipfile.ZipFile(RemoteZip(asset["browser_download_url"], asset["size"])) as archive:
        for name in desired:
            member = next((x for x in archive.namelist() if x.endswith("/" + name)), None)
            if member is None:
                print(f"Template unavailable: {name}", flush=True)
                continue
            if (folder / name).exists():
                continue
            print(f"Fetching template: {name}", flush=True)
            (folder / name).write_bytes(archive.read(member))
            print(f"Saved template: {name}", flush=True)


if __name__ == "__main__":
    with urllib.request.urlopen(f"https://api.github.com/repos/godotengine/godot-builds/releases/tags/{VERSION}", timeout=30) as response:
        release = json.load(response)
    assets = {asset["name"]: asset for asset in release["assets"]}
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        tasks = [pool.submit(engine, assets[f"Godot_v{VERSION}_win64.exe.zip"]),
                 pool.submit(templates, assets[f"Godot_v{VERSION}_export_templates.tpz"])]
        for task in tasks:
            task.result()
