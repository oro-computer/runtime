#include "src/runtime/runtime.hh"
#include "src/runtime/native/linux_platform.hh"
#include "src/runtime/native/web_push.hh"

#include <iostream>
#include <future>
#include <atomic>
#include <thread>
#include <cassert>

extern "C" {
  const unsigned char* oro_runtime_init_get_user_config_bytes () { return reinterpret_cast<const unsigned char*>(""); }
  unsigned int oro_runtime_init_get_user_config_bytes_size () { return 0; }
  bool oro_runtime_init_is_debug_enabled () { return false; }
  const char* oro_runtime_init_get_dev_host () { return "localhost"; }
  int oro_runtime_init_get_dev_port () { return 0; }
  int oro_runtime_init_get_user_config_format () { return 0; }
}
using namespace oro::runtime;
using namespace oro::runtime::native;
constexpr auto distributor = "org.unifiedpush.Distributor.OroPublicTest";
constexpr auto distributorPath = "/org/unifiedpush/Distributor";
constexpr auto distributorInterface = "org.unifiedpush.Distributor2";
constexpr auto connector = "org.oro.PublicRuntime.NativePush";
constexpr auto connectorPath = "/org/unifiedpush/Connector";
constexpr auto connectorInterface = "org.unifiedpush.Connector2";
constexpr auto xml = R"XML(<node><interface name="org.unifiedpush.Distributor2">
<method name="Register"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
<method name="Unregister"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
<method name="TestDeliver"><arg type="ay" direction="in"/></method>
<method name="TestEndpoint"><arg type="s" direction="in"/></method>
<method name="TestToken"><arg type="s" direction="out"/></method>
<method name="TestStop"/>
</interface></node>)XML";

std::string text (const JSON::Object& object, const std::string& key) {
  return object.get(key).as<JSON::String>().value();
}
int server () {
  auto bus = std::make_shared<DesktopBus>();
  auto loop = g_main_loop_new(nullptr, false);
  std::string token;
  std::string application;
  auto endpoint = [&](const std::string& address) {
    GVariantBuilder dictionary;
    g_variant_builder_init(&dictionary, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&dictionary, "{sv}", "token", g_variant_new_string(token.c_str()));
    g_variant_builder_add(&dictionary, "{sv}", "endpoint", g_variant_new_string(address.c_str()));
    bus->callNoReply(application, connectorPath, connectorInterface, "NewEndpoint", g_variant_new("(a{sv})", &dictionary));
  };
  bus->exportObject(distributorPath, xml, [&](const auto&, const auto& method, GVariant* parameters, GDBusMethodInvocation* reply) {
    if (method == "Register") {
      GVariant* dictionary = nullptr; g_variant_get(parameters, "(@a{sv})", &dictionary);
      const char* service = nullptr; const char* reference = nullptr; const char* vapid = nullptr;
      if (!g_variant_lookup(dictionary, "service", "&s", &service) || !g_variant_lookup(dictionary, "token", "&s", &reference) ||
          !g_variant_lookup(dictionary, "vapid", "&s", &vapid)) throw StateError("EINVAL");
      validateWebPushPublicKey(webPushDecode(vapid));
      token = reference; application = service; g_variant_unref(dictionary);
      endpoint("https://push.invalid/public/" + token);
      GVariantBuilder result; g_variant_builder_init(&result, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&result, "{sv}", "success", g_variant_new_string("REGISTRATION_SUCCEEDED"));
      g_dbus_method_invocation_return_value(reply, g_variant_new("(a{sv})", &result));
    } else if (method == "Unregister") {
      GVariantBuilder result; g_variant_builder_init(&result, G_VARIANT_TYPE("a{sv}"));
      g_dbus_method_invocation_return_value(reply, g_variant_new("(a{sv})", &result));
    } else if (method == "TestDeliver") {
      GVariant* message = nullptr; g_variant_get(parameters, "(@ay)", &message);
      GVariantBuilder dictionary; g_variant_builder_init(&dictionary, G_VARIANT_TYPE("a{sv}"));
      g_variant_builder_add(&dictionary, "{sv}", "token", g_variant_new_string(token.c_str()));
      g_variant_builder_add(&dictionary, "{sv}", "message", message);
      g_variant_builder_add(&dictionary, "{sv}", "id", g_variant_new_string("public-message"));
      g_variant_unref(message);
      bus->callNoReply(application, connectorPath, connectorInterface, "Message", g_variant_new("(a{sv})", &dictionary));
      g_dbus_method_invocation_return_value(reply, g_variant_new("()"));
    } else if (method == "TestEndpoint") {
      const char* address = nullptr; g_variant_get(parameters, "(&s)", &address); endpoint(address);
      g_dbus_method_invocation_return_value(reply, g_variant_new("()"));
    } else if (method == "TestToken") {
      g_dbus_method_invocation_return_value(reply, g_variant_new("(s)", token.c_str()));
    } else if (method == "TestStop") {
      g_dbus_method_invocation_return_value(reply, g_variant_new("()")); g_main_loop_quit(loop);
    }
  });
  bus->own(distributor);
  std::cout << "public-distributor-ready" << std::endl;
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  return 0;
}

int main (int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "server") return server();
  Runtime::Options options;
  options.autoStart = false;
  options.userConfig = {{"meta_bundle_identifier", "org.oro.PublicRuntime"}, {"build_name", "public-runtime"},
    {"meta_title", "Public runtime"}, {"meta_application_protocol", "oro-public"}, {"launcher_enabled", "true"},
    {"foreground_enabled", "true"}, {"foreground_continuous_enabled", "true"}, {"push_enabled", "true"}, {"background_deferred_enabled", "true"},
    {"push_provider", "unifiedpush"}, {"permissions_allow_notifications", "true"}};
  if (argc == 2 && std::string(argv[1]) == "development")
    options.userConfig["push_development_allow_loopback_http"] = "true";
  Runtime runtime(options);
  if (!runtime.services.state.init() || !runtime.services.secureStorage.start()) return 1;
  const auto backend = runtime.services.platform.nativeLinux();
  auto records = backend->openRecords("product.public", {"authority", "account", "identity"});
  const auto before = records.read();
  records.apply(before.revision, {{"sentinel", RecordBytes {1, 2, 3}}});
  assert(records.read().records.at("sentinel") == (RecordBytes {1, 2, 3}));
  assert(backend->openRecords("product.public", {"authority", "different", "identity"}).read().records.empty());
  std::atomic<int> messages = 0;
  std::atomic<bool> allowed = true;
  DesktopPushHandler handler;
  handler.authorize = [&](const auto& registration, const auto& cancellation) {
    return allowed.load() && registration.authorityRef == "authority" && !cancellation.cancelled();
  };
  handler.message = [&](const auto&, std::span<const unsigned char> payload, const auto& cancellation) {
    if (cancellation.cancelled() || std::string(payload.begin(), payload.end()) != "public-message") return false;
    messages++; return true;
  };
  backend->registerPushHandler("public.alert", handler);
  backend->registerExecutor(false, "public.read", [](const auto&, const auto&) { return ExecutionOutcome::Completed; });
  backend->registerExecutor(true, "public.monitor", [](const auto&, const auto& signal) {
    while (!signal.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return ExecutionOutcome::Unknown;
  });
  backend->registerExtension("public.metadata", [weak = std::weak_ptr<LinuxPlatform>(backend)](const auto& action, const auto& payload, const auto& signal) -> JSON::Any {
    const auto backend = weak.lock();
    if (!backend) throw StateError("ESHUTDOWN");
    if (action == "wait") while (!signal.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (action == "discover") {
      JSON::Array rows;
      for (const auto& value : backend->discoverPushRegistrations({"authority", "account", "public.alert"}))
        rows.push(JSON::Object::Entries{{"id", value.id}, {"kind", value.kind}, {"phase", value.phase}, {"revision", value.revision}});
      assert(backend->discoverPushRegistrations({"authority", "different", "public.alert"}).empty());
      return rows;
    }
    if (action == "retire") return backend->unregisterPushIfCurrent({"authority", "account", "public.alert"},
      payload.get("id").template as<JSON::String>().value(), payload.get("revision").template as<JSON::String>().value());
    return JSON::Object::Entries {{"public", true}};
  });
  DeferredExecutor read;
  read.authorize = [](const auto& task, const auto& signal) { return task.authorityRef == "authority" && !signal.cancelled(); };
  read.execute = [](const auto&, const auto&) { return DeferredOutcome::Completed; };
  backend->registerDeferredRead("public.reconcile", read);
  backend->registerDeferredMaintenance("public.maintenance", {"cleanup"}, true, read);
  backend->initializeNativeServices();
  auto loop = g_main_loop_new(nullptr, false);
  std::thread pump([&] { g_main_loop_run(loop); });
  auto bus = std::make_shared<DesktopBus>();
  std::cout << "public-native-client-ready" << std::endl;
  std::string line;
  uint64_t sequence = 0;
  while (std::getline(std::cin, line)) {
    try {
      const auto command = JSON::parse(line).as<JSON::Object>();
      const auto action = text(command, "action");
      if (action == "__quit") break;
      JSON::Any result;
      if (action == "__count") result = messages.load();
      else if (action == "__network") result = runtime.services.networkStatus.snapshot();
      else if (action == "__restart") {
        backend->shutdown();
        backend->restart();
        backend->initializeNativeServices();
        result = true;
      }
      else if (action == "__allow") { allowed = command.get("allowed").as<JSON::Boolean>().value(); result = true; }
      else if (action == "__deliver" || action == "__forge") {
        const auto payload = webPushDecode(text(command, "payload"));
        auto bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, payload.data(), payload.size(), 1);
        if (action == "__deliver") bus->call(distributor, distributorPath, distributorInterface, "TestDeliver",
          g_variant_new("(@ay)", bytes), G_VARIANT_TYPE("()"));
        else {
          const auto tokenReply = bus->call(distributor, distributorPath, distributorInterface, "TestToken", nullptr, G_VARIANT_TYPE("(s)"));
          const char* token = nullptr; g_variant_get(tokenReply.get(), "(&s)", &token);
          GVariantBuilder dictionary; g_variant_builder_init(&dictionary, G_VARIANT_TYPE("a{sv}"));
          g_variant_builder_add(&dictionary, "{sv}", "token", g_variant_new_string(token));
          g_variant_builder_add(&dictionary, "{sv}", "message", bytes);
          bus->callNoReply(connector, connectorPath, connectorInterface, "Message", g_variant_new("(a{sv})", &dictionary));
        }
        result = true;
      } else if (action == "__endpoint") {
        bus->call(distributor, distributorPath, distributorInterface, "TestEndpoint",
          g_variant_new("(s)", text(command, "endpoint").c_str()), G_VARIANT_TYPE("()"));
        result = true;
      } else if (action == "__cancel") {
        backend->cancelRequest("cancelled-before-admission");
        std::promise<std::pair<JSON::Any, String>> promise;
        try {
          backend->request("cancelled-before-admission", "extension", "metadata", JSON::Object::Entries {},
            [&](const auto& data, const auto& error) { promise.set_value({data, error}); });
          throw StateError("EIO");
        } catch (const StateError& error) { if (error.code != "ABORT_ERR") throw; result = true; }
      } else {
        std::promise<std::pair<JSON::Any, String>> promise;
        auto future = promise.get_future();
        backend->request("public-" + std::to_string(++sequence), text(command, "feature"), action,
          command.has("options") ? command.get("options").as<JSON::Object>() : JSON::Object(),
          [&](const auto& data, const auto& error) { promise.set_value({data, error}); });
        const auto response = future.get();
        if (!response.second.empty()) throw StateError(response.second);
        result = response.first;
      }
      std::cout << JSON::Object(JSON::Object::Entries {{"data", result}}).str() << std::endl;
    } catch (const StateError& error) { std::cout << JSON::Object(JSON::Object::Entries {{"error", error.code}}).str() << std::endl;
    } catch (...) { std::cout << "{\"error\":\"EIO\"}" << std::endl; }
  }
  backend->shutdown();
  g_main_loop_quit(loop); pump.join(); g_main_loop_unref(loop);
}
