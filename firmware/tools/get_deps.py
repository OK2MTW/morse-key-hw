#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Downloads the pinned third-party sources into firmware/third_party/ (not stored in git).

    python firmware/tools/get_deps.py

Every dependency is a GitHub archive of an exact commit/tag, so builds are reproducible.
"""
import io
import os
import shutil
import sys
import tarfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.normpath(os.path.join(HERE, "..", "third_party"))

# name: (archive url, path inside the archive to keep (None = all), licence)
DEPS = {
    "tinyusb": ("https://github.com/hathach/tinyusb/archive/ea1d867ced5259d49679f481e5d4e21a36dc11b0.tar.gz",
                "src", "MIT"),
    "cmsis_device_f4": ("https://github.com/STMicroelectronics/cmsis_device_f4/archive/2615e866fa48fe1ff1af9e31c348813f2b19e7ec.tar.gz",
                        None, "Apache-2.0"),
    "stm32f4xx_hal_driver": ("https://github.com/STMicroelectronics/stm32f4xx_hal_driver/archive/04e99fbdabd00ab8f370f377c66b0a4570365b58.tar.gz",
                             None, "BSD-3-Clause"),
    # CMSIS core headers (core_cm4.h ...), the subset vendored in TinyUF2 0.36.0
    "cmsis_core": ("https://github.com/adafruit/tinyuf2/archive/refs/tags/0.36.0.tar.gz",
                   "lib/CMSIS_5", "Apache-2.0"),
}


def fetch(name, url, keep):
    target = os.path.join(DEST, name)
    if os.path.isdir(target) and os.listdir(target):
        print(f"{name}: present")
        return
    print(f"{name}: downloading {url}")
    data = urllib.request.urlopen(url).read()
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
        top = tf.getmembers()[0].name.split("/")[0]
        prefix = top + "/" + (keep + "/" if keep else "")
        members = []
        for m in tf.getmembers():
            if not m.name.startswith(prefix) or not (m.isfile() or m.isdir()):
                continue  # skip symlinks and anything outside the kept path
            rel = m.name[len(prefix):]
            if not rel or rel.startswith("/") or ".." in rel.split("/"):
                continue
            m.name = rel
            members.append(m)
        os.makedirs(target, exist_ok=True)
        tf.extractall(target, members=members, filter="data")
    print(f"{name}: {len(members)} entries")


def main():
    os.makedirs(DEST, exist_ok=True)
    for name, (url, keep, _lic) in DEPS.items():
        try:
            fetch(name, url, keep)
        except Exception as e:  # noqa: BLE001
            shutil.rmtree(os.path.join(DEST, name), ignore_errors=True)
            sys.exit(f"{name}: failed: {e}")


if __name__ == "__main__":
    main()
