#!/usr/bin/env python3
"""Linux source SDK leases and collection. Release packaging does not call this module."""

import argparse
from contextlib import contextmanager, ExitStack
import fcntl
import json
import os
from pathlib import Path
import re
import signal
import stat
import subprocess
import sys
import time
import uuid

SCHEMA = 1
DEFAULT_LIMIT = 20 * 1024**3
RESERVE = 1024**3
NAME = re.compile(r"sdk-[a-zA-Z0-9_-]+\Z")


def sync_directory(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def atomic_json(path, value):
    temporary = path.parent / (".json-" + uuid.uuid4().hex)
    with temporary.open("x") as output:
        temporary.chmod(0o600)
        json.dump(value, output, sort_keys=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
    sync_directory(path.parent)


def boot_id():
    try:
        return Path("/proc/sys/kernel/random/boot_id").read_text().strip()
    except OSError:
        return "unknown"


def ensure_home(home):
    home = home.resolve()
    home.mkdir(parents=True, exist_ok=True)
    for name in ["leases", "work", "trash", "generations"]:
        directory = home / name
        if directory.is_symlink():
            raise RuntimeError("Managed SDK cache directory is a link")
        directory.mkdir(mode=0o700, exist_ok=True)
    return home


@contextmanager
def locked(home, exclusive=True, nonblocking=False):
    if exclusive:
        ensure_home(home)
    descriptor = os.open(home / ".publication.lock",
                         (os.O_RDWR | os.O_CREAT if exclusive else os.O_RDONLY) | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(descriptor, (fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH) |
                    (fcntl.LOCK_NB if nonblocking else 0))
        yield
    finally:
        os.close(descriptor)


def receipt(path):
    if path.is_symlink() or not path.is_dir():
        raise RuntimeError("SDK generation is not a regular directory")
    if (path / "generation.json").is_symlink():
        raise RuntimeError("SDK generation metadata is a link")
    info = json.loads((path / "generation.json").read_text())
    if info.get("schema") != 1 or info.get("sdkPackage") not in (
            "@oro-computer/runtime-linux-x64", "@oro-computer/runtime-linux-arm64"):
        raise RuntimeError("Invalid SDK generation metadata")
    return info


def current_name(home):
    current = home / "current"
    if not current.is_symlink():
        if current.exists():
            raise RuntimeError("Active SDK pointer is invalid")
        return None
    target = current.resolve(strict=True)
    if target.parent != (home / "generations").resolve() or not NAME.fullmatch(target.name):
        raise RuntimeError("Active SDK escapes its managed generations")
    receipt(target)
    return target.name


def state(home):
    filename = home / "retention.json"
    if filename.exists():
        value = json.loads(filename.read_text())
        if filename.is_symlink() or value.get("schema") != SCHEMA:
            raise RuntimeError("Unknown SDK cache metadata version")
        validate_state(home, value)
        pending = value.get("pending")
        if pending:
            active = current_name(home)
            if active == pending["new"]:
                value["previous"] = pending["old"] or pending["rollback"]
            elif active != pending["old"]:
                raise RuntimeError("Interrupted SDK publication has an unexpected active pointer")
            value["candidate"] = pending["new"] if active != pending["new"] else None
            value["pending"] = None
            save(home, value)
        return value
    entries = {}
    generations = home / "generations"
    if generations.is_symlink():
        raise RuntimeError("SDK generations directory is a link")
    if generations.exists():
        for entry in generations.iterdir():
            if NAME.fullmatch(entry.name) and not entry.is_symlink():
                try:
                    receipt(entry)
                    entries[entry.name] = {"legacyBoot": boot_id(), "device": entry.stat().st_dev, "inode": entry.stat().st_ino}
                except (OSError, ValueError, RuntimeError):
                    pass
    active = current_name(home)
    previous = sorted((name for name in entries if name != active),
                      key=lambda name: (generations / name).stat().st_mtime_ns, reverse=True)
    value = {"schema": SCHEMA, "managed": entries, "previous": previous[0] if previous else None,
             "candidate": None, "pending": None, "stages": {}, "work": {}, "trash": {}, "pins": []}
    atomic_json(filename, value)
    return value


def validate_state(home, value):
    for table in ["managed", "work", "stages", "trash"]:
        if not isinstance(value.get(table), dict):
            raise RuntimeError("Invalid SDK cache registry")
    for name in [*value["managed"], value.get("previous"), value.get("candidate"), *value.get("pins", [])]:
        if name is not None and (not isinstance(name, str) or not NAME.fullmatch(name)):
            raise RuntimeError("Invalid managed SDK identity")
    if value.get("pending"):
        for name in value["pending"].values():
            if name is not None and not NAME.fullmatch(name):
                raise RuntimeError("Invalid pending SDK identity")
    for table in ["work", "stages", "trash"]:
        for name, item in value[table].items():
            path = Path(item["path"])
            if not path.is_absolute() or path.parent.is_symlink() or path.parent.resolve() != path.parent:
                raise RuntimeError("Managed SDK workspace parent changed")
            parent = home / ("work" if table == "work" else "trash")
            if table == "stages":
                parent = Path(value["stageParent"])
            elif table == "trash" and path.parent != parent:
                parent = Path(value["stageParent"]) / ".sdk-trash"
            prefix = {"work": "copy-", "stages": ".link-staging-", "trash": "discard-"}[table]
            if path.parent != parent or path.name != name or not re.fullmatch(re.escape(prefix) + r"[a-zA-Z0-9_-]+", name):
                raise RuntimeError("Managed SDK workspace escapes its registered location")
            if table != "trash" and not re.fullmatch(r"(?:copy-|build-)[a-zA-Z0-9_-]+(?:\.lock)?", item["lease"]):
                raise RuntimeError("Invalid SDK workspace lease")


def save(home, value):
    atomic_json(home / "retention.json", value)


def lease(home, name, require_exists=True):
    if not NAME.fullmatch(name):
        raise RuntimeError("Invalid SDK lease identity")
    with locked(home, exclusive=False):
        if require_exists:
            receipt(home / "generations" / name)
        path = home / "leases" / (name + ".lock")
        descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        try:
            fcntl.flock(descriptor, fcntl.LOCK_SH)
        except BaseException:
            os.close(descriptor)
            raise
    return descriptor


def try_exclusive(path):
    descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return descriptor
    except BlockingIOError:
        os.close(descriptor)
        return None


def size(path):
    total = 0
    if path.is_symlink():
        return 0
    for parent, directories, files in os.walk(path, followlinks=False):
        directories[:] = [name for name in directories if not (Path(parent) / name).is_symlink()]
        for name in files:
            item = Path(parent) / name
            info = item.lstat()
            if stat.S_ISREG(info.st_mode):
                total += info.st_size
    return total


def check_capacity(home, value, requested):
    limit = int(os.environ.get("ORO_LOCAL_SDK_MAX_BYTES", DEFAULT_LIMIT))
    if limit <= 0:
        raise RuntimeError("ORO_LOCAL_SDK_MAX_BYTES must be positive")
    used = sum(size(home / "generations" / name) for name in value["managed"])
    for table in ["work", "stages"]:
        for item in value[table].values():
            used += max(item.get("reserved", 0), size(Path(item["path"])))
    used += sum(size(Path(item["path"])) for item in value["trash"].values())
    filesystem = os.statvfs(home)
    if used + requested > limit or filesystem.f_bavail * filesystem.f_frsize < requested + RESERVE:
        raise RuntimeError("Insufficient SDK cache capacity; active, rollback and in-use generations were retained. "
                           "Stop old commands or increase ORO_LOCAL_SDK_MAX_BYTES.")


def begin_work(home, content, requested):
    if not NAME.fullmatch(content) or requested < 0:
        raise RuntimeError("Invalid SDK preparation request")
    with locked(home):
        value = state(home)
        target = home / "generations" / content
        if target.is_symlink():
            raise RuntimeError("SDK preparation destination is a link")
        if target.exists():
            receipt(target)
            if content not in value["managed"]:
                raise RuntimeError("SDK preparation destination is not registered")
            descriptor = os.open(home / "leases" / (content + ".lock"),
                                 os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
            fcntl.flock(descriptor, fcntl.LOCK_SH)
            value["candidate"] = content
            save(home, value)
            return target, descriptor, True
        check_capacity(home, value, requested)
        name = "copy-" + uuid.uuid4().hex
        path = home / "work" / name
        descriptor = os.open(home / "leases" / (name + ".lock"),
                             os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        fcntl.flock(descriptor, fcntl.LOCK_SH)
        path.mkdir(mode=0o700)
        info = path.stat()
        value["work"][name] = {"path": str(path), "lease": name + ".lock", "reserved": requested,
                               "device": info.st_dev, "inode": info.st_ino}
        save(home, value)
        return path, descriptor, False


def finish_work(home, work, content):
    with locked(home):
        value = state(home)
        destination = home / "generations" / content
        (home / "generations").mkdir(exist_ok=True)
        # Concurrent same-content publishers reuse the winner; the owned losing
        # copy is collected once its preparation lease is released.
        if not destination.exists():
            info = work.stat()
            value["managed"][content] = {"legacyBoot": None, "device": info.st_dev, "inode": info.st_ino}
            value["candidate"] = content
            # Register both names before moving, so a killed publisher cannot
            # leave an untracked full payload between the rename and save.
            save(home, value)
            work.chmod(0o755)
            os.rename(work, destination)
            destination.chmod(0o555)
            sync_directory(destination)
            sync_directory(destination.parent)
            sync_directory(work.parent)
            value["work"].pop(work.name, None)
            value["managed"][content] = {"legacyBoot": None, "device": destination.stat().st_dev, "inode": destination.stat().st_ino}
        descriptor = os.open(home / "leases" / (content + ".lock"),
                             os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        fcntl.flock(descriptor, fcntl.LOCK_SH)
        value["candidate"] = content
        save(home, value)
        return destination, descriptor


def remove_tree(path):
    """Remove only an already detached managed tree; never follow or chmod a link."""
    def remove(parent, name):
        info = os.stat(name, dir_fd=parent, follow_symlinks=False)
        if stat.S_ISDIR(info.st_mode):
            child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
            try:
                actual = os.fstat(child)
                if (actual.st_dev, actual.st_ino) != (info.st_dev, info.st_ino):
                    raise RuntimeError("SDK cleanup directory changed")
                os.fchmod(child, stat.S_IMODE(actual.st_mode) | 0o700)
                for entry in os.listdir(child):
                    remove(child, entry)
            finally:
                os.close(child)
            os.rmdir(name, dir_fd=parent)
        else:
            os.unlink(name, dir_fd=parent)
    parent = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        remove(parent, path.name)
    finally:
        os.close(parent)


def collect(home, dry_run=False, legacy_idle=False, force=True, nonblocking=False):
    home = ensure_home(home)
    collector = os.open(home / ".collection.lock", os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        try:
            fcntl.flock(collector, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return {"skipped": "another collector is running"}
        status = home / "gc-status.json"
        if not force and status.exists() and time.time() - status.stat().st_mtime < 300:
            return {"skipped": "collection interval"}
        report = {"removed": [], "kept": [], "errors": [], "dryRun": dry_run}
        detached = []
        with ExitStack() as leases, locked(home, nonblocking=nonblocking):
            value = state(home)
            active = current_name(home)
            if not active and value["managed"] and not value.get("candidate"):
                raise RuntimeError("No valid active SDK; collection was not performed")
            protected = {active, value.get("previous"), value.get("candidate"), *value.get("pins", [])}
            pending = value.get("pending")
            if pending:
                protected.update(pending.values())
            for name, item in list(value["managed"].items()):
                path = home / "generations" / name
                if not path.exists():
                    if not dry_run:
                        value["managed"].pop(name)
                    continue
                if name in protected:
                    report["kept"].append({"name": name, "reason": "active/rollback/prepared/pinned"})
                    continue
                legacy = item.get("legacyBoot")
                if legacy and not legacy_idle and (legacy == boot_id() or legacy == "unknown"):
                    report["kept"].append({"name": name, "reason": "legacy commands not tracked until reboot or idle confirmation"})
                    continue
                receipt(path)
                actual = path.stat()
                if (actual.st_dev, actual.st_ino) != (item["device"], item["inode"]):
                    raise RuntimeError("Managed SDK generation identity changed")
                descriptor = try_exclusive(home / "leases" / (name + ".lock"))
                if descriptor is None:
                    report["kept"].append({"name": name, "reason": "in use"})
                    continue
                leases.callback(os.close, descriptor)
                detached.append((name, path, descriptor, "managed"))
            for table in ["work", "stages"]:
                for name, item in list(value[table].items()):
                    path = Path(item["path"])
                    if not path.exists():
                        if not dry_run:
                            value[table].pop(name)
                        continue
                    info = path.lstat()
                    if path.is_symlink() or (info.st_dev, info.st_ino) != (item["device"], item["inode"]):
                        report["errors"].append("Owned staging directory changed: " + str(path))
                        continue
                    if table == "stages":
                        marker = path / ".sdk-stage.json"
                        if marker.is_symlink() or json.loads(marker.read_text()) != {"schema": 1, "home": str(home), "lease": item["lease"]}:
                            raise RuntimeError("SDK staging ownership marker differs")
                    descriptor = try_exclusive(home / "leases" / item["lease"])
                    if descriptor is not None:
                        leases.callback(os.close, descriptor)
                        detached.append((name, path, descriptor, table))
            if legacy_idle and not dry_run:
                for item in value["managed"].values():
                    item["legacyBoot"] = None
            for name, path, descriptor, table in detached:
                entry = {"name": name, "path": str(path), "bytes": size(path)}
                report["removed"].append(entry)
                if dry_run:
                    continue
                if table == "stages":
                    trash_parent = path.parent / ".sdk-trash"
                    trash_parent.mkdir(mode=0o700, exist_ok=True)
                else:
                    trash_parent = home / "trash"
                if trash_parent.is_symlink():
                    raise RuntimeError("SDK trash directory is a link")
                trash = trash_parent / ("discard-" + uuid.uuid4().hex)
                info = path.stat()
                value["trash"][trash.name] = {"path": str(trash), "device": info.st_dev, "inode": info.st_ino}
                save(home, value)
                directory_fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
                try:
                    os.fchmod(directory_fd, stat.S_IMODE(os.fstat(directory_fd).st_mode) | 0o700)
                    os.rename(path, trash)
                finally:
                    os.close(directory_fd)
                value[table].pop(name)
                save(home, value)
            if not dry_run:
                save(home, value)
        # Recursive deletion happens outside the publication lock. A slow disk
        # cannot prevent the active launcher from acquiring a usage lease.
        if not dry_run:
            with locked(home):
                pending_trash = dict(state(home)["trash"])
            for name, item in pending_trash.items():
                path = Path(item["path"])
                try:
                    if path.exists():
                        info = path.lstat()
                        if path.is_symlink() or (info.st_dev, info.st_ino) != (item["device"], item["inode"]):
                            raise RuntimeError("SDK trash identity changed")
                        remove_tree(path)
                    with locked(home):
                        latest = state(home)
                        latest["trash"].pop(name, None)
                        save(home, latest)
                except (OSError, RuntimeError) as error:
                    report["errors"].append(str(error))
            with locked(home):
                trim_lease_files(home, state(home))
            atomic_json(status, report)
        return report
    finally:
        os.close(collector)


def trim_lease_files(home, value):
    keep = {name + ".lock" for name in value["managed"]}
    keep.update(item["lease"] for table in ["work", "stages"] for item in value[table].values())
    for path in (home / "leases").iterdir():
        if path.name in keep or path.is_symlink() or not re.fullmatch(r"(?:sdk-|copy-|build-)[a-zA-Z0-9_-]+(?:\.lock)?", path.name):
            continue
        descriptor = try_exclusive(path)
        if descriptor is not None:
            try:
                path.unlink()
            finally:
                os.close(descriptor)


def source_leases(paths):
    descriptors = []
    try:
        for path in paths:
            for parent in [path.resolve(), *path.resolve().parents]:
                if parent.parent.name == "generations" and NAME.fullmatch(parent.name) and (parent / "generation.json").is_file():
                    descriptors.append(lease(parent.parent.parent, parent.name))
                    break
        return descriptors
    except BaseException:
        for descriptor in descriptors:
            os.close(descriptor)
        raise


def create_stage(home, parent, descriptor, lease_path, publication_fd):
    # The shell holds the publication lock until this method has registered the
    # directory. Its inherited shared lease also survives a killed shell.
    if parent != Path(__file__).resolve().parent.parent / "build/npm" or parent.is_symlink():
        raise RuntimeError("Source staging must use its build/npm directory")
    if lease_path.parent != home / "leases" or not lease_path.name.startswith("build-"):
        raise RuntimeError("Invalid source build lease")
    expected, actual = lease_path.stat(), os.fstat(descriptor)
    if (expected.st_dev, expected.st_ino) != (actual.st_dev, actual.st_ino):
        raise RuntimeError("Source build lease descriptor differs")
    fcntl.flock(descriptor, fcntl.LOCK_SH)
    expected, actual = (home / ".publication.lock").stat(), os.fstat(publication_fd)
    if (expected.st_dev, expected.st_ino) != (actual.st_dev, actual.st_ino):
        raise RuntimeError("Source publication lock descriptor differs")
    fcntl.flock(publication_fd, fcntl.LOCK_EX)
    value = state(home)
    value["stageParent"] = str(parent)
    active = current_name(home)
    estimate = size(home / "generations" / active) if active else 512 * 1024**2
    check_capacity(home, value, estimate)
    import tempfile
    path = Path(tempfile.mkdtemp(prefix=".link-staging-", dir=parent))
    info = path.stat()
    value["stages"][path.name] = {"path": str(path), "lease": lease_path.name, "reserved": estimate,
                                  "device": info.st_dev, "inode": info.st_ino}
    atomic_json(path / ".sdk-stage.json", {"schema": 1, "home": str(home), "lease": lease_path.name})
    save(home, value)
    return path


def run(home, node, entry, preload, arguments):
    # Existing lock files support shared kernel leases without filesystem writes.
    # Publication provisions them before exposing a generation to readers.
    descriptors = []
    # Nested managed commands also carry staging and other-generation leases.
    # Duplicate first: selecting fixed descriptor numbers could replace a live
    # inherited lock before the child has acquired its own generation lease.
    for inherited in json.loads(os.environ.get("ORO_SDK_LEASES", "[]")):
        expected, actual = Path(inherited["path"]).lstat(), os.fstat(inherited["fd"])
        if not stat.S_ISREG(actual.st_mode) or (expected.st_dev, expected.st_ino) != (actual.st_dev, actual.st_ino):
            raise RuntimeError("Inherited SDK usage lease differs")
        descriptor = fcntl.fcntl(inherited["fd"], fcntl.F_DUPFD_CLOEXEC, 64)
        descriptors.append({"fd": descriptor, "path": inherited["path"]})
    with locked(home, exclusive=False):
        active = current_name(home)
        if not active:
            raise RuntimeError("No active SDK generation")
        names = {active}
        for key in ["ORO_HOME", "ORO_HOME_API"]:
            if os.environ.get(key):
                target = Path(os.environ[key]).resolve()
                if target.is_relative_to(home / "generations"):
                    names.add(target.relative_to(home / "generations").parts[0])
        for name in sorted(names):
            receipt(home / "generations" / name)
            path = home / "leases" / (name + ".lock")
            if any(item["path"] == str(path) for item in descriptors):
                continue
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            try:
                fcntl.flock(descriptor, fcntl.LOCK_SH)
                target_fd = fcntl.fcntl(descriptor, fcntl.F_DUPFD_CLOEXEC, 64)
            finally:
                os.close(descriptor)
            descriptors.append({"fd": target_fd, "path": str(path)})
        generation = home / "generations" / active
    env = {**os.environ, "ORO_SDK_GENERATION": str(generation), "ORO_SDK_LEASES": json.dumps(descriptors)}
    env["NODE_OPTIONS"] = env.get("NODE_OPTIONS", "") + " --require " + json.dumps(str(preload))
    child = None
    def forward(number, frame):
        if child is not None:
            try:
                child.send_signal(number)
            except ProcessLookupError:
                pass
    signal.signal(signal.SIGINT, forward)
    signal.signal(signal.SIGTERM, forward)
    try:
        child = subprocess.Popen([node, str(entry), *arguments], env=env, pass_fds=[item["fd"] for item in descriptors])
        code = child.wait()
    finally:
        for item in descriptors:
            os.close(item["fd"])
        try:
            collect(home, force=False, nonblocking=True)
        except (OSError, ValueError, RuntimeError):
            pass
    return code if code >= 0 else 128 - code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["run", "gc", "stage", "pin", "unpin"])
    parser.add_argument("--home", type=Path, required=True)
    parser.add_argument("--generation")
    parser.add_argument("--node")
    parser.add_argument("--entry", type=Path)
    parser.add_argument("--preload", type=Path)
    parser.add_argument("--parent", type=Path)
    parser.add_argument("--lease-fd", type=int)
    parser.add_argument("--publication-fd", type=int)
    parser.add_argument("--lease-path", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--legacy-idle", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    raw = sys.argv[1:]
    divider = raw.index("--") if "--" in raw else len(raw)
    args = parser.parse_args(raw[:divider])
    arguments = raw[divider + 1:]
    home = args.home.resolve()
    if args.action in ["pin", "unpin"]:
        if not args.generation or not NAME.fullmatch(args.generation):
            parser.error("--generation must name a managed SDK generation")
        with locked(home):
            value = state(home)
            receipt(home / "generations" / args.generation)
            if args.generation not in value["managed"]:
                raise RuntimeError("Cannot pin an unregistered SDK generation")
            pins = set(value.get("pins", []))
            if args.action == "pin":
                pins.add(args.generation)
            else:
                pins.discard(args.generation)
            value["pins"] = sorted(pins)
            save(home, value)
        return
    if args.action == "run":
        raise SystemExit(run(home, args.node, args.entry, args.preload, arguments))
    if args.action == "stage":
        print(create_stage(home, args.parent.resolve(), args.lease_fd, args.lease_path.resolve(), args.publication_fd))
    else:
        report = collect(home, args.dry_run, args.legacy_idle)
        if not args.quiet:
            print(json.dumps(report, indent=2))
        if report.get("errors"):
            raise SystemExit(1)


if __name__ == "__main__":
    main()
