#!/usr/bin/env python3
"""Copy a Linux checkout SDK/API generation and publish one stable active pointer."""

import argparse
try:
    import fcntl
except ImportError:
    fcntl = None
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import uuid


OMIT = {"node_modules", ".oro.env"}
LEGACY_LAUNCHER = r'''#!/usr/bin/env node
const fs = require('node:fs')
const path = require('node:path')
const { pathToFileURL } = require('node:url')
async function run () {
  const home = path.dirname(__dirname)
  const generation = fs.realpathSync(path.join(home, 'current'))
  const receipt = JSON.parse(fs.readFileSync(path.join(generation, 'generation.json'), 'utf8'))
  if (receipt.schema !== 1) throw new Error('Unsupported installed SDK generation')
  const api = path.join(generation, 'packages', '@oro-computer', 'runtime')
  const sdk = path.join(generation, 'packages', ...receipt.sdkPackage.split('/'))
  process.env.ORO_HOME ||= sdk
  process.env.ORO_HOME_API ||= api
  const legacy = path.join(home, 'configuration', 'sdk.env')
  if (!process.env.ORO_ENV_FILE && !process.env.ORO_CONFIG_HOME && fs.existsSync(legacy)) process.env.ORO_ENV_FILE = legacy
  await (await import(pathToFileURL(path.join(api, 'bin', 'oroc.js')).href)).run()
}
run().catch(error => { console.error(error.message); process.exitCode = 1 })
'''


LAUNCHER = r'''#!/usr/bin/env node
// Oro managed source SDK launcher, lease protocol 1.
const path = require('node:path')
const { spawn } = require('node:child_process')
const home = path.dirname(__dirname)
const child = spawn('python3', [path.join(__dirname, 'sdk-cache.py'), 'run',
  '--home', home, '--node', process.execPath, '--entry', path.join(__dirname, 'sdk-entry.cjs'),
  '--preload', path.join(__dirname, 'sdk-lease.cjs'), '--', ...process.argv.slice(2)], { stdio: 'inherit' })
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => child.kill(signal))
child.on('error', error => { console.error(error.message); process.exitCode = 1 })
child.on('exit', (code, signal) => { process.exitCode = code ?? (signal === 'SIGINT' ? 130 : 143) })
'''

_spec = importlib.util.spec_from_file_location("sdk_cache", Path(__file__).with_name("sdk-cache.py"))
cache = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cache)


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def inventory(directory):
    """Follow every payload link, rejecting cycles and special files before copying."""
    result = {}

    def walk(parent, relative, ancestors):
        resolved = parent.resolve(strict=True)
        if resolved in ancestors:
            raise RuntimeError("SDK payload contains a directory link cycle")
        for item in sorted(parent.iterdir()):
            if item.name in OMIT:
                continue
            name = relative / item.name
            mode = item.stat().st_mode
            if stat.S_ISDIR(mode):
                walk(item, name, ancestors | {resolved})
            elif stat.S_ISREG(mode):
                result[name.as_posix()] = {"sha256": digest(item), "size": item.stat().st_size,
                                          "executable": bool(mode & 0o111)}
            else:
                raise RuntimeError("SDK payload contains an unsupported special file")
            if len(result) > 100000:
                raise RuntimeError("SDK payload exceeds the file limit")

    walk(directory, Path(), set())
    return result


def validate(root, sdk, api):
    spec = importlib.util.spec_from_file_location("activation", root / "bin/activate-runtime-sdk.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # Desktop-only packages still expose the compiled catalog; only the native
    # Android archive inventory is optional when that target was excluded.
    if any((sdk / "lib").glob("*-android")):
        return module.validate(root, sdk, api)
    program = """
import assert from 'node:assert/strict'
import androidSdk from './src/cli/android-sdk.cjs'
import { validateDistribution, cliSourceFingerprint } from './bin/check-android-distribution.js'
const [sdk, api, root] = process.argv.slice(1)
const metadata = await androidSdk.readAndroidSdk(sdk + '/bin/oroc', { env: { ...process.env, ORO_HOME: sdk, ORO_HOME_API: api } })
validateDistribution(metadata, root)
assert.equal(cliSourceFingerprint(sdk, metadata.cliSourceFingerprintAlgorithm ?? 1), metadata.cliSourceFingerprint)
console.log('Desktop-only compiled CLI, sources and paired APIs verified')
"""
    result = subprocess.run(["node", "--input-type=module", "-e", program, str(sdk), str(api), str(root)],
                            cwd=root, capture_output=True, text=True, timeout=90,
                            pass_fds=module.inherited_lease_fds())
    if result.returncode:
        raise RuntimeError("SDK/source validation failed:\n" + result.stdout + result.stderr)
    module.validate_catalog(sdk, api)
    return result.stdout.strip()


def verify_generation(generation, require_readonly=True):
    receipt = json.loads((generation / "generation.json").read_text())
    if receipt.get("schema") != 1 or receipt.get("sdkPackage") not in (
            "@oro-computer/runtime-linux-x64", "@oro-computer/runtime-linux-arm64") or set(receipt.get("files", {})) != {
                receipt["sdkPackage"], "@oro-computer/runtime"}:
        raise RuntimeError("Invalid SDK generation receipt")
    if require_readonly:
        for item in [generation, *generation.rglob("*")]:
            if not item.is_symlink() and item.stat().st_mode & 0o222:
                raise RuntimeError("Published SDK generation is writable")
    module_link = generation / "node_modules/@oro-computer"
    if not module_link.is_symlink() or module_link.resolve() != (generation / "packages/@oro-computer").resolve():
        raise RuntimeError("SDK package resolution escapes its generation")
    for package, expected in receipt["files"].items():
        directory = generation / "packages" / package
        if directory.resolve() != directory.absolute() or any(item.is_symlink() for item in directory.rglob("*")):
            raise RuntimeError("Published SDK payload contains a link")
        if inventory(directory) != expected:
            raise RuntimeError("Published SDK payload differs from its generation receipt")
    return receipt


def preserve_configurations(home, sdk, api, legacy_prefix):
    output = []
    directory = home / "configuration"
    directory.mkdir(mode=0o700, exist_ok=True)
    directory.chmod(0o700)
    sources = {"sdk": sdk / ".oro.env", "api": api / ".oro.env"}
    if legacy_prefix:
        sources["prefix"] = legacy_prefix / ".oro.env"
    for name, original in sources.items():
        if not original.is_file():
            continue
        destination = directory / (name + ".env")
        # Active configuration belongs to the user. A new publication never
        # overwrites it with an older build-staging copy.
        if destination.is_symlink():
            raise RuntimeError("Managed SDK configuration must not be a link")
        if not destination.exists():
            before = digest(original)
            temporary = directory / (".configuration-" + uuid.uuid4().hex)
            with temporary.open("xb") as target:
                temporary.chmod(0o600)
                with original.open("rb") as source:
                    shutil.copyfileobj(source, target)
                target.flush()
                os.fsync(target.fileno())
            if digest(temporary) != before or digest(original) != before:
                raise RuntimeError("Legacy configuration changed during migration; active generation was not changed")
            try:
                os.link(temporary, destination)
            except FileExistsError:
                if not destination.is_file() or destination.is_symlink():
                    raise RuntimeError("Concurrent SDK configuration is invalid")
            temporary.unlink()
        output.append({"source": str(original), "preservedAt": str(destination),
                       "sourceSha256": digest(original), "preservedSha256": digest(destination)})
    return output


def prepare(root, sdk, api, home, legacy_prefix=None):
    root, sdk, api, home = [path.resolve() for path in (root, sdk, api, home)]
    if home == root or home.is_relative_to(root / "build") or home.is_relative_to(root / "tmp"):
        raise RuntimeError("Stable installation must be outside disposable build and tmp outputs")
    if home.is_relative_to(sdk) or home.is_relative_to(api):
        raise RuntimeError("Stable installation cannot be inside an input package")
    sdk_package = json.loads((sdk / "package.json").read_text())
    api_package = json.loads((api / "package.json").read_text())
    if sdk_package["name"] not in ("@oro-computer/runtime-linux-x64", "@oro-computer/runtime-linux-arm64") or api_package["name"] != "@oro-computer/runtime":
        raise RuntimeError("Expected a Linux SDK and runtime API pair")
    if sdk_package["version"] != api_package["version"]:
        raise RuntimeError("SDK and API versions differ")
    validation = validate(root, sdk, api)
    sources = {sdk_package["name"]: sdk, api_package["name"]: api}
    before = {name: inventory(source) for name, source in sources.items()}
    content = "sdk-" + hashlib.sha256(json.dumps({"version": sdk_package["version"],
        "sdkPackage": sdk_package["name"], "files": before}, sort_keys=True).encode()).hexdigest()
    cache.collect(home)
    generation, work_fd, reused = cache.begin_work(home, content,
        sum(item["size"] for files in before.values() for item in files.values()))
    retained_fd = None
    try:
        if reused:
            verify_generation(generation)
            validate(root, generation / "packages" / sdk_package["name"],
                     generation / "packages" / api_package["name"])
            return generation, sdk_package["name"], work_fd, True
        for name, source in sources.items():
            destination = generation / "packages" / name
            shutil.copytree(source, destination, symlinks=False, ignore=lambda parent, names: OMIT.intersection(names))
            if inventory(destination) != before[name] or inventory(source) != before[name]:
                raise RuntimeError("SDK input changed while copying; previous active generation retained")
        validate(root, generation / "packages" / sdk_package["name"], generation / "packages" / api_package["name"])
        (generation / "node_modules").mkdir()
        (generation / "node_modules/@oro-computer").symlink_to("../packages/@oro-computer", target_is_directory=True)
        receipt = {"schema": 1, "version": sdk_package["version"], "sdkPackage": sdk_package["name"],
                   "validation": validation, "files": before}
        (generation / "generation.json").write_text(json.dumps(receipt, indent=2) + "\n")
        verify_generation(generation, require_readonly=False)
        for item in sorted(generation.rglob("*"), key=lambda item: len(item.parts), reverse=True):
            if not item.is_symlink():
                item.chmod(stat.S_IMODE(item.stat().st_mode) & ~0o222)
                if item.is_file():
                    with item.open("rb") as payload:
                        os.fsync(payload.fileno())
                else:
                    cache.sync_directory(item)
        generation.chmod(0o555)
        cache.sync_directory(generation)
        generation, retained_fd = cache.finish_work(home, generation, content)
        verify_generation(generation)
        os.close(work_fd)
        return generation, sdk_package["name"], retained_fd, False
    except BaseException:
        if retained_fd is not None:
            os.close(retained_fd)
        os.close(work_fd)
        raise


def temporary_link(parent, destination):
    pointer = parent / (".sdk-pointer-" + uuid.uuid4().hex)
    pointer.symlink_to(destination)
    return pointer


def install_launcher(home):
    # A sealed installation may be executed by readers with no cache write
    # permission. Lock files must exist before the new launcher is exposed.
    value = cache.state(home)
    publication = os.open(home / ".publication.lock", os.O_RDONLY | os.O_NOFOLLOW)
    try:
        os.fchmod(publication, os.fstat(publication).st_mode | 0o444)
    finally:
        os.close(publication)
    for name in value["managed"]:
        descriptor = os.open(home / "leases" / (name + ".lock"), os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o644)
        os.fchmod(descriptor, os.fstat(descriptor).st_mode | 0o444)
        os.close(descriptor)
    directory = home / "bin"
    directory.mkdir(exist_ok=True)
    launcher = directory / "oroc.cjs"
    if launcher.is_symlink() or (launcher.exists() and launcher.read_text() not in [LEGACY_LAUNCHER, LAUNCHER]):
        raise RuntimeError("Existing stable launcher differs; installation was not changed")
    # Protocol-1 helpers remain compatible with existing dispatchers. Each file
    # is installed atomically before the dispatcher is enabled.
    files = {name: Path(__file__).with_name(name).read_text()
             for name in ["sdk-cache.py", "sdk-entry.cjs", "sdk-lease.cjs"]}
    files["oroc.cjs"] = LAUNCHER
    for name, content in files.items():
        destination = directory / name
        if destination.is_symlink():
            raise RuntimeError("Managed SDK helper is a link")
        if destination.exists() and destination.read_text() == content:
            continue
        temporary = directory / (".launcher-" + uuid.uuid4().hex)
        with temporary.open("x") as output:
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        temporary.chmod(0o555)
        os.replace(temporary, destination)
    return launcher


def activate(home, generation):
    home, generation = home.resolve(), generation.resolve()
    if not generation.is_relative_to(home / "generations"):
        raise RuntimeError("Generation does not belong to this installation")
    with cache.locked(home):
        value = cache.state(home)
        verify_generation(generation)
        launcher = install_launcher(home)
        current = home / "current"
        if current.is_symlink() and not current.exists():
            raise RuntimeError("Active SDK pointer is broken; previous installation was not changed")
        if current.exists() and not current.is_symlink():
            raise RuntimeError("Active SDK pointer must be a managed symlink")
        previous = current.resolve(strict=True) if current.exists() else None
        value["pending"] = {"old": previous.name if previous else None, "new": generation.name,
                            "rollback": value.get("previous")}
        cache.save(home, value)
        pointer = temporary_link(home, generation.relative_to(home))
        os.replace(pointer, current)
        cache.sync_directory(home)
        try:
            verify_generation(generation)
        except BaseException:
            if current.resolve() != generation:
                raise RuntimeError("Active SDK pointer changed outside the publication lock")
            if previous is not None:
                os.replace(temporary_link(home, previous.relative_to(home)), current)
            else:
                os.replace(current, home / ("rejected-pointer-" + uuid.uuid4().hex))
            value["pending"] = None
            cache.save(home, value)
            raise
        if previous and previous != generation:
            value["previous"] = previous.name
        value["pending"] = None
        value["candidate"] = None
        cache.save(home, value)
    return launcher, previous


def link_global(home, prefix, sdk_name):
    """One-time migration uses rename, never npm unlink/relink of a live command."""
    prefix = prefix.resolve()
    mappings = [(prefix / "bin/oroc", home / "bin/oroc.cjs"),
                (prefix / "lib/node_modules" / sdk_name, home / "current/packages" / sdk_name),
                (prefix / "lib/node_modules/@oro-computer/runtime", home / "current/packages/@oro-computer/runtime")]
    for target, source in mappings:
        if not source.exists():
            raise RuntimeError("Stable SDK publication must finish before linking commands")
        if target.exists() and not target.is_symlink():
            raise RuntimeError("Refusing to replace an unmanaged global file or directory")
    changes = [(target, source) for target, source in mappings
               if not target.is_symlink() or os.path.normpath(target.parent / os.readlink(target)) != str(source)]
    if not changes:
        return None
    history = home / "global-link-history" / uuid.uuid4().hex
    history.mkdir(parents=True)
    previous_links = []
    for target, source in changes:
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_symlink():
            previous_links.append({"path": str(target), "linkText": os.readlink(target)})
        (history / "links.json").write_text(json.dumps(previous_links, indent=2) + "\n")
        os.replace(temporary_link(target.parent, source), target)
    return str(history)


def publish(root, sdk, api, home, active=False, global_prefix=None, legacy_prefix=None):
    if global_prefix and not active:
        raise RuntimeError("Global linking requires activation of a validated generation")
    home = home.resolve()
    source_fds = cache.source_leases([sdk, api])
    generation_fd = None
    try:
        generation, sdk_name, generation_fd, reused = prepare(root, sdk, api, home, legacy_prefix)
        configurations = preserve_configurations(home, sdk, api, legacy_prefix)
        launcher, previous = activate(home, generation) if active else (None, None)
        links = link_global(home, global_prefix, sdk_name) if global_prefix else None
        report = {"activated": active, "installation": str(home), "generation": str(generation),
                  "previousGeneration": str(previous) if previous else None, "previousTreeRetained": True,
                  "reusedGeneration": reused,
                  "sdk": str(generation / "packages" / sdk_name), "api": str(generation / "packages/@oro-computer/runtime"),
                  "launcher": str(launcher) if launcher else None, "globalLinksEdited": links is not None,
                  "globalLinkHistory": links, "preservedConfigurations": configurations,
                  "generationReceiptSha256": digest(generation / "generation.json"), "recursivePayloadLinks": 0}
    finally:
        for descriptor in source_fds + ([generation_fd] if generation_fd is not None else []):
            os.close(descriptor)
        try:
            collection = cache.collect(home) if (home / "retention.json").exists() else {"skipped": "no managed cache"}
        except (OSError, ValueError, RuntimeError) as error:
            collection = {"errors": [str(error)]}
            print("SDK cleanup remains pending: " + str(error), file=sys.stderr)
    report["collection"] = collection
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gc", action="store_true", help="Collect unused managed source SDK generations")
    parser.add_argument("--dry-run", action="store_true", help="Report collection without deleting files")
    parser.add_argument("--legacy-idle", action="store_true", help="Confirm pre-lease SDK commands have stopped")
    parser.add_argument("--sdk", type=Path)
    parser.add_argument("--api", type=Path)
    parser.add_argument("--stable-home", type=Path)
    parser.add_argument("--activate-local", action="store_true")
    parser.add_argument("--global-prefix", type=Path)
    parser.add_argument("--legacy-prefix", type=Path)
    parser.add_argument("--link-current", action="store_true", help="Atomically link the already prepared active generation into --global-prefix")
    args = parser.parse_args()
    if os.name != "posix" or not hasattr(os, "uname") or os.uname().sysname != "Linux":
        parser.error("Stable checkout SDK publication currently requires Linux")
    root = Path(__file__).resolve().parent.parent
    home = args.stable_home or root / ".local/share/oro-sdk/linux"
    if args.gc:
        if args.sdk or args.api or args.link_current or args.activate_local or args.global_prefix:
            parser.error("--gc cannot be combined with publication or linking")
        if not args.dry_run:
            with cache.locked(home):
                if cache.current_name(home):
                    install_launcher(home)
        report = cache.collect(home, args.dry_run, args.legacy_idle)
        print(json.dumps(report, indent=2))
        if report.get("errors"):
            raise SystemExit(1)
        return
    if args.dry_run or args.legacy_idle:
        parser.error("--dry-run and --legacy-idle require --gc")
    if args.link_current:
        if not args.global_prefix or args.sdk or args.api or args.activate_local:
            parser.error("--link-current requires --global-prefix and does not accept package or activation arguments")
        home = home.resolve()
        with (home / ".publication.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            generation = (home / "current").resolve(strict=True)
            if not generation.is_relative_to(home / "generations"):
                raise RuntimeError("Active generation does not belong to this installation")
            receipt = verify_generation(generation)
            install_launcher(home)
            history = link_global(home, args.global_prefix, receipt["sdkPackage"])
            report = {"installation": str(home), "generation": str(generation),
                      "generationReceiptSha256": digest(generation / "generation.json"),
                      "globalLinkHistory": history, "globalLinksEdited": history is not None}
    else:
        if not args.sdk or not args.api:
            parser.error("--sdk and --api are required when preparing a generation")
        report = publish(root, args.sdk.resolve(), args.api.resolve(), home, args.activate_local,
                         args.global_prefix, args.legacy_prefix)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
