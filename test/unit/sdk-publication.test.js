import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtempSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createNpmFixtureEnvironment, NPM_FIXTURE_TIMEOUT } from '../scripts/npm-fixture.js'

const root = fileURLToPath(new URL('../../', import.meta.url))
const sdkPackTimeout = 3 * NPM_FIXTURE_TIMEOUT + 60000
test('stable SDK publication pins complete generations across concurrent activation, source edits and discarded build paths', {
  skip: process.platform !== 'linux', timeout: 60000
}, () => {
  const program = String.raw`
import importlib.util, pathlib, tempfile, json, subprocess, threading, time, os
spec=importlib.util.spec_from_file_location('publication','bin/publish-runtime-sdk.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
p.validate=lambda *args:'public fixture validation'
environ={k:v for k,v in os.environ.items() if k not in ['ORO_HOME','ORO_HOME_API','ORO_ENV_FILE','ORO_CONFIG_HOME']}
root=pathlib.Path(tempfile.mkdtemp(prefix='oro-stable-sdk-'))
sdk=root/'build/staging/sdk';api=root/'build/staging/api';home=root/'.local/sdk';prefix=root/'global'
for package,name in [(sdk,'@oro-computer/runtime-linux-x64'),(api,'@oro-computer/runtime')]:
 package.mkdir(parents=True)
 (package/'package.json').write_text(json.dumps({'name':name,'version':'0.1.8','type':'module','main':'src/index.js'}))
(sdk/'bin').mkdir();(sdk/'bin/oroc').write_text('public native binary')
(sdk/'src').mkdir();(sdk/'src/index.js').write_text('export default import.meta.url')
(api/'bin').mkdir();(api/'bin/oroc.js').write_text('''
import fs from 'node:fs'
import path from 'node:path'
import {fileURLToPath} from 'node:url'
import sdk from '@oro-computer/runtime-linux-x64'
export async function run() {
 const own=path.dirname(path.dirname(fileURLToPath(import.meta.url)))
 const held=process.argv[2]
 if(held) {
  fs.writeFileSync(held+'.ready','public')
  const until=Date.now()+10000
  while(!fs.existsSync(held+'.release')) { if(Date.now()>until) throw Error('public latch timed out'); await new Promise(r=>setTimeout(r,10)) }
 }
 console.log(JSON.stringify({api:fs.readFileSync(path.join(own,'marker'),'utf8'),
   sdk:fs.readFileSync(path.join(process.env.ORO_HOME,'marker'),'utf8'),
   imported:path.dirname(path.dirname(fileURLToPath(sdk))),sdkRoot:process.env.ORO_HOME,apiRoot:process.env.ORO_HOME_API}))
}
''')
external=root/'external';external.mkdir();(external/'header.hh').write_text('public header A')
(sdk/'include').mkdir();(sdk/'include/deep').symlink_to(external,target_is_directory=True)
(api/'nested').mkdir();(api/'nested/header').symlink_to(external/'header.hh')
(sdk/'.oro.env').write_text('PUBLIC_TOOLCHAIN=preserved\n')
(sdk/'marker').write_text('A');(api/'marker').write_text('A')
first=p.publish(root,sdk,api,home,True,prefix)
launcher=prefix/'bin/oroc';generation=pathlib.Path(first['generation'])
def read():
 result=subprocess.run([str(launcher)],capture_output=True,text=True,timeout=10,env=environ)
 assert result.returncode==0,result.stderr
 value=json.loads(result.stdout);assert value['api']==value['sdk'],value
 assert value['sdkRoot']==value['imported'],value
 assert pathlib.Path(value['apiRoot']).parents[2]==pathlib.Path(value['sdkRoot']).parents[2]
 return value
assert read()['api']=='A'
original_link_inode=launcher.lstat().st_ino
assert p.link_global(home,prefix,'@oro-computer/runtime-linux-x64') is None
assert launcher.lstat().st_ino==original_link_inode
assert not any(x.is_symlink() for x in (generation/'packages').rglob('*'))
assert (generation/'packages/@oro-computer/runtime-linux-x64/include/deep/header.hh').read_text()=='public header A'
assert (home/'configuration/sdk.env').stat().st_mode & 0o777 == 0o600
(home/'configuration/sdk.env').write_text('PUBLIC_TOOLCHAIN=user_updated\n')
latched=root/'latched';old=subprocess.Popen([str(launcher),str(latched)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=environ)
until=time.monotonic()+10
while not pathlib.Path(str(latched)+'.ready').exists():
 assert time.monotonic()<until;time.sleep(.01)
errors=[];observed=[];stop=threading.Event()
def reader():
 try:
  while not stop.is_set(): observed.append(read()['api'])
 except BaseException as error: errors.append(str(error))
thread=threading.Thread(target=reader);thread.start()
for index in range(5):
 value='B'+str(index);(sdk/'marker').write_text(value);(api/'marker').write_text(value)
 result=p.publish(root,sdk,api,home,True)
 assert result['previousGeneration'] and pathlib.Path(result['previousGeneration']).is_dir()
 assert (home/'configuration/sdk.env').read_text()=='PUBLIC_TOOLCHAIN=user_updated\n'
stop.set();thread.join(10);assert not thread.is_alive() and not errors,errors
assert observed
pathlib.Path(str(latched)+'.release').write_text('public')
stdout,stderr=old.communicate(timeout=10);assert old.returncode==0,stderr
held=json.loads(stdout);assert held['api']=='A' and held['sdk']=='A',held
# A changing input is rejected before activation; no mixed snapshot becomes current.
prior=(home/'current').resolve();copytree=p.shutil.copytree
(sdk/'marker').write_text('new unique content for interrupted copy')
changed=False
def racing_copy(source,destination,*args,**kwargs):
 global changed
 result=copytree(source,destination,*args,**kwargs)
 if pathlib.Path(source)==sdk and not changed:
  changed=True;(sdk/'marker').write_text('changed while copying')
 return result
p.shutil.copytree=racing_copy
try:p.publish(root,sdk,api,home,True)
except RuntimeError:pass
else:raise AssertionError('changing input accepted')
p.shutil.copytree=copytree
assert (home/'current').resolve()==prior
(sdk/'marker').write_text('B4')
(external/'header.hh').write_text('mutable workspace changed')
(root/'build').rename(root/'retained-disposable-build')
assert read()['api']=='B4'
assert (prior/'packages/@oro-computer/runtime/nested/header').read_text()=='public header A'
assert (prior/'packages/@oro-computer/runtime-linux-x64/bin/oroc').read_text()=='public native binary'
# A failed post-publication check restores the old pointer and retains both trees.
verify=p.verify_generation;calls=0
prior=(home/'current').resolve()
generation,_,held_fd,_=p.prepare(root,root/'retained-disposable-build/staging/sdk',root/'retained-disposable-build/staging/api',home)
def fail_after_swap(candidate):
 global calls
 calls+=1
 if calls==2:raise RuntimeError('public post-publication failure')
 return verify(candidate)
p.verify_generation=fail_after_swap
try:p.activate(home,generation)
except RuntimeError:pass
else:raise AssertionError('post-publication failure accepted')
p.verify_generation=verify
os.close(held_fd)
assert (home/'current').resolve()==prior and generation.is_dir()
assert read()['api']=='B4'
# Reject nested cycles before copying; never replace the active pointer.
input_sdk=root/'retained-disposable-build/staging/sdk';input_api=root/'retained-disposable-build/staging/api'
(input_sdk/'include/cycle').symlink_to(input_sdk/'include',target_is_directory=True)
try:p.prepare(root,input_sdk,input_api,home)
except RuntimeError:pass
else:raise AssertionError('nested link cycle accepted')
assert (home/'current').resolve()==prior
print('stable-recursive-sdk-copy-pinned-concurrent-generations-history-config-and-build-independence-passed')
`
  const result = spawnSync('python3', ['-c', program], { cwd: root, encoding: 'utf8', timeout: 55000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /stable-recursive-sdk-copy-pinned-concurrent-generations-history-config-and-build-independence-passed/)
})

test('Linux relink cleans owned staging and CI packs all three release packages without the source manager', {
  skip: process.platform !== 'linux', timeout: sdkPackTimeout + 10000
}, () => {
  const program = String.raw`
import pathlib,tempfile,subprocess,os,json,shutil
source=pathlib.Path.cwd();root=pathlib.Path(tempfile.mkdtemp(prefix='oro-relink-pipeline-'))
def write(name,text,executable=False):
 target=root/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
 if executable:target.chmod(0o755)
write('bin/publish-npm-modules.sh',(source/'bin/publish-npm-modules.sh').read_text(),True)
for name in ['sdk-cache.py','sdk-lease.cjs','verify-npm-package-size.js']:
 write('bin/'+name,(source/'bin'/name).read_text())
write('package.json',json.dumps({'type':'module'}))
write('bin/android-functions.sh','android_supported_abis() { echo arm64-v8a x86_64; }\n')
write('bin/functions.sh','host_arch() { echo x86_64; }\nhost_os() { echo Linux; }\n')
write('bin/runtime-artifacts.sh','ORO_RUNTIME_ARTIFACT_NAME=oro-runtime\n')
write('commands/git','#!/bin/sh\nprintf deadbeef\n',True)
write('commands/npm','#!/bin/sh\necho "unexpected early npm linking" >&2\nexit 31\n',True)
write('VERSION.txt','0.1.8\n')
for name in ['LICENSE.txt','NOTICE','THIRD_PARTY_NOTICES.md','README.md','docs/BUILD_ENVIRONMENT.md','assets/public','api/README.md','api/public.js','npm/src/index.js','npm/bin/oroc.js']:write(name,'public fixture\n')
for name in ['runtime','runtime-linux-x64','runtime-node']:
 write('npm/packages/@oro-computer/'+name+'/package.json',json.dumps({'name':'@oro-computer/'+name,'version':'0.1.8'}))
live=root/'build/npm/linux';write('build/npm/linux/bin/oroc','#!/bin/sh\nprintf public-old-alive\n',True)
write('build/npm/linux/preserve','original active installation')
write('bin/install.sh',r'''#!/bin/bash
set -eu
[[ "$PREFIX" == "$ORO_HOME" ]]
[[ "$ORO_HOME" != "$ORO_TEST_LIVE" ]]
[[ "$*" != *--link* ]]
[[ "$("$ORO_TEST_LIVE/bin/oroc")" == public-old-alive ]]
[[ "$ORO_TEST_FAIL_BUILD" != 1 ]] || exit 23
mkdir -p "$ORO_HOME"/{bin,src,include,uv,lib/x86_64-desktop,objects/x86_64-desktop}
printf '#!/bin/sh\nprintf "0.1.8 (deadbeef)\\n"\n' > "$ORO_HOME/bin/oroc"
chmod +x "$ORO_HOME/bin/oroc"
printf public > "$ORO_HOME/src/init.cc"
printf public > "$ORO_HOME/include/public.hh"
printf public > "$ORO_HOME/uv/public.h"
printf public > "$ORO_HOME/lib/x86_64-desktop/liboro-runtime.a"
[[ "$("$ORO_TEST_LIVE/bin/oroc")" == public-old-alive ]]
''',True)
write('bin/publish-runtime-sdk.py',r'''
import argparse,pathlib,os,json
p=argparse.ArgumentParser();p.add_argument('--sdk');p.add_argument('--api');p.add_argument('--stable-home');p.add_argument('--legacy-prefix');p.add_argument('--global-prefix');p.add_argument('--activate-local',action='store_true');a=p.parse_args()
sdk=pathlib.Path(a.sdk);api=pathlib.Path(a.api);live=pathlib.Path(os.environ['ORO_TEST_LIVE'])
assert (sdk/'package.json').is_file() and (api/'package.json').is_file()
assert (sdk/'bin/oroc').is_file() and (sdk/'include/public.hh').is_file()
assert not sdk.is_relative_to(live)
assert a.legacy_prefix==str(live) and a.activate_local
assert (live/'preserve').read_text()=='original active installation'
pathlib.Path(os.environ['ORO_TEST_RESULT']).write_text(json.dumps(vars(a)))
''')
env={**os.environ,'ORO_TEST_FAIL_BUILD':'0','PREFIX':str(root/'global'),'ORO_LOCAL_SDK_HOME':str(root/'.local/sdk'),'NO_ANDROID':'1','NO_IOS':'1','ORO_TEST_LIVE':str(live),'ORO_TEST_RESULT':str(root/'result.json'),'PATH':str(root/'commands')+os.pathsep+os.environ['PATH']}
env.pop('ORO_NPM_STAGING_HOME',None)
result=subprocess.run(['bash',str(root/'bin/publish-npm-modules.sh'),'--link'],cwd=root,env=env,capture_output=True,text=True,timeout=30)
assert result.returncode==0,result.stdout+result.stderr
record=json.loads((root/'result.json').read_text());assert record['stable_home']==str(root/'.local/sdk')
assert not pathlib.Path(record['sdk']).parents[2].exists(), 'owned completed staging was retained'
assert (live/'preserve').read_text()=='original active installation'
# Installer failure still releases and collects its owned staging, retaining
# unrelated unmarked staging and the existing installed command.
unowned=root/'build/npm/.link-staging-unowned';unowned.mkdir()
failed=subprocess.run(['bash',str(root/'bin/publish-npm-modules.sh'),'--link'],cwd=root,env={**env,'ORO_TEST_FAIL_BUILD':'1'},capture_output=True,text=True,timeout=20)
assert failed.returncode==23,failed.stdout+failed.stderr
assert list((root/'build/npm').glob('.link-staging-*'))==[unowned]
assert not json.loads((root/'.local/sdk/retention.json').read_text())['stages']
assert (live/'preserve').read_text()=='original active installation'
# CI pack-only uses the real npm pack executable and cannot invoke the manager.
import shlex,tarfile
npm=shutil.which('npm')
write('commands/npm','#!/bin/sh\nexec '+shlex.quote(npm)+' "$@"\n',True)
write('bin/sdk-cache.py','raise RuntimeError("release invoked source cache manager")\n')
write('bin/publish-runtime-sdk.py','raise RuntimeError("release invoked source publication")\n')
release=root/'build/npm/release';output=root/'tarballs';output.mkdir()
for directory in ['bin','src','include','uv','lib/x86_64-desktop','objects/x86_64-desktop']:(release/directory).mkdir(parents=True,exist_ok=True)
write('build/npm/release/bin/oroc','#!/bin/sh\nprintf "0.1.8 (deadbeef)\\n"\n',True)
for directory in ['src','include','uv','lib/x86_64-desktop','objects/x86_64-desktop']:write('build/npm/release/'+directory+'/public','public release payload')
release_env={**env,'ORO_NPM_STAGING_HOME':str(release),'ORO_LOCAL_SDK_HOME':str(root/'release-must-not-use-sdk-cache')}
for selection,count in [('--only-platforms',1),('--only-top-level',2)]:
 result=subprocess.run(['bash',str(root/'bin/publish-npm-modules.sh'),selection,'--no-rebuild','--no-remove-oro-home','--dry-run','--pack-destination',str(output)],cwd=root,env=release_env,capture_output=True,text=True,timeout=count*float(env['ORO_NPM_FIXTURE_TIMEOUT_SECONDS']))
 assert result.returncode==0,result.stdout+result.stderr
 assert 'Verified npm package size:' in result.stdout,result.stdout+result.stderr
assert not pathlib.Path(release_env['ORO_LOCAL_SDK_HOME']).exists()
archives=list(output.glob('*.tgz'));assert len(archives)==3,[str(item) for item in archives]
for archive in archives:
 with tarfile.open(archive) as packed:
  members=packed.getmembers()
  assert not any(member.issym() or member.islnk() for member in members)
  assert all(not any(part in member.name for part in ['.local/', 'sdk-cache', 'sdk-lease', 'retention.json', 'generation.json']) for member in members)
  metadata=json.load(packed.extractfile('package/package.json'));assert metadata['version']=='0.1.8'
print('relink-private-staging-keeps-old-command-and-publishes-complete-pair-passed')
`
  const environment = createNpmFixtureEnvironment(mkdtempSync(path.join(tmpdir(), 'oro sdk npm settings ')))
  environment.ORO_NPM_FIXTURE_TIMEOUT_SECONDS = String(NPM_FIXTURE_TIMEOUT / 1000)
  const result = spawnSync('python3', ['-c', program], { cwd: root, env: environment, encoding: 'utf8', timeout: sdkPackTimeout })
  const diagnostic = `SDK publication fixture via python3 in ${root}; timeout=${sdkPackTimeout}ms; ` +
    `status=${result.status}; signal=${result.signal}; error=${result.error?.message || 'none'}\n` +
    `stdout:\n${result.stdout || ''}\nstderr:\n${result.stderr || ''}`
  assert.equal(result.error, undefined, diagnostic)
  assert.equal(result.status, 0, diagnostic)
  assert.match(result.stdout, /relink-private-staging-keeps-old-command-and-publishes-complete-pair-passed/)
})
