import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('compiled CLI requires owned emulator framework readiness and bounds scoped ADB clients', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  function fragment (start, end, after = 0) {
    const first = source.indexOf(start, after)
    const last = source.indexOf(end, first)
    assert.ok(first >= 0 && last > first)
    return source.slice(first, last)
  }
  const helper = fragment('ExecOutput runAndroidAdb (', '\n/// <summary>', source.indexOf('// Direct arguments preserve SDK paths'))
  const selection = fragment('bool selectAndroidDevice (', '\nbool getAdbPath')
  const parsing = fragment('Vector<String> parseAndroidDevices (', '\nString androidAdbCommand')
  const install = fragment('bool installAndroidApp (', '\nbool initAndStartAndroidEmulator')
  const directory = mkdtempSync(path.join(tmpdir(), 'oro cli readiness '))
  const filename = path.join(directory, 'readiness.cc')
  const executable = path.join(directory, 'readiness')
  const apk = path.join(directory, 'public fixture.apk')
  writeFileSync(apk, 'public test input')
  writeFileSync(filename, `
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using String = std::string;
using StringStream = std::stringstream;
template<class T> using Vector = std::vector<T>;
using namespace std::chrono;
namespace fs = std::filesystem;
using Path = fs::path;
struct ExecOutput { String output; int exitCode = 0; };
struct ProcessConfig { bool useDirectArguments = false, rawOutput = false; size_t argumentCount = 0; };
std::map<String, String> environment, settings;
namespace env { String get(const String& name) { return environment[name]; } }
String trim(String value) {
 const auto first = value.find_first_not_of(" \\r\\n\\t");
 return first == String::npos ? "" : value.substr(first, value.find_last_not_of(" \\r\\n\\t") - first + 1);
}
void logInfo(const String&) {} void logWarn(const String&) {} void logError(const String&) {} void logDebug(const String&) {} void logVerbose(const String&) {}
String mode, boot = "1", listing = "List of devices attached\\npublic-phone device\\nemulator-5556 device\\n";
int starts = 0, emulatorKills = 0, adbKills = 0;
Vector<Vector<String>> calls;
class Process {
 public:
 String command, arguments;
 std::function<void(const String&)> stdoutCallback, stderrCallback;
 ProcessConfig config;
 bool closed = true;
 int status = 0;
 Process(const String& command, const String& arguments, const String&, std::function<void(const String&)> out,
   std::function<void(const String&)> err, std::nullptr_t, bool stdinOpen, const ProcessConfig& config)
   : command(command), arguments(arguments), stdoutCallback(out), stderrCallback(err), config(config) {
   assert(!stdinOpen && config.useDirectArguments);
 }
 int open() {
   Vector<String> args; StringStream encoded(arguments); String part;
   while (std::getline(encoded, part, '\\x01')) args.push_back(part);
   assert(args.size() == config.argumentCount);
   if (command == "public-emulator") {
     starts++; assert(args == Vector<String>({"@public-avd", "-gpu", "swiftshader_indirect", "-port", "5584", "-no-snapshot-save"}));
     closed = mode == "exited"; status = closed ? 3 : -1; return 8;
   }
   assert(command == "public-adb"); calls.push_back(args);
   if (args == Vector<String>{"devices"}) { stdoutCallback(listing); return 7; }
   assert(args.size() >= 3 && args[0] == "-s" && args[1] == "emulator-5584");
   if (mode == "flood") { stdoutCallback(String(70000, ' ')); return 7; }
   if (mode == "hang" || (mode == "install-hang" && args[2] == "install")) { closed = false; return 7; }
   if (args[2] == "install") { assert(args.size() == 4 && args[3].find("public fixture.apk") != String::npos); return 7; }
   if (args.size() > 3 && args[3] == "am") {
     if (mode == "activity-error") stdoutCallback("Error type 3\\n");
     return 7;
   }
   if (args.back() == "sys.boot_completed") { stdoutCallback(boot); status = mode == "boot-error" ? 1 : 0; }
   else if (args.back() == "ro.boot.qemu.avd_name") stdoutCallback(mode == "wrong-avd" ? "another-avd" : "public-avd\\r\\n");
   else if (args.back() == "package") stdoutCallback(mode == "no-package" ? "Service package: not found" : "Service package: found\\r\\n");
   else if (args.back() == "activity") stdoutCallback(mode == "no-activity" ? "Service activity: not found" : "Service activity: found\\r\\n");
   else assert(false);
   return 7;
 }
 void kill() { if (command == "public-emulator") emulatorKills++; else adbKills++; closed = true; status = 137; }
 int wait() { assert(closed); return status; }
};
struct AndroidCliState {
 StringStream adb, emulator, adbInstall, adbShellStart; String device, androidHome, avdName, devNull, targetPlatform;
 Process* androidEmulatorProcess = nullptr;
 bool verbose = false; int androidTaskTimeout = 15, androidTaskSleepTime = 1; Path apkPath;
};
${parsing}
ExecOutput runAndroidAdb(const AndroidCliState&, Vector<String>, int, bool select = true);
${selection}
${helper}
#define DEFAULT_ANDROID_MAIN_ACTIVITY_NAME ".MainActivity"
${install}
void reset(AndroidCliState& state) {
 environment.clear(); environment["ORO_ANDROID_EMULATOR_PORT"] = "5584";
 mode = ""; boot = "1"; starts = 0; emulatorKills = 0; adbKills = 0; calls.clear();
 listing = "List of devices attached\\npublic-phone device\\nemulator-5556 device\\n";
 state.adb << "public-adb"; state.emulator << "public-emulator"; state.avdName = "public-avd";
}
void cleanup(AndroidCliState& state) {
 state.androidEmulatorProcess->stdoutCallback("late public output");
 state.androidEmulatorProcess->kill(); state.androidEmulatorProcess->wait(); delete state.androidEmulatorProcess;
}
int main(int argc, char** argv) {
 for (const String value : {"", "0", "true", "1 extra", "1\\nfailed"}) {
  AndroidCliState s; reset(s); boot = value; assert(!startAndroidEmulator(s));
  assert(starts == 1 && emulatorKills == 1 && s.androidEmulatorProcess == nullptr);
 }
 for (const String value : {"1", "1\\r\\n"}) {
  AndroidCliState s; reset(s); boot = value; assert(startAndroidEmulator(s));
  assert(s.device == "emulator-5584" && starts == 1 && emulatorKills == 0); cleanup(s);
 }
 for (const String failure : {"wrong-avd", "no-package", "no-activity", "boot-error", "exited", "hang"}) {
  AndroidCliState s; reset(s); mode = failure; assert(!startAndroidEmulator(s));
  assert(starts == 1 && s.androidEmulatorProcess == nullptr);
  assert(emulatorKills == (failure == "exited" ? 0 : 1));
  if (failure == "hang") assert(adbKills > 0);
 }
 for (const String port : {"5588", "5555", "public", "0"}) {
  AndroidCliState s; reset(s); environment["ORO_ANDROID_EMULATOR_PORT"] = port;
  assert(!startAndroidEmulator(s) && starts == 0 && calls.empty());
 }
 for (const String status : {"device", "offline", "unauthorized"}) {
  AndroidCliState s; reset(s); listing += "emulator-5584\\t" + status + "\\n";
  assert(!startAndroidEmulator(s) && starts == 0 && emulatorKills == 0);
 }
 { AndroidCliState s; reset(s); environment["ANDROID_SERIAL"] = "public-phone"; assert(!startAndroidEmulator(s) && starts == 0 && calls.empty()); }
 { AndroidCliState s; reset(s); assert(!selectAndroidDevice(s)); assert(s.device.empty()); }
 { AndroidCliState s; reset(s); s.targetPlatform = "android-emulator"; assert(selectAndroidDevice(s)); assert(s.device == "emulator-5556"); }
 { AndroidCliState s; reset(s); environment["ANDROID_SERIAL"] = "missing"; assert(!selectAndroidDevice(s)); }
 { AndroidCliState s; reset(s); s.device = "public-phone"; s.targetPlatform = "android-emulator"; assert(!selectAndroidDevice(s)); }
 { AndroidCliState s; reset(s); s.device = "emulator-5584"; mode = "flood"; assert(runAndroidAdb(s, {"shell"}, 100).exitCode == 125); }
 { AndroidCliState s; reset(s); assert(runAndroidAdb(s, {"shell"}, 0).exitCode == 124 && calls.empty()); }
 { AndroidCliState s; reset(s); assert(runAndroidAdb(s, {String("bad\\0value", 9)}, 10).exitCode == 125 && calls.empty()); }
 for (const String failure : {"", "install-hang"}) {
  AndroidCliState s; reset(s); s.device = "emulator-5584"; s.apkPath = argv[1]; s.targetPlatform = "android-emulator"; mode = failure;
  assert(installAndroidApp(s) == failure.empty());
  if (!failure.empty()) assert(adbKills == 1);
 }
 { AndroidCliState s; reset(s); s.device = "emulator-5584"; mode = "activity-error"; assert(!startAndroidApp(s)); }
 std::cout << "CLI boot values, ownership, services, bounded clients, install and start passed\\n";
}
`)
  const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', '-Werror', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [apk], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr || result.stdout)
})
