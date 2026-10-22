import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, symlinkSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const compiler = spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0

test('Android native input inventory includes manifests, resources and code without generated outputs', {
  skip: !compiler || process.platform === 'win32'
}, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  const start = source.indexOf('static String androidNormalizeBuildPaths (')
  const end = source.indexOf('static JSON::Object androidApplicationInputs (', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-inputs-'))
  const project = path.join(directory, 'application')
  const output = path.join(project, 'out/android')
  const sdk = path.join(directory, 'SDK with spaces')
  const files = [
    'app/src/main/java/public/Native.kt', 'app/src/main/kotlin/Other.kt',
    'app/src/main/res/values/theme.xml', 'app/src/main/jni/src/public.cc',
    'app/src/main/jni/include/public.hh', 'app/src/main/AndroidManifest.xml',
    'app/build.gradle', 'oroManifest0/src/main/AndroidManifest.xml', 'oroManifest0/build.gradle',
    'app/src/main/libs/arm64-v8a/libpublic.a', 'app/src/main/libs/arm64-v8a/liboro-runtime.a'
  ]
  const excluded = [
    'app/src/main/assets/index.html', 'app/src/main/obj/compiled.o', 'app/build/outputs/classes.dex',
    'app/src/main/jni/include/oro/_user-config-bytes.hh'
  ]
  for (const name of [...files, ...excluded]) {
    const filename = path.join(output, name)
    mkdirSync(path.dirname(filename), { recursive: true })
    writeFileSync(filename, 'public input fixture')
  }
  mkdirSync(path.join(sdk, 'lib/arm64-v8a-android'), { recursive: true })
  writeFileSync(path.join(sdk, 'lib/arm64-v8a-android/liboro-runtime.a'), 'SDK separately inventoried')
  const filename = path.join(directory, 'inputs.cc')
  const binary = path.join(directory, 'inputs')
  writeFileSync(filename, `
    #include <algorithm>
    #include <filesystem>
    #include <iostream>
    #include <iterator>
    #include <map>
    #include <regex>
    #include <stdexcept>
    #include <string>
    #include <vector>
    namespace fs = std::filesystem;
    using Path = fs::path;
    using String = std::string;
    template<class T> using Vector = std::vector<T>;
    ${source.slice(start, end)}
    int main(int argc, char** argv) {
      if (argc != 5) return 2;
      try {
        if (std::string(argv[1]) == "normalize") {
          const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
          std::cout << androidNormalizeBuildPaths(input, argv[2], argv[3], argv[4]);
        } else {
          const Path root(argv[2]);
          for (const auto& file : androidApplicationInputFiles(root)) {
            std::cout << file.lexically_relative(root).generic_string() << '\\n';
          }
        }
      } catch (const std::exception& error) { std::cerr << error.what(); return 3; }
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', binary], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const inventory = spawnSync(binary, ['list', output, project, sdk], { encoding: 'utf8' })
  assert.equal(inventory.status, 0, inventory.stderr)
  assert.deepEqual(inventory.stdout.trim().split('\n'), [...files].sort())
  const input = `-I${sdk}/include ${output}/app ${project}/oro.toml ${project}-other/file`
  const normalized = spawnSync(binary, ['normalize', output, project, sdk], { input, encoding: 'utf8' })
  assert.equal(normalized.status, 0, normalized.stderr)
  assert.equal(normalized.stdout, `-I<runtime>/include <android-build>/app <application>/oro.toml ${project}-other/file`)
  symlinkSync(path.join(output, files[0]), path.join(output, 'app/src/main/java/linked.kt'))
  const rejected = spawnSync(binary, ['list', output, project, sdk], { encoding: 'utf8' })
  assert.equal(rejected.status, 3)
  assert.match(rejected.stderr, /symbolic links/)
})

test('release native-input fingerprint rejects altered configuration, code, archives and unsafe inventories', () => {
  const script = `
import copy,hashlib,json,runpy,sys
validate=runpy.run_path(sys.argv[1])['application_input_evidence']
def digest(value): return hashlib.sha256(json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()).hexdigest()
contract={'schema':1,'sources':[]}
archives=[{'path':'arm64-v8a-android/liboro-runtime.a','sha256':'ab'*32}]
inputs={'debug':False,'androidDistribution':contract,'sdk':'android-37.0','nativePlatform':'android-26','ndkVersion':'29.0.14206865'}
inventory={'schema':1,'scope':'staged-android-inputs-v1','debug':False,'configurationSha256':'cd'*32,
 'runtimeContractSha256':digest(contract),'nativeArchives':archives,'toolchain':{key:inputs[key] for key in ['sdk','nativePlatform','ndkVersion']},
 'files':[{'path':'app/src/main/java/Public.kt','size':12,'sha256':'ef'*32,'normalization':'bytes'}]}
inputs['applicationInputs']={'sha256':digest(inventory),'inventory':inventory}
valid={'inputs':inputs,'nativeArchives':archives}
assert validate(valid)==inputs['applicationInputs']
invalid=[{}, {'inputs':{}}, copy.deepcopy(valid)]
invalid[-1]['inputs']['applicationInputs']['inventory']['configurationSha256']='00'*32
for field,value in [('path','../outside'),('path','/absolute'),('path','C:/absolute'),('path','app/./Public.kt'),
                    ('size',True),('size',-1),('size',8589934593),('sha256','not-a-digest'),('normalization','unknown')]:
 candidate=copy.deepcopy(valid);candidate['inputs']['applicationInputs']['inventory']['files'][0][field]=value;invalid.append(candidate)
candidate=copy.deepcopy(valid);candidate['inputs']['applicationInputs']['inventory']['files']*=2;invalid.append(candidate)
candidate=copy.deepcopy(valid);candidate['inputs']['applicationInputs']['inventory']['nativeArchives'][0]['sha256']='00'*32;invalid.append(candidate)
candidate=copy.deepcopy(valid);candidate['inputs']['androidDistribution']['schema']=2;invalid.append(candidate)
candidate=copy.deepcopy(valid);candidate['inputs']['debug']=True;invalid.append(candidate)
for candidate in invalid:
 try: validate(candidate)
 except ValueError: continue
 raise AssertionError('Altered native application evidence accepted')
for mutation in ['configuration','source','archive','manifest','resource']:
 candidate=copy.deepcopy(valid);evidence=candidate['inputs']['applicationInputs'];item=evidence['inventory']
 if mutation=='configuration': item['configurationSha256']='12'*32
 elif mutation=='archive':
  item['nativeArchives'][0]['sha256']='34'*32;candidate['nativeArchives'][0]['sha256']='34'*32
 else: item['files'][0]['sha256']='56'*32
 evidence['sha256']=digest(item)
 assert validate(candidate)['sha256']!=valid['inputs']['applicationInputs']['sha256']
`
  const result = spawnSync('python3', ['-c', script, fileURLToPath(new URL('../../bin/verify-android-release.py', import.meta.url))], {
    encoding: 'utf8', timeout: 10000
  })
  assert.equal(result.status, 0, result.stderr)
})
