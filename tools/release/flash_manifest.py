# SPDX-License-Identifier: MIT
"""The files a first flash writes, named by a manifest a web flasher reads.

    python3 flash_manifest.py <build dir> <out dir> <version>

Copies what flasher_args.json of an ESP-IDF build lists - the bootloader, the
partition table, the OTA data, the firmware and the applications' file system -
into <out dir>, and writes manifest.json beside them in the form of ESP Web
Tools: the chip and, for every file, where it goes.
"""

import json
import shutil
import sys
from pathlib import Path


def main(build: Path, out: Path, version: str) -> None:
    args = json.loads((build / "flasher_args.json").read_text())
    out.mkdir(parents=True, exist_ok=True)
    parts = []
    for offset, path in sorted(args["flash_files"].items(), key=lambda item: int(item[0], 16)):
        name = Path(path).name
        shutil.copyfile(build / path, out / name)
        parts.append({"path": name, "offset": int(offset, 16)})
    settings = args["flash_settings"]
    manifest = {
        "name": "perINKpheral",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-C6", "parts": parts}],
        "flash": settings,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
