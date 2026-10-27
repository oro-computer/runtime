#!/usr/bin/env python3
"""Stage a matching local Linux SDK/API pair; optionally exchange the generated installation atomically."""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import subprocess
import tempfile


def exchange_directories(first, second):
    """Preserve both directory trees and make the new pair visible in one Linux rename."""
    function = getattr(ctypes.CDLL(None, use_errno=True), "renameat2", None)
    if function is None:
        raise RuntimeError("Atomic directory exchange is unavailable; installation was not changed")
    function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    function.restype = ctypes.c_int
    if function(-100, os.fsencode(first), -100, os.fsencode(second), 2) != 0:
        failure = ctypes.get_errno()
        raise OSError(failure, "Atomic SDK exchange failed; both directory trees are retained")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def inherited_lease_fds():
    """Preserve source-build leases across Python's close-fds subprocess boundary."""
    descriptors = []
    for lease in json.loads(os.environ.get("ORO_SDK_LEASES", "[]")):
        actual = os.fstat(lease["fd"])
        expected = Path(lease["path"]).lstat()
        if not stat.S_ISREG(actual.st_mode) or (actual.st_dev, actual.st_ino) != (expected.st_dev, expected.st_ino):
            raise RuntimeError("Inherited SDK lease descriptor differs from its registered file")
        descriptors.append(lease["fd"])
    return tuple(descriptors)


def validate_catalog(sdk, api):
    """An optional packaged catalog must describe the selected compiled CLI."""
    catalog = sdk / "android-distribution.json"
    if not catalog.is_file():
        return
    result = subprocess.run(
        [str(sdk / "bin/oroc")],
        env={**os.environ, "ORO_HOME": str(sdk), "ORO_HOME_API": str(api),
             "ORO_INTERNAL_ANDROID_SDK_METADATA": "1", "ORO_LOG_JSON": "", "ORO_LOG_FILE": ""},
        capture_output=True, text=True, timeout=90, pass_fds=inherited_lease_fds(),
    )
    if result.returncode:
        raise RuntimeError("Cannot verify the packaged SDK catalog")
    compiled = json.loads(result.stdout)
    packaged = json.loads(catalog.read_text())
    if not isinstance(packaged, dict) or any(packaged.get(key) != value for key, value in compiled.items()):
        raise RuntimeError("Packaged android-distribution.json is stale; regenerate it from the selected CLI and API before publication")


def validate(root, sdk, api):
    result = subprocess.run(
        ["node", str(root / "bin/check-android-distribution.js"), str(sdk), str(api), str(root)],
        env={**os.environ, "ORO_HOME": str(sdk), "ORO_HOME_API": str(api)},
        capture_output=True, text=True, timeout=90, pass_fds=inherited_lease_fds(),
    )
    if result.returncode:
        raise RuntimeError("SDK/source validation failed:\n" + result.stdout + result.stderr)
    validate_catalog(sdk, api)
    return result.stdout.strip()


def stage(root, sdk, api):
    target = root / "build/npm/linux"
    if not target.is_dir() or target.is_symlink():
        raise RuntimeError("Expected the existing generated Linux installation at build/npm/linux")
    sdk_name = json.loads((sdk / "package.json").read_text())["name"]
    api_name = json.loads((api / "package.json").read_text())["name"]
    if sdk_name not in ("@oro-computer/runtime-linux-x64", "@oro-computer/runtime-linux-arm64") or api_name != "@oro-computer/runtime":
        raise RuntimeError("Expected a Linux platform SDK and its paired runtime API package")
    validate(root, sdk, api)
    output = Path(tempfile.mkdtemp(prefix=".linux-sdk-exchange-", dir=target.parent))
    packages = output / "packages/@oro-computer"
    packages.mkdir(parents=True)
    next_sdk = packages / sdk_name.rsplit("/", 1)[1]
    next_api = packages / "runtime"
    shutil.copytree(sdk, next_sdk, symlinks=False)
    shutil.copytree(api, next_api, symlinks=False)
    # A read-only qualification source must not prevent later development relinks.
    for package in (next_sdk, next_api):
        for item in [package, *package.rglob("*")]:
            item.chmod(stat.S_IMODE(item.stat().st_mode) | stat.S_IWUSR)
    # The legacy prefix and npm package layout both resolve to this one pair.
    for item in next_sdk.iterdir():
        if item.name in ("packages", "node_modules", ".oro.env", "api"):
            continue
        (output / item.name).symlink_to(item.relative_to(output), target_is_directory=item.is_dir())
    if not (output / "api").exists():
        (output / "api").symlink_to(next_api.relative_to(output), target_is_directory=True)
    (output / "node_modules").mkdir()
    (output / "node_modules/@oro-computer").symlink_to("../packages/@oro-computer", target_is_directory=True)
    # Preserve old prefix-only dependency resources without mixing its native archives or sources.
    if not (output / "uv").exists() and (target / "uv").is_dir():
        shutil.copytree(target / "uv", output / "uv", symlinks=True)
    configurations = []
    for original, destination in (
        (target / ".oro.env", output / ".oro.env"),
        (target / "packages" / sdk_name / ".oro.env", next_sdk / ".oro.env"),
        (target / "packages" / api_name / ".oro.env", next_api / ".oro.env"),
    ):
        if not original.is_file():
            continue
        directory_mode = stat.S_IMODE(destination.parent.stat().st_mode)
        destination.parent.chmod(directory_mode | stat.S_IWUSR | stat.S_IXUSR)
        shutil.copyfile(original, destination)
        destination.chmod(0o600)
        destination.parent.chmod(directory_mode)
        configurations.append({"path": str(original.relative_to(target)), "sha256": digest(original)})
    output.chmod(stat.S_IMODE(target.stat().st_mode))
    validate(root, next_sdk, next_api)
    return output, target, sdk_name, configurations


def activate(root, output, target, sdk_name, configurations):
    for item in configurations:
        if digest(target / item["path"]) != item["sha256"]:
            raise RuntimeError("Existing CLI configuration changed while staging; installation was not changed")
    exchange_directories(output, target)
    try:
        validate(root, target / "packages" / sdk_name, target / "packages/@oro-computer/runtime")
        for item in configurations:
            if digest(output / item["path"]) != item["sha256"]:
                raise RuntimeError("Previous CLI configuration changed during activation")
            if digest(target / item["path"]) != item["sha256"]:
                raise RuntimeError("CLI configuration preservation check failed")
    except BaseException:
        exchange_directories(output, target)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--api", type=Path, required=True)
    parser.add_argument("--activate-local", action="store_true", help="Atomically replace only this repository's generated Linux installation; retain its complete previous tree")
    args = parser.parse_args()
    if platform.system() != "Linux":
        parser.error("Local atomic activation requires Linux")
    root = Path(__file__).resolve().parent.parent
    output, target, sdk_name, configurations = stage(root, args.sdk.resolve(), args.api.resolve())
    if args.activate_local:
        activate(root, output, target, sdk_name, configurations)
    report = {
        "activated": args.activate_local,
        "installation": str(target),
        "previousInstallation" if args.activate_local else "stagedInstallation": str(output),
        "sdk": str(target / "packages" / sdk_name) if args.activate_local else str(output / "packages" / sdk_name),
        "api": str(target / "packages/@oro-computer/runtime") if args.activate_local else str(output / "packages/@oro-computer/runtime"),
        "preservedConfigurations": configurations,
        "previousTreeRetained": args.activate_local,
        "globalLinksEdited": False,
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
