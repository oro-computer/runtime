#include "desktop_service.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "linux_platform.hh"
#include "web_push.hh"
#include <sodium.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cmath>

namespace oro::runtime::native {
  namespace {
    constexpr auto path = "/dev/oro/NativeRuntime";
    constexpr auto interface = "dev.oro.NativeRuntime1";
    constexpr auto xml = R"XML(<node><interface name="dev.oro.NativeRuntime1">
      <method name="Probe"><arg type="s" direction="out"/></method>
      <method name="Invoke"><arg type="s" direction="in"/><arg type="s" direction="in"/>
        <arg type="s" direction="out"/><arg type="s" direction="out"/></method>
      <method name="Cancel"><arg type="s" direction="in"/><arg type="s" direction="in"/></method>
      <signal name="Changed"><arg type="s"/></signal>
      </interface></node>)XML";
    std::string text (const JSON::Object& value, const std::string& name, size_t maximum = 256) {
      if (!value.has(name) || value.get(name).type != JSON::Type::String)
        throw StateError("EINVAL");
      const auto result = value.get(name).as<JSON::String>().value();
      if (result.empty() || result.size() > maximum || result.find('\0') != std::string::npos)
        throw StateError("EINVAL");
      return result;
    }
    std::string identifier () {
      auto value = g_uuid_string_random();
      const std::string result(value);
      g_free(value);
      return result;
    }
    int64_t now () {
      return g_get_real_time() / 1000;
    }
    std::string signature (const RecordBytes& key, const std::string& value) {
      if (key.size() != crypto_auth_hmacsha256_KEYBYTES)
        throw StateError("EKEYREVOKED");
      std::array<unsigned char, crypto_auth_hmacsha256_BYTES> output;
      crypto_auth_hmacsha256(output.data(), reinterpret_cast<const unsigned char*>(value.data()), value.size(),
                             key.data());
      return webPushBase64(output);
    }
    void authenticate (const RecordBytes& key, const std::string& value, const std::string& mac) {
      const auto expected = signature(key, value);
      if (mac.size() != expected.size() || sodium_memcmp(mac.data(), expected.data(), expected.size()))
        throw StateError("EACCES");
    }
    void peer (DesktopBus& bus, const std::string& name) {
      auto owner = name;
      if (!g_dbus_is_unique_name(name.c_str()))
        owner = bus.owner(name);
      const auto result =
        bus.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID",
                 g_variant_new("(s)", owner.c_str()), G_VARIANT_TYPE("(u)"));
      guint32 pid = 0;
      g_variant_get(result.get(), "(u)", &pid);
      struct stat actual {
      }, expected{};
      const auto executable = "/proc/" + std::to_string(pid) + "/exe";
      if (stat(executable.c_str(), &actual) || stat("/proc/self/exe", &expected) || actual.st_dev != expected.st_dev ||
          actual.st_ino != expected.st_ino)
        throw StateError("ESTALE");
      const auto identity =
        bus.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixUser",
                 g_variant_new("(s)", owner.c_str()), G_VARIANT_TYPE("(u)"));
      guint32 uid = 0;
      g_variant_get(identity.get(), "(u)", &uid);
      if (uid != getuid())
        throw StateError("EACCES");
    }
    bool delegated (const std::string& feature) {
      return feature == "foreground" || feature == "push" || feature == "notifications" || feature == "deferred" ||
             feature == "launcher" || feature == "extension";
    }
    JSON::Object decode (const std::string& source) {
      try {
        return JSON::parse(source).as<JSON::Object>();
      } catch (...) {
        throw StateError("EBADMSG");
      }
    }
  }  // namespace

  struct DesktopService::State : std::enable_shared_from_this<State> {
    std::shared_ptr<DesktopBus> bus;
    std::weak_ptr<LinuxPlatform> backend;
    RecordBytes secret;
    std::string generation = identifier();
    std::map<std::string, std::chrono::steady_clock::time_point> nonces;
    std::atomic<bool> closing = false;
    ~State() {
      sodium_memzero(secret.data(), secret.size());
    }

    JSON::Object verify (const std::string& sender, const std::string& source, const std::string& mac) {
      if (closing)
        throw StateError("ESHUTDOWN");
      if (source.size() > 262144 || mac.size() != 43)
        throw StateError("E2BIG");
      authenticate(secret, source, mac);
      peer(*bus, sender);
      const auto value = decode(source);
      if (value.size() != 7 || text(value, "generation", 36) != generation || !value.has("issuedAt") ||
          value.get("issuedAt").type != JSON::Type::Number)
        throw StateError("EACCES");
      const auto issued = value.get("issuedAt").as<JSON::Number>().value();
      if (!std::isfinite(issued) || std::abs(issued - now()) > 95000)
        throw StateError("ESTALE");
      const auto nonce = text(value, "nonce", 36);
      const auto timestamp = std::chrono::steady_clock::now();
      for (auto i = nonces.begin(); i != nonces.end();) {
        if (i->second <= timestamp)
          i = nonces.erase(i);
        else
          ++i;
      }
      if (nonces.contains(nonce))
        throw StateError("EACCES");
      if (nonces.size() >= 4096)
        throw StateError("EBUSY");
      nonces[nonce] = timestamp + std::chrono::seconds(95);
      return value;
    }
    void method (const std::string& sender, const std::string& method, GVariant* parameters,
                 GDBusMethodInvocation* invocation) {
      if (method == "Probe") {
        peer(*bus, sender);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", generation.c_str()));
        return;
      }
      const char* source = nullptr;
      const char* mac = nullptr;
      g_variant_get(parameters, "(&s&s)", &source, &mac);
      const auto value = verify(sender, source, mac);
      const auto native = backend.lock();
      if (!native)
        throw StateError("ESHUTDOWN");
      const auto key = "host:" + sender + ":" + text(value, "key");
      const auto feature = text(value, "feature", 32);
      const auto action = text(value, "action", 64);
      if (method == "Cancel") {
        if (feature != "cancel" || action != "cancel")
          throw StateError("EINVAL");
        native->cancelRequest(key);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
        return;
      }
      if (!delegated(feature) || !value.has("options") || value.get("options").type != JSON::Type::Object)
        throw StateError("EACCES");
      const auto nonce = text(value, "nonce", 36);
      const auto weak = weak_from_this();
      g_log("OroRuntimeNative", G_LOG_LEVEL_DEBUG, "Native host accepted an authenticated request");
      auto reply =
        std::shared_ptr<GDBusMethodInvocation>(G_DBUS_METHOD_INVOCATION(g_object_ref(invocation)), g_object_unref);
      native->request(key, feature, action, value.get("options").as<JSON::Object>(),
                      [weak, nonce, reply] (const auto& data, const auto& error) {
                        const auto state = weak.lock();
                        if (!state || state->closing) {
                          g_dbus_method_invocation_return_dbus_error(reply.get(), "dev.oro.Error.Stopped",
                                                                     "Native host stopped");
                          return;
                        }
                        const auto source =
                          JSON::Object(JSON::Object::Entries{{"nonce", nonce}, {"data", data}, {"error", error}}).str();
                        g_log("OroRuntimeNative", G_LOG_LEVEL_DEBUG, "Native host completed a request: %s",
                              error.empty() ? "success" : "operation error");
                        g_dbus_method_invocation_return_value(
                          reply.get(), g_variant_new("(ss)", source.c_str(), signature(state->secret, source).c_str()));
                      });
    }
  };

  DesktopService::DesktopService(std::shared_ptr<DesktopBus> bus, RecordStore records, const std::string& service,
                                 std::weak_ptr<LinuxPlatform> backend)
      : state(std::make_shared<State>()) {
    state->bus = std::move(bus);
    state->backend = std::move(backend);
    auto snapshot = records.read();
    if (!snapshot.records.contains("hmac")) {
      RecordBytes bytes(crypto_auth_hmacsha256_KEYBYTES);
      randombytes_buf(bytes.data(), bytes.size());
      snapshot = records.apply(snapshot.revision, {{"hmac", bytes}});
      sodium_memzero(bytes.data(), bytes.size());
    }
    state->secret = snapshot.records.at("hmac");
    if (state->secret.size() != crypto_auth_hmacsha256_KEYBYTES)
      throw StateError("EKEYREVOKED");
    const auto weak = std::weak_ptr<State>(state);
    object =
      state->bus->exportObject(path, xml, [weak] (const auto& sender, const auto& method, auto parameters, auto reply) {
        const auto state = weak.lock();
        if (!state || state->closing)
          throw StateError("ESHUTDOWN");
        state->method(sender, method, parameters, reply);
      });
    // Publish readiness only after the key and exported methods are available.
    try {
      state->bus->own(service);
    } catch (...) {
      state->bus->unexport(object);
      object = 0;
      throw;
    }
  }
  DesktopService::~DesktopService() {
    shutdown();
  }
  void DesktopService::changed(const std::string& name) {
    if (!state->closing && (name == "pushchange" || name == "launcherchange" || name == "notificationresponse"))
      state->bus->emitSignal(path, interface, "Changed", g_variant_new("(s)", name.c_str()));
  }
  void DesktopService::shutdown() {
    state->closing = true;
    if (object) {
      state->bus->unexport(object);
      object = 0;
    }
  }

  struct DesktopServiceClient::State {
    std::shared_ptr<DesktopBus> bus;
    RecordStore store;
    std::string service;
    std::mutex mutex;
    RecordBytes secret;
    std::string generation;
    guint subscription = 0;
    State(std::shared_ptr<DesktopBus> bus, RecordStore store, std::string service)
        : bus(std::move(bus)), store(std::move(store)), service(std::move(service)) {
    }
    ~State() {
      if (subscription)
        bus->unsubscribe(subscription);
      sodium_memzero(secret.data(), secret.size());
    }
    std::pair<std::string, std::string> envelope (const std::string& key, const std::string& feature,
                                                  const std::string& action, const JSON::Object& options) {
      if (secret.size() != crypto_auth_hmacsha256_KEYBYTES || generation.empty())
        throw StateError("ENOTREADY");
      const auto nonce = identifier();
      return {JSON::Object(JSON::Object::Entries{{"generation", generation},
                                                 {"nonce", nonce},
                                                 {"issuedAt", now()},
                                                 {"key", key},
                                                 {"feature", feature},
                                                 {"action", action},
                                                 {"options", options}})
                .str(),
              nonce};
    }
  };
  DesktopServiceClient::DesktopServiceClient(std::shared_ptr<DesktopBus> bus, RecordStore store,
                                             const std::string& service,
                                             std::function<void(const std::string&)> changed)
      : state(std::make_shared<State>(std::move(bus), std::move(store), service)) {
    if (changed) {
      const auto weak = std::weak_ptr<State>(state);
      state->subscription =
        state->bus->subscribe("", path, interface, "Changed", [weak, changed] (const auto& sender, GVariant* value) {
          const auto state = weak.lock();
          if (!state || g_variant_get_size(value) > 256 || state->bus->owner(state->service) != sender ||
              !g_variant_is_of_type(value, G_VARIANT_TYPE("(s)")))
            return;
          const char* name = nullptr;
          g_variant_get(value, "(&s)", &name);
          if (strlen(name) <= 32 && (std::string(name) == "pushchange" || std::string(name) == "launcherchange" ||
                                     std::string(name) == "notificationresponse"))
            changed(name);
        });
    }
  }
  DesktopServiceClient::~DesktopServiceClient() = default;
  JSON::Any DesktopServiceClient::request(const std::string& key, const std::string& feature, const std::string& action,
                                          const JSON::Object& options, const ExecutionCancellation& signal) {
    if (!delegated(feature) || signal.cancelled())
      throw StateError("ABORT_ERR");
    const auto probe =
      state->bus->call(state->service, path, interface, "Probe", nullptr, G_VARIANT_TYPE("(s)"), 10000);
    g_log("OroRuntimeNative", G_LOG_LEVEL_DEBUG, "Native host probe completed");
    peer(*state->bus, state->service);
    g_log("OroRuntimeNative", G_LOG_LEVEL_DEBUG, "Native host peer verified");
    const char* generation = nullptr;
    g_variant_get(probe.get(), "(&s)", &generation);
    std::string source;
    std::string nonce;
    std::string mac;
    const auto snapshot = state->store.read();
    g_log("OroRuntimeNative", G_LOG_LEVEL_DEBUG, "Native host authentication key read");
    if (!snapshot.records.contains("hmac"))
      throw StateError("EKEYREVOKED");
    {
      std::lock_guard lock(state->mutex);
      state->generation = generation;
      sodium_memzero(state->secret.data(), state->secret.size());
      state->secret = snapshot.records.at("hmac");
      const auto value = state->envelope(key, feature, action, options);
      source = value.first;
      nonce = value.second;
      mac = signature(state->secret, source);
    }
    if (signal.cancelled())
      throw StateError("ABORT_ERR");
    const auto response =
      state->bus->call(state->service, path, interface, "Invoke", g_variant_new("(ss)", source.c_str(), mac.c_str()),
                       G_VARIANT_TYPE("(ss)"), 95000);
    g_log("OroRuntimeNative", G_LOG_LEVEL_DEBUG, "Native host response received");
    const char* reply = nullptr;
    const char* replySignature = nullptr;
    g_variant_get(response.get(), "(&s&s)", &reply, &replySignature);
    if (strlen(reply) > 525312)
      throw StateError("E2BIG");
    {
      std::lock_guard lock(state->mutex);
      authenticate(state->secret, reply, replySignature);
    }
    const auto result = decode(reply);
    if (text(result, "nonce", 36) != nonce || !result.has("data") || !result.has("error") ||
        result.get("error").type != JSON::Type::String)
      throw StateError("EBADMSG");
    const auto error = result.get("error").as<JSON::String>().value();
    if (!error.empty())
      throw StateError(error);
    if (signal.cancelled())
      throw StateError("ABORT_ERR");
    return result.get("data");
  }
  void DesktopServiceClient::cancel(const std::string& key) {
    std::lock_guard lock(state->mutex);
    if (state->secret.empty() || state->generation.empty())
      return;
    const auto value = state->envelope(key, "cancel", "cancel", JSON::Object{});
    const auto mac = signature(state->secret, value.first);
    state->bus->callNoReply(state->service, path, interface, "Cancel",
                            g_variant_new("(ss)", value.first.c_str(), mac.c_str()));
  }
}  // namespace oro::runtime::native
#endif
