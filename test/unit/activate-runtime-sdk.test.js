import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
test('Linux SDK activation preserves complete history and configuration and rolls back failed validation', {
  skip: process.platform !== 'linux'
}, () => {
  const program = String.raw`
import importlib.util, pathlib, tempfile, json
spec = importlib.util.spec_from_file_location('activation', 'bin/activate-runtime-sdk.py')
activation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(activation)
root = pathlib.Path(tempfile.mkdtemp(prefix='oro-sdk-activation-'))
target = root / 'build/npm/linux'
target.mkdir(parents=True)
(target / '.oro.env').write_text('PUBLIC_CONFIGURATION=preserved\n')
(target / '.oro.env').chmod(0o600)
(target / 'previous-marker').write_text('public previous installation')
sdk = root / 'sdk'
api = root / 'api-package'
sdk.mkdir(); api.mkdir()
(sdk / 'package.json').write_text(json.dumps({'name':'@oro-computer/runtime-linux-x64'}))
(api / 'package.json').write_text(json.dumps({'name':'@oro-computer/runtime'}))
(sdk / 'api').mkdir()
(sdk / 'api/marker').write_text('stale legacy alias')
(api / 'marker').write_text('paired API')
for package in [sdk, api]:
 for item in [package, *package.rglob('*')]: item.chmod(item.stat().st_mode & ~0o222)
activation.validate = lambda *args: 'public validation fixture'
output, target, name, configuration = activation.stage(root, sdk, api)
assert (output / 'api/marker').read_text() == 'paired API'
assert (output / 'api/marker').stat().st_mode & 0o200
assert not (api / 'marker').stat().st_mode & 0o222
activation.activate(root, output, target, name, configuration)
assert (output / 'previous-marker').read_text() == 'public previous installation'
assert (target / 'api/marker').read_text() == 'paired API'
assert (target / '.oro.env').stat().st_mode & 0o777 == 0o600
assert (target / '.oro.env').read_text() == 'PUBLIC_CONFIGURATION=preserved\n'
second, _, _, second_configuration = activation.stage(root, sdk, api)
(target / 'current-marker').write_text('must survive rollback')
def fail(*args): raise RuntimeError('public post-activation validation failure')
activation.validate = fail
try: activation.activate(root, second, target, name, second_configuration)
except RuntimeError: pass
else: raise AssertionError('validation failure accepted')
assert (target / 'current-marker').read_text() == 'must survive rollback'
assert second.is_dir() and output.is_dir()
activation.validate = lambda *args: 'public validation fixture'
third, _, _, third_configuration = activation.stage(root, sdk, api)
exchange = activation.exchange_directories
changed = False
def race(first, second):
 global changed
 exchange(first, second)
 if not changed:
  changed = True
  (first / '.oro.env').write_text('PUBLIC_CONFIGURATION=concurrently_updated\n')
activation.exchange_directories = race
try: activation.activate(root, third, target, name, third_configuration)
except RuntimeError: pass
else: raise AssertionError('concurrent configuration change accepted')
assert (target / '.oro.env').read_text() == 'PUBLIC_CONFIGURATION=concurrently_updated\n'
assert third.is_dir() and output.is_dir()
print('atomic-sdk-activation-history-configuration-and-rollback-passed')
`
  const result = spawnSync('python3', ['-c', program], { cwd: root, encoding: 'utf8', timeout: 30000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /atomic-sdk-activation-history-configuration-and-rollback-passed/)
})
