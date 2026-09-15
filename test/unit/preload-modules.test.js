import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import vm from 'node:vm'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const read = file => readFileSync(new URL(`../../${file}`, import.meta.url), 'utf8')
const source = read('src/runtime/webview/preload.cc')
const constants = source.slice(source.indexOf('  static constexpr'), source.indexOf('  const Preload Preload::compile'))
const compile = source.slice(source.indexOf('  const String& Preload::compile ()'), source.indexOf('  const String& Preload::str ()'))
const declaration = read('src/runtime/webview/preload.hh').replace(/^#include[^\n]*$/gm, '')
const windowSource = read('src/runtime/window/win.cc')
const windowFeatures = windowSource.match(/auto preloadUserScriptSource = webview::Preload::compile\(\{\s*\.features = (webview::Preload::Options::Features \{[\s\S]*?\n\s*\}),/)[1]

for (const windows of [true, false]) {
  test(`Generated ${windows ? 'Windows' : 'WebKit'} preloads import the runtime from the expected URLs`, {
    skip: !compiler ? 'A C++ compiler is required' : false
  }, async () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-preload-modules-'))
    const filename = path.join(directory, 'preload.cc')
    const executable = path.join(directory, process.platform === 'win32' ? 'preload.exe' : 'preload')
    // Compile the real preload generator and declarations. Substitute only its
    // JSON, configuration and string dependencies to avoid linking a GUI app.
    writeFileSync(filename, `
      #include <iostream>
      #include <map>
      #include <regex>
      #include <string>
      #include <type_traits>
      #include <vector>
      #include <nlohmann/json.hpp>
      #define ORO_RUNTIME_PLATFORM_WINDOWS ${windows ? 1 : 0}
      namespace oro::runtime {
        namespace types {
          using String = std::string;
          template <typename T> using Vector = std::vector<T>;
          template <typename K, typename V> using Map = std::map<K, V>;
        }
        using namespace types;
        struct Options {};
        struct UniqueClient { unsigned long long id = 42; };
        namespace config { bool isDebugEnabled () { return true; } }
        namespace JSON {
          struct Value {
            using Entries = std::map<runtime::String, Value>;
            nlohmann::json data;
            nlohmann::json* reference = nullptr;
            Value () : data(nlohmann::json::object()) {}
            Value (const Value& value) = default;
            Value (const Entries& entries) : Value() {
              for (const auto& [key, value] : entries) data[key] = value.json();
            }
            template <typename T> requires (!std::is_base_of_v<Value, T>)
            Value (T value) : data(value) {}
            const nlohmann::json& json () const { return reference ? *reference : data; }
            nlohmann::json& json () { return reference ? *reference : data; }
            Value operator[] (const runtime::String& key) { Value value; value.reference = &json()[key]; return value; }
            template <typename T> Value& as () { return *this; }
            void push (const runtime::String& value) { json().push_back(value); }
            runtime::String str () const { return json().dump(); }
          };
          using Object = Value;
          using String = Value;
          struct Array : Value { Array () : Value(nlohmann::json::array()) {} };
        }
        namespace http {
          struct Header { String name; struct { String str () const { return ""; } } value; };
          using Headers = Vector<Header>;
        }
      }
      ${declaration}
      namespace oro::runtime::webview {
        ${constants}
        String trim (String value) { return value; }
        String decodeURIComponent (String value) { return value; }
        String encodeURIComponent (String value) { return value; }
        String toHeaderCase (String value) { return value; }
        String getDevHost () { return ""; }
        int getDevPort () { return 0; }
        unsigned long long rand64 () { return 99; }
        struct { String os = "${windows ? 'win32' : 'linux'}"; } platform;
        String join (const Vector<String>& values, char delimiter) {
          String result;
          for (const auto& value : values) { if (!result.empty()) result += delimiter; result += value; }
          return result;
        }
        String tmpl (String value, const Map<String, String>& replacements) {
          for (const auto& [key, replacement] : replacements) {
            String token = "{{" + key + "}}";
            size_t cursor = 0;
            while ((cursor = value.find(token, cursor)) != String::npos) {
              value.replace(cursor, token.size(), replacement);
              cursor += replacement.size();
            }
          }
          return value;
        }
        ${compile}
      }
      int main () {
        nlohmann::json result = nlohmann::json::array();
        for (int mode = 0; mode < ${windows ? 4 : 3}; ++mode) {
          oro::runtime::webview::Preload preload;
          preload.options.userConfig["meta_bundle_identifier"] = "app.example";
          preload.options.features.useHTMLMarkup = mode > 0;
          preload.options.features.useESM = mode == 2;
          preload.options.features.useTestScript = true;
          preload.options.argv = {"--test=./index.js"};
          if (mode == 3) {
            using namespace oro::runtime;
            const auto options = preload.options;
            preload.options.features = ${windowFeatures};
          }
          result.push_back(preload.compile());
        }
        std::cout << result.dump();
      }
    `)
    const include = fileURLToPath(new URL('../../include', import.meta.url))
    const built = spawnSync(compiler, ['-std=c++20', '-Werror', '-I', include, filename, '-o', executable], { encoding: 'utf8' })
    assert.equal(built.status, 0, built.stderr)
    const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
    assert.equal(result.status, 0, result.stderr)
    const preloads = JSON.parse(result.stdout)
    for (const [mode, preload] of preloads.entries()) {
      const scripts = mode === 0 || mode === 3
        ? [preload]
        : [...preload.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)].map(match => match[1])
      const specifiers = new Set()
      for (const script of scripts) {
        const checked = spawnSync(process.execPath, ['--check', '--input-type=module'], {
          input: script,
          encoding: 'utf8'
        })
        assert.equal(checked.status, 0, checked.stderr)
        // The generator emits literal specifiers in static and dynamic imports.
        for (const match of script.matchAll(/\b(?:from\s*|import\s*\(\s*|import\s*)['"]([^'"]+)['"]/g)) {
          specifiers.add(match[1])
        }
      }
      for (const name of ['internal/init', 'internal/globals', 'ipc', 'path', 'process']) {
        const expected = windows ? `oro://app.example/oro/${name}.js` : `oro:${name}`
        assert.ok(specifiers.has(expected), `mode ${mode}: missing ${expected}`)
      }
      assert.ok(preload.includes('RUNTIME_TEST_FILENAME'))
      assert.equal(preload.includes('{{'), false, 'all generated placeholders are expanded')
      if (mode === 3) {
        let initialized = 0
        const context = vm.createContext({
          URL,
          console,
          origin: 'oro://app.example',
          location: { href: 'oro://app.example/index.html' },
          document: { readyState: 'complete', addEventListener () {} },
          addEventListener () {},
          async importModule (specifier) {
            if (specifier.endsWith('/internal/init.js')) {
              assert.equal(context.RUNTIME_TEST_FILENAME, 'oro://app.example/index.js',
                'the test entry must exist when internal/init starts without HTML injection')
              initialized++
            }
            if (specifier.endsWith('/internal/globals.js')) return { get: () => Promise.resolve() }
            if (specifier.includes('/module.js')) {
              return { Module: { main: { filename: '/app/index.html', scope: {} }, createRequire: () => () => {} } }
            }
            if (specifier.endsWith('/path.js')) return { dirname: () => '/app' }
            return { default: {} }
          }
        })
        // Execute the generated native script with module loading supplied by the
        // harness. No HTML preload runs, matching the failed WebView2 document.
        vm.runInContext(preload.replace(/\bimport\(/g, 'importModule('), context)
        await new Promise(resolve => setImmediate(resolve))
        assert.equal(initialized, 1)
        assert.equal(typeof context.require, 'function')
        assert.equal(context.__dirname, '/app')
        assert.equal(typeof context.process, 'object')
      }
    }
  })
}
