import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0

test('JNI attachments release only owned threads and transfer ownership once', {
  skip: !available ? 'Clang is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/platform/android/environment.cc', import.meta.url), 'utf8')
  const constructorStart = source.indexOf('  JNIEnvironmentAttachment::JNIEnvironmentAttachment (JavaVM *jvm, int version)')
  const constructorEnd = source.indexOf('  JNIEnvironmentAttachment::JNIEnvironmentAttachment (const JVMEnvironment&', constructorStart)
  const methodsStart = source.indexOf('  JNIEnvironmentAttachment::JNIEnvironmentAttachment (JNIEnvironmentAttachment&&')
  const methodsEnd = source.lastIndexOf('\n}')
  assert.ok(constructorStart >= 0 && constructorEnd > constructorStart && methodsStart > constructorEnd && methodsEnd > methodsStart)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-jni-ownership-'))
  const filename = path.join(directory, 'ownership.cc')
  const executable = path.join(directory, 'ownership')
  writeFileSync(filename, `
    #include <atomic>
    #include <cassert>
    #include <utility>
    constexpr int JNI_OK = 0;
    constexpr int JNI_ERR = -1;
    constexpr int JNI_EDETACHED = -2;
    struct JNIEnv {
      bool ExceptionCheck() { return false; }
      void ExceptionDescribe() {}
    };
    struct JavaVM {
      JNIEnv environment;
      bool connected = false;
      bool fail = false;
      int attachments = 0;
      int detachments = 0;
      int GetEnv(void** value, int) {
        *value = connected ? &environment : nullptr;
        return connected ? JNI_OK : JNI_EDETACHED;
      }
      int AttachCurrentThread(JNIEnv** value, void*) {
        if (fail) return JNI_ERR;
        connected = true;
        attachments++;
        *value = &environment;
        return JNI_OK;
      }
      int DetachCurrentThread() { connected = false; detachments++; return JNI_OK; }
    };
    struct JNIEnvironmentAttachment {
      JavaVM* jvm = nullptr;
      JNIEnv* env = nullptr;
      int status = 0;
      int version = 0;
      std::atomic<bool> attached = false;
      JNIEnvironmentAttachment() = default;
      JNIEnvironmentAttachment(JavaVM*, int);
      JNIEnvironmentAttachment(JNIEnvironmentAttachment&&);
      JNIEnvironmentAttachment& operator=(JNIEnvironmentAttachment&&);
      ~JNIEnvironmentAttachment();
      void attach(JavaVM*, int);
      void detach();
      bool hasException() const;
      void printException() const;
    };
    ${source.slice(constructorStart, constructorEnd)}
    ${source.slice(methodsStart, methodsEnd)}
    int main() {
      JavaVM owned;
      {
        JNIEnvironmentAttachment outer(&owned, 6);
        assert(outer.status == JNI_OK && outer.attached && outer.env);
        { JNIEnvironmentAttachment nested(&owned, 6); assert(!nested.attached); }
        assert(owned.connected && owned.detachments == 0);
      }
      assert(owned.attachments == 1 && owned.detachments == 1);
      JavaVM borrowed;
      borrowed.connected = true;
      { JNIEnvironmentAttachment existing(&borrowed, 6); }
      assert(borrowed.attachments == 0 && borrowed.detachments == 0);
      JavaVM failed;
      failed.fail = true;
      {
        JNIEnvironmentAttachment rejected(&failed, 6);
        assert(rejected.status == JNI_ERR && !rejected.attached && !rejected.env);
      }
      assert(failed.detachments == 0);
      JavaVM original, replacement;
      {
        JNIEnvironmentAttachment first(&original, 6);
        JNIEnvironmentAttachment second(&replacement, 6);
        first = std::move(second);
        assert(original.detachments == 1 && replacement.detachments == 0);
        assert(first.attached && !second.attached && !second.env);
        auto& same = first;
        first = std::move(same);
        assert(first.attached && replacement.detachments == 0);
      }
      assert(original.detachments == 1 && replacement.detachments == 1);
      JavaVM repeated;
      {
        JNIEnvironmentAttachment attachment(&repeated, 6);
        attachment.attach(&repeated, 6);
        assert(repeated.attachments == 2 && repeated.detachments == 1);
        JNIEnvironmentAttachment moved(std::move(attachment));
        assert(moved.attached && !attachment.attached);
      }
      assert(repeated.detachments == 2);
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
