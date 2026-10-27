import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
for (const scenario of ['readonly', 'retention', 'readiness', 'descendants', 'recovery']) {
  test(`managed Linux SDK cache: ${scenario}`, { skip: process.platform !== 'linux', timeout: 90000 }, () => {
    const result = spawnSync('python3', ['test/fixtures/sdk-cache/regression.py', scenario], {
      cwd: root, encoding: 'utf8', timeout: 85000
    })
    assert.equal(result.status, 0, result.stdout + result.stderr)
    assert.match(result.stdout, new RegExp(`SDK cache ${scenario} passed`))
  })
}

test('real SDK validators carry build leases across Python, Node and validation descendants', {
  skip: process.platform !== 'linux', timeout: 20000
}, () => {
  const program = String.raw`
import pathlib,tempfile,importlib.util,os,json,fcntl,shutil
source=pathlib.Path.cwd();root=pathlib.Path(tempfile.mkdtemp(prefix='oro-sdk-validator-lease-'))
for directory in ['bin','src/cli','sdk/lib','sdk/bin','api']:(root/directory).mkdir(parents=True,exist_ok=True)
(root/'package.json').write_text('{"type":"module"}')
shutil.copy2(source/'bin/activate-runtime-sdk.py',root/'bin/activate-runtime-sdk.py')
(root/'src/cli/android-sdk.cjs').write_text("module.exports={readAndroidSdk:async()=>({cliSourceFingerprint:'public-source'})}")
(root/'bin/check-android-distribution.js').write_text('''
import assert from 'node:assert/strict'
import fs from 'node:fs'
import cp from 'node:child_process'
const leases=JSON.parse(process.env.ORO_SDK_LEASES)
assert.equal(leases.length,1)
for(const lease of leases)assert.equal(fs.fstatSync(lease.fd).ino,fs.statSync(lease.path).ino)
const child=cp.spawnSync(process.execPath,['-e',"const fs=require('node:fs');for(const l of JSON.parse(process.env.ORO_SDK_LEASES)){if(fs.fstatSync(l.fd).ino!==fs.statSync(l.path).ino)process.exit(23)}"],{env:{},encoding:'utf8'})
assert.equal(child.status,0,child.stderr)
export function validateDistribution() {}
export function cliSourceFingerprint() {return 'public-source'}
console.log('public-validator-inherited-build-lease-passed')
''')
lease=root/'build.lock';fd=os.open(lease,os.O_RDWR|os.O_CREAT,0o600);fcntl.flock(fd,fcntl.LOCK_SH)
os.environ['ORO_SDK_LEASES']=json.dumps([{'fd':fd,'path':str(lease)}])
os.environ['NODE_OPTIONS']='--require '+json.dumps(str(source/'bin/sdk-lease.cjs'))
spec=importlib.util.spec_from_file_location('publication',source/'bin/publish-runtime-sdk.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
assert 'public-validator' in p.validate(root,root/'sdk',root/'api')
(root/'sdk/lib/arm64-v8a-android').mkdir()
assert 'public-validator' in p.validate(root,root/'sdk',root/'api')
compiled=root/'sdk/bin/oroc';compiled.write_text("#!/bin/sh\nprintf '%s' '{\"schema\":1,\"cliSourceFingerprint\":\"public-source\"}'\n");compiled.chmod(0o755)
catalog=root/'sdk/android-distribution.json';catalog.write_text('{"schema":1,"cliSourceFingerprint":"stale"}')
try:p.validate(root,root/'sdk',root/'api')
except RuntimeError as error:assert 'stale' in str(error)
else:raise AssertionError('stale packaged catalog accepted')
catalog.write_text('{"schema":1,"cliSourceFingerprint":"public-source"}')
assert 'public-validator' in p.validate(root,root/'sdk',root/'api')
os.close(fd)
print('desktop-and-Android-real-validator-subprocess-leases-passed')
`
  const result = spawnSync('python3', ['-c', program], { cwd: root, encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /desktop-and-Android-real-validator-subprocess-leases-passed/)
})
