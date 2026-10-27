"""Isolated Linux source SDK retention behavior, with public synthetic payloads."""
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("publication", ROOT / "bin/publish-runtime-sdk.py")
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
p.validate = lambda *args: "isolated public payload"
c = p.cache
base = Path(tempfile.mkdtemp(prefix="oro-sdk-retention-"))
home = base / "managed"
sdk, api = base / "sdk", base / "api"
for path, name in [(sdk, "@oro-computer/runtime-linux-x64"), (api, "@oro-computer/runtime")]:
    path.mkdir()
    (path / "package.json").write_text(json.dumps({"name": name, "version": "0.1.8", "type": "module"}))
(api / "bin").mkdir()
(api / "bin/oroc.js").write_text(r'''
import fs from 'node:fs'
import cp from 'node:child_process'
export async function run () {
 const [mode, latch] = process.argv.slice(2)
 if (!mode) return console.log('public CLI ready')
 fs.writeFileSync(latch + '.parents', JSON.stringify([process.pid, process.ppid]))
 const source = `
if (process.disconnect) process.disconnect()
const fs = require('node:fs')
const ready = ${JSON.stringify(latch + '.ready')}
// Existence signals a complete PID; staging and publication share a directory.
fs.writeFileSync(ready + '.pending', String(process.pid), {flag:'wx'})
const publish = () => fs.renameSync(ready + '.pending', ready)
if (${mode === 'readiness'}) {
 fs.writeFileSync(${JSON.stringify(latch + '.staged')}, '')
 const publication = setInterval(() => {
  if (fs.existsSync(${JSON.stringify(latch + '.publish')})) {
   clearInterval(publication)
   publish()
  }
 }, 10)
} else publish()
setInterval(() => { if (fs.existsSync(${JSON.stringify(latch + '.release')})) process.exit(0) }, 10)
`
 const childFile = latch + '.cjs'
 fs.writeFileSync(childFile, source)
 const options = {stdio:'ignore', detached:true, env:{PATH:process.env.PATH}}
 if(['spawn','readiness'].includes(mode)) cp.spawn(process.execPath, [childFile], options).unref()
 if(mode==='fork') cp.fork(childFile, [], options).unref()
 if(mode==='execFile') cp.execFile(process.execPath, [childFile], options).unref()
 if(mode==='exec') cp.exec(`"${process.execPath}" "${childFile}"`, options).unref()
 const intermediate = `require('node:child_process').spawn(process.execPath,[${JSON.stringify(childFile)}],{detached:true,stdio:'ignore'}).unref()`
 if(mode==='spawnSync') cp.spawnSync(process.execPath,['-e',intermediate],options)
 if(mode==='execFileSync') cp.execFileSync(process.execPath,['-e',intermediate],options)
 if(mode==='execSync') {
  fs.writeFileSync(latch+'.parent.cjs',intermediate)
  cp.execSync(`"${process.execPath}" "${latch}.parent.cjs"`,options)
 }
 if(mode==='native') cp.spawn('/bin/sh',['-c','exec "$1" "$2"','public',process.execPath,childFile],options).unref()
 if(mode==='killed') {
  cp.spawn(process.execPath,[childFile],options).unref()
  await new Promise(()=>{})
 }
 if(['execFile','exec'].includes(mode)) await new Promise(()=>{})
}
''')
clean_env = {key: value for key, value in os.environ.items()
             if key not in ["ORO_HOME", "ORO_HOME_API", "ORO_SDK_LEASES", "NODE_OPTIONS"]}

def publish(marker):
    (sdk / "marker").write_text(marker)
    return p.publish(base, sdk, api, home, True)

def wait_for(path, limit=10):
    until = time.monotonic() + limit
    while not path.exists():
        assert time.monotonic() < until, str(path)
        time.sleep(.01)

def generations():
    return set(item.name for item in (home / "generations").iterdir())

case = sys.argv[1]
if case == "readonly":
    publish("public readonly SDK")
    before = {str(path.relative_to(home)): path.read_bytes() for path in home.rglob("*") if path.is_file() and not path.is_symlink()}
    for path in sorted(home.rglob("*"), key=lambda path: len(path.parts), reverse=True):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode & ~0o222)
    home.chmod(0o555)
    result = subprocess.run([str(home / "bin/oroc.cjs")], env=clean_env, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == "public CLI ready"
    after = {str(path.relative_to(home)): path.read_bytes() for path in home.rglob("*") if path.is_file() and not path.is_symlink()}
    assert before == after
elif case == "retention":
    first = publish("A")
    assert publish("A")["reusedGeneration"]
    assert len(generations()) == 1
    second = publish("B")
    third = publish("C")
    assert generations() == {Path(second["generation"]).name, Path(third["generation"]).name}
    assert not Path(first["generation"]).exists()
    prior = generations()
    assert publish("C")["reusedGeneration"] and generations() == prior
    assert json.loads((home / "retention.json").read_text())["previous"] == Path(second["generation"]).name
    # Explicit pins are persisted separately from active/rollback retention.
    command = [sys.executable, str(ROOT / "bin/sdk-cache.py"), "pin", "--home", str(home),
               "--generation", Path(second["generation"]).name]
    subprocess.check_call(command)
    publish("D")
    assert Path(second["generation"]).exists()
    command[2] = "unpin"
    subprocess.check_call(command)
    c.collect(home)
    assert not Path(second["generation"]).exists()
    # A quota refusal leaves the current SDK and existing rollback untouched.
    current = (home / "current").resolve()
    os.environ["ORO_LOCAL_SDK_MAX_BYTES"] = "1"
    try:
        publish("over quota")
    except RuntimeError as error:
        assert "capacity" in str(error)
    else:
        raise AssertionError("quota allowed a new copy")
    assert (home / "current").resolve() == current
    os.environ.pop("ORO_LOCAL_SDK_MAX_BYTES")
    assert subprocess.check_output([str(home / "bin/oroc.cjs")], env=clean_env).strip() == b"public CLI ready"
    # Preserve unowned paths even when their names resemble cache entries.
    unowned = home / "generations/sdk-unowned"
    unowned.mkdir()
    (unowned / "public").write_text("not registered")
    c.collect(home)
    assert unowned.exists()
elif case == "readiness":
    publish("atomic child readiness")
    latch = base / "readiness"
    ready = Path(str(latch) + ".ready")
    pending = Path(str(ready) + ".pending")
    process = subprocess.Popen([str(home / "bin/oroc.cjs"), "readiness", str(latch)],
                               env=clean_env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        # The producer confirms that staging is complete and then waits for
        # permission to publish. File existence cannot expose a partial PID.
        wait_for(Path(str(latch) + ".staged"))
        staged_pid = pending.read_text()
        assert int(staged_pid) > 0, staged_pid
        assert not ready.exists(), "Readiness became visible before publication"
        Path(str(latch) + ".publish").write_text("")
        wait_for(ready)
        assert ready.read_text() == staged_pid
        assert not pending.exists()
    finally:
        Path(str(latch) + ".release").write_text("public")
        assert process.wait(timeout=10) == 0, process.stderr.read().decode()
elif case == "descendants":
    for mode in ["spawn", "fork", "execFile", "exec", "spawnSync", "execFileSync", "execSync", "native", "killed"]:
        held = Path(publish("held " + mode)["generation"])
        latch = base / mode
        workspace, work_fd, _ = c.begin_work(home, "sdk-public-nested-lease", 1024)
        lease_path = home / "leases" / (workspace.name + ".lock")
        env = {**clean_env, "ORO_SDK_LEASES": json.dumps([{"fd": work_fd, "path": str(lease_path)}]),
               "NODE_OPTIONS": "--require " + json.dumps(str(ROOT / "bin/sdk-lease.cjs"))}
        process = subprocess.Popen([str(home / "bin/oroc.cjs"), mode, str(latch)], env=env,
                                   pass_fds=[work_fd], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        os.close(work_fd)
        try:
            wait_for(Path(str(latch) + ".ready"))
            parents = json.loads(Path(str(latch) + ".parents").read_text())
            child_pid = int(Path(str(latch) + ".ready").read_text())
            if mode in ["killed", "execFile", "exec"]:
                # Kill both supervisors and the SDK root; only the descendant's
                # inherited kernel lock may protect its generation now.
                for pid in [*parents, process.pid]:
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            else:
                assert process.wait(timeout=10) == 0, process.stderr.read().decode()
            publish("next " + mode)
            publish("latest " + mode)
            report = c.collect(home)
            assert held.exists() and workspace.exists(), (mode, report)
            assert any(item["name"] == held.name and item["reason"] == "in use" for item in report["kept"]), (mode, report)
        finally:
            Path(str(latch) + ".release").write_text("public")
            process.wait(timeout=10)
        until = time.monotonic() + 10
        while True:
            c.collect(home)
            if not held.exists() and not workspace.exists():
                break
            assert time.monotonic() < until, (mode, child_pid)
            time.sleep(.02)
elif case == "recovery":
    one = Path(publish("A")["generation"])
    two = Path(publish("B")["generation"])
    candidate, _, fd, _ = p.prepare(base, sdk, api, home)
    os.close(fd)
    # The journal written before pointer replacement recovers both crash sides.
    with c.locked(home):
        value = c.state(home)
        value["pending"] = {"old": one.name, "new": two.name, "rollback": None}
        c.save(home, value)
    c.collect(home)
    assert json.loads((home / "retention.json").read_text())["previous"] == one.name
    # A failed partial copy is tracked, reclaimed after its lease closes, and
    # cannot delete an outside file reached through a payload link.
    workspace, lock, _ = c.begin_work(home, "sdk-aborted", 1024)
    outside = base / "outside"
    outside.mkdir()
    (outside / "safe").write_text("public")
    (workspace / "outside-link").symlink_to(outside, target_is_directory=True)
    c.collect(home)
    assert workspace.exists()
    os.close(lock)
    original = c.remove_tree
    c.remove_tree = lambda path: (_ for _ in ()).throw(OSError("public interrupted delete"))
    result = c.collect(home)
    assert result["errors"] and not workspace.exists()
    assert json.loads((home / "retention.json").read_text())["trash"]
    c.remove_tree = original
    assert not c.collect(home)["errors"]
    assert (outside / "safe").read_text() == "public"
    assert not json.loads((home / "retention.json").read_text())["trash"]
    staging_parent = base / "build/npm"
    staging_parent.mkdir(parents=True)
    unowned = staging_parent / ".link-staging-unowned"
    unowned.mkdir()
    c.__file__ = str(base / "bin/sdk-cache.py")
    lease_path = home / "leases/build-public"
    descriptor = os.open(lease_path, os.O_RDWR | os.O_CREAT, 0o600)
    publication = os.open(home / ".publication.lock", os.O_RDWR)
    stage = c.create_stage(home, staging_parent, descriptor, lease_path, publication)
    os.close(publication)
    c.collect(home)
    assert stage.exists()
    os.close(descriptor)
    c.collect(home)
    assert not stage.exists() and unowned.exists()
    # A corrupted registry cannot direct recursive deletion outside its roots.
    with c.locked(home):
        value = c.state(home)
        value["trash"]["discard-public"] = {"path": str(outside), "device": outside.stat().st_dev, "inode": outside.stat().st_ino}
        c.save(home, value)
    try:
        c.collect(home)
    except RuntimeError:
        pass
    else:
        raise AssertionError("escaped registry accepted")
    value["trash"] = {}
    c.save(home, value)
    assert (outside / "safe").exists()
    # Legacy copies are retained on this boot until explicitly declared idle.
    publish("C")
    with c.locked(home):
        value = c.state(home)
        value["managed"][one.name] = {"legacyBoot": c.boot_id(), "device": one.stat().st_dev, "inode": one.stat().st_ino} if one.exists() else {}
        if not one.exists():
            value["managed"].pop(one.name)
        # Select the actual rollback for a simulated legacy generation.
        old = home / "generations" / value["previous"]
        value["managed"][old.name]["legacyBoot"] = c.boot_id()
        value["previous"] = None
        c.save(home, value)
    c.collect(home)
    assert old.exists()
    before = old.stat().st_mode
    preview = c.collect(home, dry_run=True, legacy_idle=True)
    assert old.exists() and old.stat().st_mode == before
    assert any(item["name"] == old.name for item in preview["removed"])
    c.collect(home, legacy_idle=True)
    assert not old.exists()
else:
    raise AssertionError(case)
print("SDK cache " + case + " passed")
