import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))

test('Apple scheme replies release native I/O before main-thread delivery and discard cancelled tasks', {
  skip: process.platform !== 'darwin' ? 'Requires the Apple Objective-C runtime and main dispatch queue' : false
}, () => {
  const source = readFileSync(path.join(root, 'src/runtime/webview/scheme_handlers.cc'), 'utf8')
  function fragment (start, end, offset = 0) {
    const first = source.indexOf(start, offset)
    const last = source.indexOf(end, first)
    assert.ok(first >= 0 && last > first)
    return source.slice(first, last)
  }
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-apple-scheme-task-'))
  writeFileSync(path.join(directory, 'task-dispatch.hh'), fragment(
    'static SharedPointer<void> retainSchemeTask (', '#elif ORO_RUNTIME_PLATFORM_LINUX'))
  writeFileSync(path.join(directory, 'task-delivery.hh'), `
    void Request::retainTask () {
      ${fragment('      this->platformRequestOwner = retainSchemeTask(platformRequest);', '    #elif ORO_RUNTIME_PLATFORM_LINUX')}
    }
    void Response::head () {
      ${fragment('        const auto response = this->platformResponse;', '        cdpEmitResponseReceived(this);')}
    }
    void Response::data () {
      const size_t size = 4;
      auto bytes = std::make_shared<unsigned char[]>(size);
      memcpy(bytes.get(), "body", size);
      bool success = false;
      ${fragment('      const auto data = [NSData dataWithBytes: bytes.get() length: size];', '    #elif ORO_RUNTIME_PLATFORM_LINUX', source.indexOf('  bool SchemeHandlers::Response::write (\n'))}
      assert(success);
    }
    void Response::finish () {
      const bool requestActive = true;
      ${fragment('    if (requestActive) {', '  #if !__has_feature(objc_arc)', source.indexOf('  bool SchemeHandlers::Response::finish ()'))}
    }
    void Response::fail () {
      const auto error = [NSError errorWithDomain:@"test" code:1 userInfo:nil];
      ${fragment('    dispatchSchemeTask(', '    // notify fail callback', source.indexOf('  bool SchemeHandlers::Response::fail (const String& reason)'))}
    }
    void finalizeCompletedTask (OROInternalWKURLSchemeHandler* handler, Task task) {
      ${fragment('    // Response notifications queued by native services', '\n  });').replaceAll('self', 'handler')}
    }
  `)
  // Exercise production delivery blocks with real Foundation objects under
  // both ownership modes used by the runtime's conditional retain/release code.
  for (const arc of [false, true]) {
    const executable = path.join(directory, arc ? 'arc' : 'manual')
    const compiled = spawnSync('clang++', [
      '-std=c++20', '-fblocks', '-fsanitize=address', ...(arc ? ['-fobjc-arc'] : []),
      '-framework', 'Foundation', '-I' + directory,
      path.join(root, 'test/fixtures/apple-scheme-task/Replay.mm'), '-o', executable
    ], { encoding: 'utf8', timeout: 30000 })
    assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr)
    const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
    assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout}${result.stderr}`)
  }
})
