#include "src/runtime/native/desktop_host.hh"
#include "src/runtime/native/desktop_service.hh"
#include "src/runtime/native/web_push.hh"
#include "src/runtime/env.hh"
#include <sodium.h>
#include <future>
#include <fstream>
#include <cassert>
#include <iostream>

using namespace oro::runtime;
using namespace oro::runtime::native;
extern "C" {
const unsigned char* oro_runtime_init_get_user_config_bytes () {
  return reinterpret_cast<const unsigned char*>("");
}
unsigned int oro_runtime_init_get_user_config_bytes_size () {
  return 0;
}
bool oro_runtime_init_is_debug_enabled () {
  return false;
}
const char* oro_runtime_init_get_dev_host () {
  return "localhost";
}
int oro_runtime_init_get_dev_port () {
  return 0;
}
int oro_runtime_init_get_user_config_format () {
  return 0;
}
}
Runtime::UserConfig configuration () {
  return {{"meta_bundle_identifier", "org.oro.PublicService"},
          {"build_name", "public-service"},
          {"linux_native_service_enabled", "true"}};
}
void initialize (Runtime* runtime) {
  std::ofstream(std::filesystem::path(env::get("ORO_STATE_DIR")) / "host.pid") << getpid();
  LinuxPlatform::fromRuntime(*runtime)->registerExtension(
    "public.service", [] (const auto& action, const auto&, const auto& signal) -> JSON::Any {
      if (action == "wait") {
        std::ofstream(std::filesystem::path(env::get("ORO_STATE_DIR")) / "wait.started") << "public";
        while (!signal.cancelled())
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      return JSON::Object::Entries{{"public", true}};
    });
  std::promise<std::string> checked;
  auto ready = checked.get_future();
  LinuxPlatform::fromRuntime(*runtime)->request(
    "public-startup-probe", "extension", "echo",
    JSON::Object::Entries{{"extension", "public.service"}, {"payload", JSON::Object{}}},
    [&] (const auto&, const auto& error) { checked.set_value(error); });
  assert(ready.get().empty());
}
int main (int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "host")
    return runDesktopNativeHost(configuration(), initialize);
  Runtime::Options options;
  options.autoStart = false;
  options.userConfig = configuration();
  Runtime runtime(options);
  assert(runtime.services.state.init() && runtime.services.secureStorage.start());
  const auto backend = runtime.services.platform.nativeLinux();
  auto loop = g_main_loop_new(nullptr, false);
  std::thread pump([&] { g_main_loop_run(loop); });
  using Response = std::pair<JSON::Any, std::string>;
  auto invoke = [&] (const std::string& key, const std::string& action) {
    auto promise = std::make_shared<std::promise<Response>>();
    const auto future = std::make_shared<std::future<Response>>(promise->get_future());
    backend->request(key, "extension", action,
                     JSON::Object::Entries{{"extension", "public.service"}, {"payload", JSON::Object{}}},
                     [promise] (const auto& data, const auto& error) { promise->set_value({data, error}); });
    return future;
  };
  const auto echo = invoke("echo", "echo");
  assert(echo->wait_for(std::chrono::seconds(15)) == std::future_status::ready);
  const auto result = echo->get();
  if (!result.second.empty())
    std::cerr << "Public native host reply: " << result.second << std::endl;
  assert(result.second.empty() && result.first.as<JSON::Object>().get("public").as<JSON::Boolean>().value());
  const auto pending = invoke("pending", "wait");
  const auto marker = std::filesystem::path(env::get("ORO_STATE_DIR")) / "wait.started";
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!std::filesystem::exists(marker)) {
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  backend->cancelRequest("pending");
  assert(pending->wait_for(std::chrono::seconds(3)) == std::future_status::ready);
  assert(pending->get().second == "ABORT_ERR");
  auto bus = std::make_shared<DesktopBus>();
  constexpr auto service = "org.oro.PublicService.NativeRuntime";
  constexpr auto path = "/dev/oro/NativeRuntime";
  constexpr auto interface = "dev.oro.NativeRuntime1";
  auto probe = bus->call(service, path, interface, "Probe", nullptr, G_VARIANT_TYPE("(s)"));
  const char* generation = nullptr;
  g_variant_get(probe.get(), "(&s)", &generation);
  RecordStore records(runtime.services.state.databasePath() / "native-service-v2.sqlite",
                      runtime.services.secureStorage.nativeRecordKeys(), "service-auth",
                      {"org.oro.PublicService", "runtime", "native-service"});
  auto key = records.read().records.at("hmac");
  auto envelope = [&] (const std::string& nonce, const std::string& epoch, const std::string& feature) {
    return JSON::Object(JSON::Object::Entries{{"generation", epoch},
                                              {"nonce", nonce},
                                              {"issuedAt", g_get_real_time() / 1000},
                                              {"key", "forgery-check"},
                                              {"feature", feature},
                                              {"action", "echo"},
                                              {"options", JSON::Object::Entries{{"extension", "public.service"},
                                                                                {"payload", JSON::Object{}}}}})
      .str();
  };
  auto sign = [&] (const std::string& source) {
    std::array<unsigned char, 32> mac;
    crypto_auth_hmacsha256(mac.data(), reinterpret_cast<const unsigned char*>(source.data()), source.size(),
                           key.data());
    return webPushBase64(mac);
  };
  auto call = [&] (const std::string& source, const std::string& mac, bool allowed) {
    bool accepted = false;
    try {
      bus->call(service, path, interface, "Invoke", g_variant_new("(ss)", source.c_str(), mac.c_str()),
                G_VARIANT_TYPE("(ss)"));
      accepted = true;
    } catch (const StateError&) {
    }
    assert(accepted == allowed);
  };
  const auto valid = envelope("public-once", generation, "extension");
  call(valid, sign(valid), true);
  call(valid, sign(valid), false);
  const auto forged = envelope("public-forged", generation, "extension");
  call(forged, std::string(43, 'A'), false);
  const auto stale = envelope("public-stale", "old-host", "extension");
  call(stale, sign(stale), false);
  const auto credential = envelope("public-interactive", generation, "credentials");
  call(credential, sign(credential), false);
  sodium_memzero(key.data(), key.size());
  backend->shutdown();
  g_main_loop_quit(loop);
  pump.join();
  g_main_loop_unref(loop);
  std::cout << "linux-cold-native-service-authentication-cancellation-replay-and-scope-passed" << std::endl;
}
