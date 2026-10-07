"""Fetch official prebuilt export tools into the workspace, without installers."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

from fetch_godot import download, OUT


def unpack_sdk(package, archive):
    name, url, checksum = archive
    path = OUT / name
    download(url, path)
    assert hashlib.sha1(path.read_bytes()).hexdigest() == checksum, f"SDK checksum mismatch: {name}"
    target = OUT / "android-sdk"
    target.mkdir(exist_ok=True)
    with zipfile.ZipFile(path) as bundle:
        first = bundle.namelist()[0].split("/")[0]
        staging = OUT / ("unpack-" + package.replace(";", "-"))
        staging.mkdir(exist_ok=True)
        bundle.extractall(staging)
    if package == "platform-tools":
        destination = target / "platform-tools"
    elif package.startswith("build-tools;"):
        destination = target / "build-tools" / package.split(";")[1]
    else:
        destination = target / "platforms" / package.split(";")[1]
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(staging / first, destination, dirs_exist_ok=True)
    print(f"SDK ready: {package}", flush=True)


def jdk():
    url = "https://api.github.com/repos/adoptium/temurin17-binaries/releases/latest"
    with urllib.request.urlopen(url, timeout=60) as response:
        release = json.load(response)
    asset = next(a for a in release["assets"] if a["name"].startswith("OpenJDK17U-jdk_x64_windows_hotspot_") and a["name"].endswith(".zip"))
    checksum_asset = next(a for a in release["assets"] if a["name"] == asset["name"] + ".sha256.txt")
    with urllib.request.urlopen(checksum_asset["browser_download_url"], timeout=60) as response:
        checksum = response.read().decode().split()[0]
    package = {"name": asset["name"], "link": asset["browser_download_url"], "checksum": checksum}
    path = OUT / package["name"]
    download(package["link"], path, package["checksum"])
    target = OUT / "java"
    target.mkdir(exist_ok=True)
    with zipfile.ZipFile(path) as bundle:
        bundle.extractall(target)
    print("OpenJDK 17 ready", flush=True)


if __name__ == "__main__":
    with urllib.request.urlopen("https://dl.google.com/android/repository/repository2-3.xml", timeout=60) as response:
        repository = ET.fromstring(response.read())
    # Godot 4.6.3's prebuilt Android template targets API 36.
    desired = ["platform-tools", "build-tools;36.0.0", "platforms;android-36"]
    archives = []
    for package in desired:
        remote = next(node for node in repository if node.tag.endswith("remotePackage") and node.attrib["path"] == package)
        selected = None
        for archive in remote.findall(".//archive"):
            host = archive.findtext("host-os")
            if host is None or host == "windows":
                complete = archive.find("complete")
                name = complete.findtext("url")
                selected = (name, "https://dl.google.com/android/repository/" + name, complete.findtext("checksum"))
                if host == "windows":
                    break
        assert selected
        archives.append((package, selected))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        tasks = [pool.submit(unpack_sdk, package, archive) for package, archive in archives]
        tasks.append(pool.submit(jdk))
        for task in tasks:
            task.result()
