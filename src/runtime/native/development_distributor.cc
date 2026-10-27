#include "development_distributor.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "web_push.hh"
#include "../json.hh"
#include <libsoup/soup.h>
#include <sodium.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <set>

namespace oro::runtime::native {
  namespace {
    constexpr auto distributorPath = "/org/unifiedpush/Distributor";
    constexpr auto connectorPath = "/org/unifiedpush/Connector";
    constexpr auto connectorInterface = "org.unifiedpush.Connector2";
    constexpr auto xml = R"XML(<node><interface name="org.unifiedpush.Distributor2">
      <method name="Register"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
      <method name="Unregister"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
      </interface></node>)XML";
    std::string text (const JSON::Object& value, const std::string& name) {
      if (!value.has(name) || value.get(name).type != JSON::Type::String)
        throw StateError("EBADMSG");
      return value.get(name).as<JSON::String>().value();
    }
    std::string token () {
      std::array<unsigned char, 24> bytes;
      randombytes_buf(bytes.data(), bytes.size());
      return webPushBase64(bytes);
    }
    std::string digest (const std::string& value) {
      std::array<unsigned char, 32> bytes;
      crypto_hash_sha256(bytes.data(), reinterpret_cast<const unsigned char*>(value.data()), value.size());
      return webPushBase64(bytes);
    }
    RecordBytes encode (const JSON::Any& value) {
      const auto source = value.str();
      return {source.begin(), source.end()};
    }
    JSON::Object frame (const std::string& source) {
      int depth = 0;
      bool quoted = false;
      bool escaped = false;
      for (const auto ch : source) {
        if (quoted) {
          if (escaped)
            escaped = false;
          else if (ch == '\\')
            escaped = true;
          else if (ch == '"')
            quoted = false;
        } else if (ch == '"') {
          quoted = true;
        } else if (ch == '{' || ch == '[') {
          if (++depth > 16)
            throw StateError("E2BIG");
        } else if (ch == '}' || ch == ']') {
          if (--depth < 0)
            throw StateError("EBADMSG");
        }
      }
      if (quoted || depth)
        throw StateError("EBADMSG");
      return JSON::parse(source).as<JSON::Object>();
    }
    JSON::Object decode (const RecordBytes& bytes) {
      try {
        return JSON::parse(std::string(bytes.begin(), bytes.end())).as<JSON::Object>();
      } catch (...) {
        throw StateError("EBADMSG");
      }
    }
    int64_t now () {
      return g_get_real_time() / 1000;
    }
    std::string argument (GVariant* value, const char* name, size_t maximum, bool optional = false) {
      const char* result = nullptr;
      if (!g_variant_lookup(value, name, "&s", &result)) {
        if (optional)
          return "";
        throw StateError("EINVAL");
      }
      if (strlen(result) > maximum || (!optional && !*result))
        throw StateError("EINVAL");
      for (auto ch = reinterpret_cast<const unsigned char*>(result); *ch; ch++)
        if (*ch < 32 || *ch == 127)
          throw StateError("EINVAL");
      return result;
    }
  }  // namespace

  struct DevelopmentDistributor::State : std::enable_shared_from_this<State> {
    std::shared_ptr<DesktopBus> bus;
    RecordStore store;
    std::string origin;
    SoupSession* session = nullptr;
    GCancellable* cancellation = g_cancellable_new();
    bool closing = false;
    bool polling = false;
    bool delivering = false;
    int64_t nextPoll = 0;
    int retrySeconds = 2;
    enum class Connection { Starting, Connected, Retrying } connection = Connection::Starting;
    std::set<std::string> announced;
    std::map<std::string, int64_t> deliveryRetry;

    State(std::shared_ptr<DesktopBus> bus, RecordStore store, std::string origin)
        : bus(std::move(bus)), store(std::move(store)), origin(std::move(origin)) {
      session = soup_session_new_with_options("timeout", 8u, "user-agent", "Oro-Development-UnifiedPush", nullptr);
    }
    ~State() {
      g_object_unref(session);
      g_object_unref(cancellation);
    }
    std::vector<std::pair<std::string, JSON::Object>> registrations () {
      std::vector<std::pair<std::string, JSON::Object>> result;
      for (const auto& [key, bytes] : store.read().records)
        if (key.starts_with("r."))
          result.emplace_back(key, decode(bytes));
      return result;
    }
    std::string endpoint (const JSON::Object& record) {
      return origin + "/" + text(record, "topic") + "?up=1";
    }
    void failure () {
      polling = false;
      nextPoll = now() + retrySeconds * 1000;
      retrySeconds = std::min(30, retrySeconds * 2);
      if (!closing && connection != Connection::Retrying) {
        g_printerr("oroc push distributor: reconnecting; transport or local storage unavailable\n");
        connection = Connection::Retrying;
      }
    }
    void polled () {
      polling = false;
      retrySeconds = 2;
      nextPoll = now() + 2000;
      if (connection != Connection::Connected) {
        g_printerr("oroc push distributor: server connected\n");
        connection = Connection::Connected;
      }
      drain();
    }
    void method (const std::string& sender, const std::string& method, GVariant* parameters,
                 GDBusMethodInvocation* invocation) {
      if (closing)
        throw StateError("ESHUTDOWN");
      GVariant* dictionary = nullptr;
      g_variant_get(parameters, "(@a{sv})", &dictionary);
      DesktopBus::Variant input(dictionary, g_variant_unref);
      if (g_variant_n_children(dictionary) > 4 || g_variant_get_size(dictionary) > 2048)
        throw StateError("E2BIG");
      auto application = argument(dictionary, "service", 255, method == "Unregister");
      const auto reference = argument(dictionary, "token", 100);
      if (application.empty() && method == "Unregister") {
        for (const auto& [key, record] : registrations()) {
          if (text(record, "token") != reference)
            continue;
          try {
            const auto service = text(record, "service");
            if (bus->owner(service) == sender)
              application = service;
          } catch (const StateError& error) {
            if (error.code != "ENOTREADY")
              throw;
          }
        }
        if (application.empty()) {
          GVariantBuilder result;
          g_variant_builder_init(&result, G_VARIANT_TYPE("a{sv}"));
          g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &result));
          return;
        }
      }
      if (!g_dbus_is_name(application.c_str()) || g_dbus_is_unique_name(application.c_str()) ||
          bus->owner(application) != sender)
        throw StateError("EACCES");
      const auto key = "r." + digest(application + ":" + reference);
      auto snapshot = store.read();
      const auto existing = snapshot.records.find(key);
      if (method == "Register") {
        const auto vapid = argument(dictionary, "vapid", 87);
        validateWebPushPublicKey(webPushDecode(vapid));
        argument(dictionary, "description", 100, true);
        JSON::Object record;
        if (existing != snapshot.records.end()) {
          record = decode(existing->second);
          if (text(record, "vapid") != vapid || text(record, "origin") != origin)
            throw StateError("EEXIST");
        } else {
          if (registrations().size() >= 32)
            throw StateError("ENOSPC");
          record = JSON::Object::Entries{{"service", application},
                                         {"token", reference},
                                         {"vapid", vapid},
                                         {"topic", "up" + token()},
                                         {"origin", origin}};
          store.apply(snapshot.revision, {{key, encode(record)}});
        }
        announced.erase(key);
        nextPoll = 0;
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&result, "{sv}", "success", g_variant_new_string("REGISTRATION_SUCCEEDED"));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &result));
      } else if (method == "Unregister") {
        RecordChanges changes{{key, std::nullopt}};
        for (const auto& [id, bytes] : snapshot.records) {
          if (id.starts_with("m.") && text(decode(bytes), "registration") == key)
            changes[id] = std::nullopt;
        }
        store.apply(snapshot.revision, changes);
        announced.erase(key);
        deliveryRetry.erase(key);
        for (const auto& [id, bytes] : changes)
          if (id.starts_with("m."))
            deliveryRetry.erase(id);
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("a{sv}"));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &result));
        GVariantBuilder event;
        g_variant_builder_init(&event, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&event, "{sv}", "token", g_variant_new_string(reference.c_str()));
        bus->callNoReply(application, connectorPath, connectorInterface, "Unregistered",
                         g_variant_new("(a{sv})", &event));
      } else {
        throw StateError("EINVAL");
      }
    }
    void receive (const JSON::Object& message) {
      if (text(message, "event") != "message")
        return;
      const auto id = text(message, "id");
      const auto topic = text(message, "topic");
      if (id.empty() || id.size() > 100 ||
          id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
            std::string::npos ||
          topic.size() > 100)
        throw StateError("EBADMSG");
      if (!message.has("time") || message.get("time").type != JSON::Type::Number) throw StateError("EBADMSG");
      const auto stamp = message.get("time").as<JSON::Number>().value();
      if (!std::isfinite(stamp) || std::floor(stamp) != stamp || stamp < 0 || stamp > 253402300799) throw StateError("EBADMSG");
      const auto serverTime = static_cast<int64_t>(stamp);
      const auto identity = digest(id);
      auto snapshot = store.read();
      JSON::Object recent;
      if (snapshot.records.contains("recent")) recent = decode(snapshot.records.at("recent"));
      if (recent.size() > 1024) throw StateError("EBADMSG");
      if (recent.has(identity) || snapshot.records.contains("d." + identity) || snapshot.records.contains("m." + identity))
        return;
      int64_t cutoff = 0;
      if (snapshot.records.contains("cursor-time")) {
        const auto value = decode(snapshot.records.at("cursor-time"));
        if (!value.has("time") || value.get("time").type != JSON::Type::Number) throw StateError("EBADMSG");
        cutoff = static_cast<int64_t>(value.get("time").as<JSON::Number>().value());
      }
      RecordChanges changes;
      for (const auto& [key, value] : snapshot.records) if (key.starts_with("d.")) {
        recent[key.substr(2)] = std::max(cutoff, serverTime);
        changes[key] = std::nullopt;
      }
      // ntfy can replay the entire marker second. Never evict IDs from that
      // boundary to admit another frame: doing so prevents backlog progress.
      if (serverTime > cutoff) {
        std::vector<std::string> obsolete;
        for (const auto& [key, value] : recent) {
          if (value.type != JSON::Type::Number) throw StateError("EBADMSG");
          if (value.as<JSON::Number>().value() < serverTime) obsolete.push_back(key);
        }
        for (const auto& key : obsolete) recent.data.erase(key);
      }
      if (recent.size() >= 1024) throw StateError("ENOSPC");
      std::string registration;
      for (const auto& [key, record] : registrations())
        if (text(record, "topic") == topic)
          registration = key;
      if (registration.empty())
        return;
      const auto encoding = message.has("encoding") ? text(message, "encoding") : "";
      const auto content = text(message, "message");
      RecordBytes bytes;
      if (encoding == "base64") {
        if (content.size() > 5464)
          throw StateError("E2BIG");
        bytes.resize(4096);
        size_t size = 0;
        if (sodium_base642bin(bytes.data(), bytes.size(), content.c_str(), content.size(), nullptr, &size, nullptr,
                              sodium_base64_VARIANT_ORIGINAL))
          throw StateError("EBADMSG");
        bytes.resize(size);
      } else if (encoding.empty()) {
        bytes = {content.begin(), content.end()};
      } else {
        throw StateError("EBADMSG");
      }
      if (bytes.empty() || bytes.size() > 4096)
        throw StateError("E2BIG");
      size_t pending = 0;
      for (const auto& [key, value] : snapshot.records)
        if (key.starts_with("m."))
          pending++;
      if (pending >= 32)
        throw StateError("ENOSPC");
      const auto record = JSON::Object(JSON::Object::Entries{
        {"registration", registration}, {"id", id}, {"bytes", webPushBase64(bytes)}, {"receivedAt", now()}});
      recent[identity] = std::max(cutoff, serverTime);
      changes["m." + identity] = encode(record);
      changes["recent"] = encode(recent);
      if (serverTime >= cutoff) {
        changes["cursor"] = RecordBytes(id.begin(), id.end());
        changes["cursor-time"] = encode(JSON::Object::Entries{{"time", serverTime}});
      }
      store.apply(snapshot.revision, changes);
    }
    void drain () {
      if (closing || delivering)
        return;
      for (const auto& [key, record] : registrations()) {
        if (announced.contains(key) || deliveryRetry[key] > now())
          continue;
        GVariantBuilder parameters;
        g_variant_builder_init(&parameters, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&parameters, "{sv}", "token", g_variant_new_string(text(record, "token").c_str()));
        g_variant_builder_add(&parameters, "{sv}", "endpoint", g_variant_new_string(endpoint(record).c_str()));
        delivering = true;
        bus->callAsync(text(record, "service"), connectorPath, connectorInterface, "NewEndpoint",
                       g_variant_new("(a{sv})", &parameters), G_VARIANT_TYPE("(a{sv})"),
                       [weak = weak_from_this(), key, topic = text(record, "topic")] (auto, const auto& error) {
                         if (const auto state = weak.lock(); state && !state->closing) {
                           state->delivering = false;
                           state->deliveryRetry[key] = now() + 5000;
                           const auto records = state->store.read();
                           if (error.empty() && records.records.contains(key) &&
                               text(decode(records.records.at(key)), "topic") == topic)
                             state->announced.insert(key);
                         }
                       });
        return;
      }
      const auto snapshot = store.read();
      for (const auto& [key, value] : snapshot.records) {
        if (!key.starts_with("m."))
          continue;
        if (deliveryRetry[key] > now())
          continue;
        const auto message = decode(value);
        const auto receivedAt = message.get("receivedAt").as<JSON::Number>().value();
        if (receivedAt < now() - 604800000) {
          store.apply(snapshot.revision, {{key, std::nullopt}});
          deliveryRetry.erase(key);
          return;
        }
        const auto registration = text(message, "registration");
        if (!snapshot.records.contains(registration))
          continue;
        const auto record = decode(snapshot.records.at(registration));
        const auto payload = webPushDecode(text(message, "bytes"));
        GVariantBuilder parameters;
        g_variant_builder_init(&parameters, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&parameters, "{sv}", "token", g_variant_new_string(text(record, "token").c_str()));
        g_variant_builder_add(&parameters, "{sv}", "id", g_variant_new_string(text(message, "id").c_str()));
        g_variant_builder_add(&parameters, "{sv}", "message",
                              g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, payload.data(), payload.size(), 1));
        delivering = true;
        bus->callAsync(text(record, "service"), connectorPath, connectorInterface, "Message",
                       g_variant_new("(a{sv})", &parameters), G_VARIANT_TYPE("(a{sv})"),
                       [weak = weak_from_this(), key, id = text(message, "id")] (auto response, const auto& error) {
                         if (const auto state = weak.lock(); state && !state->closing) {
                           state->delivering = false;
                           state->deliveryRetry[key] = now() + 5000;
                           GVariant* dictionary = nullptr;
                           const char* acknowledged = nullptr;
                           if (response)
                             g_variant_get(response.get(), "(@a{sv})", &dictionary);
                           DesktopBus::Variant result(dictionary, g_variant_unref);
                           if (error.empty() && dictionary && g_variant_lookup(dictionary, "id", "&s", &acknowledged) &&
                               id == acknowledged) {
                             const auto snapshot = state->store.read();
                             if (snapshot.records.contains(key))
                               state->store.apply(snapshot.revision, {{key, std::nullopt}});
                             state->deliveryRetry.erase(key);
                           }
                         }
                       });
        return;
      }
    }
    struct Poll {
      std::shared_ptr<State> state;
      SoupMessage* message;
      GInputStream* stream = nullptr;
      std::string body;
      size_t frames = 0;
      GCancellable* cancellation = g_cancellable_new();
      gulong parentCancellation = 0;
      guint deadline = 0;
      explicit Poll(std::shared_ptr<State> state, SoupMessage* message) : state(std::move(state)), message(message) {
        parentCancellation =
          g_cancellable_connect(this->state->cancellation, G_CALLBACK(+[] (GCancellable*, gpointer pointer) {
                                  g_cancellable_cancel(static_cast<Poll*>(pointer)->cancellation);
                                }),
                                this, nullptr);
        deadline = g_timeout_add_seconds(
          12,
          +[] (gpointer pointer) -> gboolean {
            auto poll = static_cast<Poll*>(pointer);
            poll->deadline = 0;
            g_cancellable_cancel(poll->cancellation);
            return G_SOURCE_REMOVE;
          },
          this);
      }
      ~Poll() {
        if (deadline)
          g_source_remove(deadline);
        if (parentCancellation)
          g_cancellable_disconnect(state->cancellation, parentCancellation);
        g_object_unref(cancellation);
        if (stream) {
          g_input_stream_close(stream, nullptr, nullptr);
          g_object_unref(stream);
        }
        g_object_unref(message);
      }
    };
    static void read (Poll* poll) {
      g_input_stream_read_bytes_async(
        poll->stream, 4096, G_PRIORITY_DEFAULT, poll->cancellation,
        +[] (GObject* stream, GAsyncResult* result, gpointer pointer) {
          std::unique_ptr<Poll> poll(static_cast<Poll*>(pointer));
          GError* error = nullptr;
          auto bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(stream), result, &error);
          if (!bytes || poll->state->closing || error) {
            if (error)
              g_error_free(error);
            if (bytes)
              g_bytes_unref(bytes);
            poll->state->failure();
            return;
          }
          gsize size = 0;
          const auto data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
          if (size)
            poll->body.append(data, size);
          g_bytes_unref(bytes);
          if (poll->body.size() > 12288) {
            poll->state->failure();
            return;
          }
          try {
            size_t start = 0;
            while (start < poll->body.size()) {
              const auto end = poll->body.find('\n', start);
              if (end == std::string::npos)
                break;
              if (end - start > 8192)
                throw StateError("EBADMSG");
              if (end > start)
                poll->state->receive(frame(poll->body.substr(start, end - start)));
              start = end + 1;
              if (++poll->frames >= 2048) {
                poll->state->polled();
                return;
              }
            }
            poll->body.erase(0, start);
            if (poll->body.size() > 8192 || (!size && !poll->body.empty()))
              throw StateError("EBADMSG");
            if (size) {
              read(poll.release());
              return;
            }
            poll->state->polled();
          } catch (...) {
            poll->state->failure();
          }
        },
        poll);
    }
    void poll () {
      if (closing || polling || now() < nextPoll)
        return;
      auto records = registrations();
      if (records.empty())
        return;
      std::string topics;
      for (const auto& [key, record] : records) {
        if (text(record, "origin") != origin)
          throw StateError("EINVAL");
        if (!topics.empty())
          topics += ',';
        topics += text(record, "topic");
      }
      const auto snapshot = store.read();
      const auto cursor = snapshot.records.find("cursor");
      const auto since =
        cursor == snapshot.records.end() ? "all" : std::string(cursor->second.begin(), cursor->second.end());
      const auto escaped = g_uri_escape_string(since.c_str(), nullptr, false);
      const auto address = origin + "/" + topics + "/json?poll=1&since=" + escaped;
      g_free(escaped);
      const auto message = soup_message_new("GET", address.c_str());
      if (!message)
        throw StateError("EINVAL");
      soup_message_set_flags(message, SOUP_MESSAGE_NO_REDIRECT);
      polling = true;
      const auto request = new Poll(shared_from_this(), message);
      soup_session_send_async(
        session, message, G_PRIORITY_DEFAULT, request->cancellation,
        +[] (GObject* session, GAsyncResult* result, gpointer pointer) {
          std::unique_ptr<Poll> poll(static_cast<Poll*>(pointer));
          GError* error = nullptr;
          poll->stream = soup_session_send_finish(SOUP_SESSION(session), result, &error);
          if (error || !poll->stream || soup_message_get_status(poll->message) != 200 || poll->state->closing) {
            if (error)
              g_error_free(error);
            poll->state->failure();
            return;
          }
          read(poll.release());
        },
        request);
    }
  };

  std::string DevelopmentDistributor::validateOrigin(const std::string& value, bool allowLoopbackHTTP) {
    if (value.empty() || value.size() > 850 || value.find('\0') != std::string::npos)
      throw StateError("EINVAL");
    GError* error = nullptr;
    auto uri = g_uri_parse(value.c_str(), G_URI_FLAGS_NONE, &error);
    if (!uri) {
      if (error)
        g_error_free(error);
      throw StateError("EINVAL");
    }
    const auto scheme = g_uri_get_scheme(uri);
    const auto host = g_uri_get_host(uri);
    const auto path = g_uri_get_path(uri);
    auto address = host ? g_inet_address_new_from_string(host) : nullptr;
    const bool http = scheme && std::string(scheme) == "http" && allowLoopbackHTTP && address &&
                      g_inet_address_get_is_loopback(address);
    const bool valid = scheme && host && *host && (std::string(scheme) == "https" || http) &&
                       !g_uri_get_userinfo(uri) && !g_uri_get_query(uri) && !g_uri_get_fragment(uri) &&
                       (!path || !*path || std::string(path) == "/") && g_uri_get_port(uri) != 0;
    if (address)
      g_object_unref(address);
    g_uri_unref(uri);
    if (!valid)
      throw StateError("EINVAL");
    return value.back() == '/' ? value.substr(0, value.size() - 1) : value;
  }

  DevelopmentDistributor::DevelopmentDistributor(std::shared_ptr<DesktopBus> bus, RecordStore store,
                                                 const std::string& origin, bool allowLoopbackHTTP,
                                                 const std::string& caFile)
      : state(std::make_shared<State>(std::move(bus), std::move(store), validateOrigin(origin, allowLoopbackHTTP))) {
    if (!webPushAvailable())
      throw StateError("ENOSYS");
    // Read before owning a service name; locked/lost keys must never yield an empty fallback database.
    state->store.read();
    if (!caFile.empty()) {
      GError* error = nullptr;
      auto trust = g_tls_file_database_new(caFile.c_str(), &error);
      if (!trust) {
        if (error)
          g_error_free(error);
        throw StateError("EACCES");
      }
      soup_session_set_tls_database(state->session, trust);
      g_object_unref(trust);
    }
    const auto weak = std::weak_ptr<State>(state);
    object = state->bus->exportObject(distributorPath, xml,
                                      [weak] (const auto& sender, const auto& method, auto parameters, auto reply) {
                                        const auto state = weak.lock();
                                        if (!state || state->closing)
                                          throw StateError("ESHUTDOWN");
                                        state->method(sender, method, parameters, reply);
                                      });
    try {
      state->bus->own(service);
    } catch (...) {
      state->bus->unexport(object);
      throw;
    }
    timer = g_timeout_add_full(
      G_PRIORITY_DEFAULT, 100,
      +[] (gpointer pointer) -> gboolean {
        const auto state = static_cast<std::weak_ptr<State>*>(pointer)->lock();
        if (!state || state->closing)
          return G_SOURCE_REMOVE;
        try {
          state->drain();
          state->poll();
        } catch (...) {
          state->failure();
        }
        return G_SOURCE_CONTINUE;
      },
      new std::weak_ptr<State>(state), +[] (gpointer pointer) { delete static_cast<std::weak_ptr<State>*>(pointer); });
  }
  DevelopmentDistributor::~DevelopmentDistributor() {
    shutdown();
  }
  void DevelopmentDistributor::shutdown() {
    if (state->closing)
      return;
    state->closing = true;
    if (timer) {
      g_source_remove(timer);
      timer = 0;
    }
    if (object) {
      state->bus->unexport(object);
      object = 0;
    }
    g_cancellable_cancel(state->cancellation);
    soup_session_abort(state->session);
  }
}  // namespace oro::runtime::native
#endif
