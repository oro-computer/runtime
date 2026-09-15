import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const source = readFileSync(new URL('../../src/runtime/process/win.cc', import.meta.url), 'utf8')
const start = source.indexOf('  auto process_command = command;')
const end = source.indexOf('  const auto wideApplicationName =', start)
assert.ok(start >= 0 && end > start)

test('Windows launches preserve CLI flags, direct arguments and COMSPEC commands', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-windows-arguments-'))
  const filename = path.join(directory, 'arguments.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'arguments.exe' : 'arguments')
  // Execute the production command-line builder without needing a Windows GUI.
  // These are the exact strings passed as lpApplicationName and lpCommandLine.
  writeFileSync(filename, String.raw`
    #include <cassert>
    #include <string>
    #include <vector>
    using String = std::string;
    template <typename T> using Vector = std::vector<T>;
    namespace env {
      String comspec;
      String get (const char*) { return comspec; }
    }
    namespace oro::runtime::string {
      Vector<String> splitc (const String& value, char delimiter) {
        Vector<String> result;
        size_t start = 0;
        for (size_t index = 0; index < value.size(); ++index) {
          if (value[index] == delimiter) {
            result.push_back(value.substr(start, index - start));
            start = index + 1;
          }
        }
        result.push_back(value.substr(start));
        return result;
      }
    }
    struct Process {
      String command, argv, shell, application, commandLine;
      struct { bool useDirectArguments = false; size_t argumentCount = 0; } config;
      bool build () {
        ${source.slice(start, end)}
        application = applicationName;
        commandLine = cmdline;
        return true;
      }
    };
    int main () {
      Process app;
      app.command = R"(C:\Program Files\Oro\tests.exe)";
      app.argv = " --test=./index.js --headless --platform=win32 --from-oroc";
      assert(app.build());
      assert(app.application == app.command);
      assert(app.commandLine == R"("C:\Program Files\Oro\tests.exe"  --test=./index.js --headless --platform=win32 --from-oroc)");
      app.argv = R"(--test="./test entry.js" --headless)";
      assert(app.build());
      assert(app.commandLine == R"("C:\Program Files\Oro\tests.exe" --test="./test entry.js" --headless)");
      app.argv.clear();
      assert(app.build());
      assert(app.commandLine == R"("C:\Program Files\Oro\tests.exe")");

      app.config.useDirectArguments = true;
      app.config.argumentCount = 4;
      app.argv = String("A B") + char(1) + "" + char(1) + "quoted\"value" + char(1) + "C:\\trailing space\\";
      assert(app.build());
      assert(app.commandLine == R"("C:\Program Files\Oro\tests.exe" "A B" "" "quoted\"value" "C:\trailing space\\")");
      app.config.argumentCount = 0;
      app.argv.clear();
      assert(app.build());
      assert(app.commandLine == R"("C:\Program Files\Oro\tests.exe")");
      app.config.argumentCount = 1;
      assert(app.build());
      assert(app.commandLine == R"("C:\Program Files\Oro\tests.exe" "")");
      app.config.argumentCount = 2;
      assert(!app.build());

      Process shell;
      shell.shell = "cmd.exe";
      shell.command = R"CMD("C:\Program Files\Node\node.exe" -e "console.log('hello')")CMD";
      shell.argv = "--flag";
      for (const String comspec : {String(""), String(R"(C:\Windows\System32\cmd.exe)"), String(R"(C:\Custom Shell\cmd.exe)")}) {
        env::comspec = comspec;
        assert(shell.build());
        const auto program = comspec.empty() ? "cmd.exe" : comspec;
        assert(shell.application == program);
        const auto quoted = program.find(' ') == String::npos ? program : "\"" + program + "\"";
        assert(shell.commandLine == quoted + R"CMD( /d /s /c ""C:\Program Files\Node\node.exe" -e "console.log('hello')" --flag")CMD");
      }
    }
  `)
  const built = spawnSync(compiler, ['-std=c++20', '-Werror', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(built.status, 0, built.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
