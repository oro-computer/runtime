import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0

test('JNI strings own each acquisition, transfer once and preserve UTF-8 including NUL and supplementary characters', {
  skip: !available ? 'Clang is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/platform/android/string_wrap.cc', import.meta.url), 'utf8')
  const header = readFileSync(new URL('../../src/runtime/platform/android/string_wrap.hh', import.meta.url), 'utf8')
  const start = source.indexOf('{')
  const end = source.lastIndexOf('}')
  const classStart = header.indexOf('  class StringWrap {')
  const classEnd = header.indexOf('\n  };', classStart) + 5
  assert.ok(start > 0 && end > start && classStart > 0 && classEnd > classStart)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-jni-strings-'))
  const filename = path.join(directory, 'strings.cc')
  const executable = path.join(directory, 'strings')
  writeFileSync(filename, `
    #include <cassert>
    #include <cstdint>
    #include <limits>
    #include <memory>
    #include <string>
    #include <utility>
    #include <vector>
    using String = std::string;
    template<class T> using Vector = std::vector<T>;
    using jchar = char16_t;
    using jsize = int;
    using jboolean = unsigned char;
    using jstring = std::u16string*;
    struct Object {};
    using jobject = Object*;
    struct JNIEnv {
      std::vector<std::unique_ptr<std::u16string>> references;
      int utf8Pins = 0;
      int utf16Pins = 0;
      bool ExceptionCheck() { return false; }
      jstring NewString(const jchar* units, jsize size) {
        references.emplace_back(std::make_unique<std::u16string>(units, size));
        return references.back().get();
      }
      const char* GetStringUTFChars(jstring, jboolean* copied) {
        if (copied) *copied = false;
        utf8Pins++;
        return "borrowed";
      }
      jsize GetStringUTFLength(jstring) { return 8; }
      void ReleaseStringUTFChars(jstring, const char*) { assert(utf8Pins > 0); utf8Pins--; }
      const jchar* GetStringChars(jstring value, jboolean*) { utf16Pins++; return value->data(); }
      jsize GetStringLength(jstring value) { return value->size(); }
      void ReleaseStringChars(jstring, const jchar*) { assert(utf16Pins > 0); utf16Pins--; }
    };
    ${header.slice(classStart, classEnd)}
    ${source.slice(start + 1, end)}
    int main() {
      JNIEnv first, second;
      String value = {'A', '\\0', 'B', char(0xf0), char(0x9f), char(0x99), char(0x82)};
      {
        StringWrap original(&first, value);
        assert(original.str() == value && first.utf8Pins == 1);
        {
          StringWrap copy(original);
          assert(copy.str() == value && first.utf8Pins == 2);
        }
        assert(first.utf8Pins == 1);
        StringWrap moved(std::move(original));
        assert(!original.j_str() && moved.str() == value && first.utf8Pins == 1);
        StringWrap assigned(&second, "old");
        assigned = moved;
        assert(first.utf8Pins == 2 && second.utf8Pins == 0 && assigned.str() == value);
        assigned = std::move(moved);
        assert(first.utf8Pins == 1 && !moved.j_str());
        auto& self = assigned;
        assigned = self;
        assigned = std::move(self);
        assert(first.utf8Pins == 1 && assigned.str() == value);
        assigned.set(String("replacement"));
        assert(first.utf8Pins == 1 && assigned.str() == "replacement");
        assigned.set(String());
        assert(assigned.j_str() && assigned.str().empty() && first.utf8Pins == 1);
        assigned.release();
        assert(first.utf8Pins == 0 && !assigned.j_str());
      }
      assert(first.utf8Pins == 0 && first.utf16Pins == 0 && second.utf8Pins == 0);
      const jchar unpaired[] = {0xd800};
      {
        StringWrap invalid(&first, first.NewString(unpaired, 1));
        assert(invalid.str() == String({char(0xef), char(0xbf), char(0xbd)}));
      }
      const String maximum = {char(0xf4), char(0x8f), char(0xbf), char(0xbf)};
      { StringWrap boundary(&first, maximum); assert(boundary.str() == maximum); }
      assert(first.utf8Pins == 0 && first.utf16Pins == 0);
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
