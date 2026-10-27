"""Fetch pinned official source dependencies for the STM32 diagnostic build.

No downloaded program is executed. Every input has an immutable commit URL and
a SHA-256 identity. Existing modified source files cause an error; they are not
silently replaced. TinyUSB is MIT; ARM/ST CMSIS headers are Apache-2.0.
"""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".tools/diagnostic-deps"
TINYUSB_COMMIT = "86ad6e56c1700e85f1c5678607a762cfe3aa2f47"
ARM_COMMIT = "2b7495b8535bdcb306dac29b9ded4cfb679d7e5c"
ST_COMMIT = "9192c7b9df75a142f2027ab266601fe061fc00b3"
TINYUSB_ROOT = DEPS / ("tinyusb-" + TINYUSB_COMMIT)

# Fixed identities obtained from these official immutable source URLs.
CORE_FILES = {
    "core_cm4.h": (120975, "f5b63d52dd1557b15ca414cb59c264f595a7b5669db2a5878dffe22f4caedc8c"),
    "cmsis_compiler.h": (9481, "b51963d271c1571ca3463654c76aa7ea50c4eac5d0a2710df10414ac64d9e0f8"),
    "cmsis_gcc.h": (63374, "43bfd1fe69fbbc2ca70aaf7d43cc3e20f16e0c7f66e1bdef9b51af6dc14b2617"),
    "cmsis_version.h": (1680, "184c19fd3ee73632edf35a0b4d49cd48be75fbf49e6ccb19d9db05fa83bea4b3"),
    "mpu_armv7.h": (11731, "29206b52ee02290ed6f5a5415ebd4187de802cf176d9b3cb844390d8e5571372"),
}
DEVICE_FILES = {
    "stm32f4xx.h": (11928, "a19edea6b2f4df2ab3c567782c4d313a52319c4303bd56ad7dd1d0e077014657"),
    "stm32f405xx.h": (1087221, "10c3b2dfc2b358d62a55668a7085ef07c44fc01abca8f2a08c2640a538040c7b"),
    "system_stm32f4xx.h": (2214, "02c067d5a135f540c03215dc834b1e60a97bbd4e55bfd3994b0c82a15ea3012a"),
}
USB_SOURCES = (
    "src/tusb.c", "src/common/tusb_fifo.c", "src/device/usbd.c",
    "src/device/usbd_control.c", "src/class/cdc/cdc_device.c",
    "src/class/midi/midi_device.c",
    "src/portable/synopsys/dwc2/dcd_dwc2.c",
    "src/portable/synopsys/dwc2/dwc2_common.c",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(relative, url, size, sha256):
    destination = DEPS / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if destination.stat().st_size != size or digest(destination) != sha256:
            raise ValueError(f"Modified dependency input: {destination}")
    else:
        print(f"Fetching official source: {relative}", flush=True)
        request = urllib.request.Request(url, headers={"User-Agent": "THY666-diagnostic-source"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(size + 1)
        if len(data) != size or hashlib.sha256(data).hexdigest() != sha256:
            raise ValueError(f"Dependency identity mismatch: {relative}")
        destination.write_bytes(data)
    return {"path": relative, "source_url": url, "bytes": size, "sha256": sha256}


def setup():
    records = []
    archive_sha = "3011c90c128988012b553e5d2f0a90bc0b64046591c964bc1f9f6659edcd7e4b"
    records.append(fetch("tinyusb-0.18.0.zip",
        f"https://codeload.github.com/hathach/tinyusb/zip/{TINYUSB_COMMIT}",
        4934193, archive_sha))
    # Extract/verify only source files and the upstream license, no example
    # executables, external submodules, build actions, or post-install scripts.
    with zipfile.ZipFile(DEPS / "tinyusb-0.18.0.zip") as archive:
        prefix = "tinyusb-" + TINYUSB_COMMIT + "/"
        for member in archive.infolist():
            if member.is_dir() or not member.filename.startswith(prefix):
                continue
            relative = member.filename[len(prefix):]
            if not (relative.startswith("src/") or relative == "LICENSE"):
                continue
            target = TINYUSB_ROOT / relative
            if not target.resolve().is_relative_to(TINYUSB_ROOT.resolve()):
                raise ValueError("Dependency archive path escape")
            data = archive.read(member)
            if target.exists() and target.read_bytes() != data:
                raise ValueError(f"Modified TinyUSB source: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.exists():
                target.write_bytes(data)
    for name, (size, sha) in CORE_FILES.items():
        records.append(fetch("cmsis-core/" + name,
            f"https://raw.githubusercontent.com/ARM-software/CMSIS_5/{ARM_COMMIT}/CMSIS/Core/Include/{name}", size, sha))
    records.append(fetch("cmsis-core/LICENSE.txt",
        f"https://raw.githubusercontent.com/ARM-software/CMSIS_5/{ARM_COMMIT}/LICENSE.txt",
        11357, "b40930bbcf80744c86c46a12bc9da056641d722716c378f5659b9e555ef833e1"))
    for name, (size, sha) in DEVICE_FILES.items():
        records.append(fetch("cmsis-device-f4/" + name,
            f"https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-f4/{ST_COMMIT}/Include/{name}", size, sha))
    records.append(fetch("cmsis-device-f4/LICENSE.md",
        f"https://raw.githubusercontent.com/STMicroelectronics/cmsis-device-f4/{ST_COMMIT}/LICENSE.md",
        9757, "dadb755f51d36614173b28c5790cb4a991e8f4cc822e5b634fd66a4f4145824d"))
    manifest = {
        "format": "thy666.diagnostic-dependencies", "version": 1,
        "tinyusb": {"version": "0.18.0", "commit": TINYUSB_COMMIT, "license": "MIT"},
        "cmsis_core": {"version": "5.9.0", "commit": ARM_COMMIT, "license": "Apache-2.0"},
        "cmsis_device_f4": {"commit": ST_COMMIT, "license": "Apache-2.0"},
        "inputs": records,
        "tinyusb_source_files": [{"path": str((TINYUSB_ROOT / file).relative_to(ROOT)).replace("\\", "/"),
            "sha256": digest(TINYUSB_ROOT / file)} for file in USB_SOURCES],
    }
    output = ROOT / ".tools/config/diagnostic-dependencies.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    value = setup()
    print(f"Verified TinyUSB 0.18.0 + ARM/ST CMSIS source under {DEPS}")
