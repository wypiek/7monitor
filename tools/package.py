#!/usr/bin/env python3
"""Creates release archives from build/x86 and build/x64.

  dist/sevenmon-<version>-win-x86.zip
  dist/sevenmon-<version>-win-x64.zip
  dist/SHA256SUMS

Each archive contains sevenmon.exe, README.md, LICENSE and docs/. Run after building
both Release configurations (see docs/building.md).
"""
import hashlib
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def version():
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r"project\(SevenMonitor VERSION ([0-9.]+)", f.read())
    return m.group(1)


def main():
    ver = version()
    dist = os.path.join(ROOT, "dist")
    os.makedirs(dist, exist_ok=True)
    sums = []
    for arch in ("x86", "x64"):
        exe = os.path.join(ROOT, "build", arch, "sevenmon.exe")
        if not os.path.isfile(exe):
            print("missing %s (build the %s preset first)" % (exe, arch))
            return 1
        name = "sevenmon-%s-win-%s.zip" % (ver, arch)
        path = os.path.join(dist, name)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(exe, "sevenmon.exe")
            z.write(os.path.join(ROOT, "README.md"), "README.md")
            z.write(os.path.join(ROOT, "LICENSE"), "LICENSE")
            for doc in sorted(os.listdir(os.path.join(ROOT, "docs"))):
                z.write(os.path.join(ROOT, "docs", doc), "docs/" + doc)
        with open(path, "rb") as f:
            sums.append("%s  %s" % (hashlib.sha256(f.read()).hexdigest(), name))
        print("wrote", path)
    with open(os.path.join(dist, "SHA256SUMS"), "w", encoding="ascii") as f:
        f.write("\n".join(sums) + "\n")
    print("\n".join(sums))
    return 0


if __name__ == "__main__":
    sys.exit(main())
