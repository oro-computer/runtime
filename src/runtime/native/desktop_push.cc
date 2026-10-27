#include "desktop_push.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "web_push.hh"
#include <sodium.h>
#include <thread>
#include <algorithm>
#include <cstring>
#include <condition_variable>

namespace oro::runtime::native {
  namespace {
    constexpr auto connectorPath = "/org/unifiedpush/Connector";
    constexpr auto connectorXml = R"XML(<node><interface name="org.unifiedpush.Connector2">
      <method name="Message"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
      <method name="NewEndpoint"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
      <method name="Unregistered"><arg type="a{sv}" direction="in"/><arg type="a{sv}" direction="out"/></method>
      </interface></node>)XML";

    std::string uuid () {
      auto raw = g_uuid_string_random ();
      std::string value (raw);
      g_free (raw);
      return value;
    }
    int64_t now () {
      return std::chrono::duration_cast<std::chrono::milliseconds> (
                 std::chrono::system_clock::now ().time_since_epoch ())
          .count ();
    }
    std::string digest (const std::string& value) {
      std::array<unsigned char, 32> hash;
      crypto_hash_sha256 (hash.data (), reinterpret_cast<const unsigned char*> (value.data ()), value.size ());
      std::array<char, 65> encoded;
      sodium_bin2hex (encoded.data (), encoded.size (), hash.data (), hash.size ());
      return encoded.data ();
    }
    bool opaque (const std::string& value) {
      return !value.empty () && value.size () <= 128 &&
             value.find_first_not_of ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                 std::string::npos;
    }
    std::string string (const JSON::Object& object, const std::string& name) {
      if (!object.has (name) || object.get (name).type != JSON::Type::String)
        throw StateError ("EBADMSG");
      return object.get (name).as<JSON::String> ().value ();
    }
    int64_t number (const JSON::Object& object, const std::string& name, int64_t fallback = 0) {
      if (!object.has (name))
        return fallback;
      if (object.get (name).type != JSON::Type::Number)
        throw StateError ("EBADMSG");
      return static_cast<int64_t> (object.get (name).as<JSON::Number> ().value ());
    }
    bool flag (const JSON::Object& object, const std::string& name) {
      return object.has (name) && object.get (name).type == JSON::Type::Boolean &&
             object.get (name).as<JSON::Boolean> ().value ();
    }
    RecordBytes encode (const JSON::Any& value) {
      const auto text = value.str ();
      return {text.begin (), text.end ()};
    }
    JSON::Object decode (const RecordBytes& value) {
      try {
        return JSON::parse (std::string (value.begin (), value.end ())).as<JSON::Object> ();
      } catch (...) {
        throw StateError ("EBADMSG");
      }
    }
    DesktopPushRegistration reference (const JSON::Object& value) {
      return {string (value, "id"), string (value, "kind"), string (value, "authorityRef"),
              string (value, "accountRef"), string (value, "targetRef")};
    }
    bool endpoint (const std::string& value, bool allowLoopbackHTTP) {
      if (value.empty () || value.size () > 1000 || value.find ('\0') != std::string::npos)
        return false;
      GError* error = nullptr;
      auto uri = g_uri_parse (value.c_str (), G_URI_FLAGS_NONE, &error);
      if (!uri) {
        if (error)
          g_error_free (error);
        return false;
      }
      const auto host = g_uri_get_host (uri);
      const auto address = host ? g_inet_address_new_from_string (host) : nullptr;
      const bool loopback = allowLoopbackHTTP && address && g_inet_address_get_is_loopback (address);
      const auto scheme = g_uri_get_scheme (uri);
      const bool valid = scheme && (std::string (scheme) == "https" || (loopback && std::string (scheme) == "http")) &&
                         g_uri_get_host (uri) && *g_uri_get_host (uri) && !g_uri_get_userinfo (uri) &&
                         !g_uri_get_fragment (uri);
      g_uri_unref (uri);
      if (address)
        g_object_unref (address);
      return valid;
    }
    WebPushKeys keys (const JSON::Object& record) {
      WebPushKeys keys;
      const auto secret = webPushDecode (string (record, "privateKey"));
      const auto publicKey = webPushDecode (string (record, "p256dh"));
      const auto authentication = webPushDecode (string (record, "auth"));
      if (secret.size () != 32 || publicKey.size () != 65 || authentication.size () != 16)
        throw StateError ("EBADMSG");
      std::copy (secret.begin (), secret.end (), keys.privateKey.begin ());
      std::copy (publicKey.begin (), publicKey.end (), keys.publicKey.begin ());
      std::copy (authentication.begin (), authentication.end (), keys.authentication.begin ());
      return keys;
    }
  }  // namespace

  struct DesktopPush::State : std::enable_shared_from_this<State> {
    std::shared_ptr<DesktopBus> bus;
    std::shared_ptr<void> lease;
    RecordStore store;
    std::shared_ptr<PushJournalLedger> journal;
    DesktopPush::JournalFactory journalFactory;
    std::string journalEpoch;
    std::string application;
    Schedule schedule;
    std::function<void ()> changed;
    mutable std::mutex mutex;
    std::map<std::string, DesktopPushHandler> handlers;
    std::map<std::string, std::shared_ptr<ExecutionCancellation>> active;
    bool closing = false;
    bool draining = false;
    bool drainRequested = false;
    bool allowLoopbackHTTP = false;
    std::condition_variable drained;
    std::thread::id worker;

    void refreshJournal () {
      if (!this->journal)
        return;
      const auto epoch = this->journal->epoch ();
      if (epoch == this->journalEpoch)
        return;
      if (this->draining)
        throw StateError ("EBUSY");
      this->store = this->journalFactory (epoch);
      this->journalEpoch = epoch;
    }

    State (std::shared_ptr<DesktopBus> bus, RecordStore store, std::string application, Schedule schedule,
           std::function<void ()> changed)
        : bus (std::move (bus)),
          store (std::move (store)),
          application (std::move (application)),
          schedule (std::move (schedule)),
          changed (std::move (changed)) {
    }

    JSON::Object record (const std::string& id) {
      this->refreshJournal ();
      if (!opaque (id))
        throw StateError ("EINVAL");
      const auto snapshot = this->store.read ();
      const auto found = snapshot.records.find ("r." + digest (id));
      if (found == snapshot.records.end ())
        throw StateError ("ENOENT");
      const auto record = decode (found->second);
      if (string (record, "id") != id)
        throw StateError ("EBADMSG");
      return record;
    }

    PushJournalSnapshot journalSnapshot () {
      if (!this->journal || !this->journalFactory)
        throw StateError ("ENOTSUP");
      this->refreshJournal ();
      PushJournalSnapshot snapshot{this->journalEpoch, "", this->store.opaqueArchive ()};
      try {
        this->store.read ();
      } catch (const StateError& error) {
        if (error.code == "EKEYREVOKED")
          snapshot.reason = "missing-key";
        else if (error.code == "EBADMSG")
          snapshot.reason = "ciphertext-corrupt";
        else
          snapshot.reason = "provider-unavailable";
      }
      return snapshot;
    }

    void save (const std::string& key, const JSON::Object& value) {
      this->refreshJournal ();
      const auto snapshot = this->store.read ();
      RecordChanges changes{{key, encode (value)}};
      if (key.starts_with ("r."))
        changes["v." + key.substr (2)] = encode (JSON::String (uuid ()));
      this->store.apply (snapshot.revision, changes);
    }

    JSON::Object publicRegistration (const JSON::Object& value) const {
      const auto id = string (value, "id");
      size_t pending = 0;
      size_t exhausted = 0;
      for (const auto& [key, bytes] : this->store.read ().records) {
        if (!key.starts_with ("m."))
          continue;
        const auto message = decode (bytes);
        if (string (message, "registration") == id) {
          pending++;
          if (number (message, "attempts") >= 5)
            exhausted++;
        }
      }
      return JSON::Object::Entries{
          {"id", id},
          {"kind", string (value, "kind")},
          {"phase", string (value, "phase")},
          {"endpoint", string (value, "endpoint")},
          {"distributor", string (value, "distributor")},
          {"applicationServerKey", string (value, "applicationServerKey")},
          {"generation", string (value, "generation")},
          {"enrollmentPending", flag (value, "enrollmentPending")},
          {"pendingMessages", pending},
          {"exhaustedMessages", exhausted},
          {"keys", JSON::Object::Entries{{"p256dh", string (value, "p256dh")}, {"auth", string (value, "auth")}}}};
    }

    void notify () {
      std::function<void ()> callback;
      {
        std::lock_guard lock (this->mutex);
        if (!this->closing)
          callback = this->changed;
      }
      if (callback) {
        try {
          callback ();
        } catch (...) {
        }
      }
    }

    void receive (const std::string& sender, const std::string& method, GVariant* parameters,
                  GDBusMethodInvocation* invocation) {
      GVariant* dictionary = nullptr;
      g_variant_get (parameters, "(@a{sv})", &dictionary);
      const auto input = DesktopBus::Variant (dictionary, g_variant_unref);
      const char* token = nullptr;
      GVariantBuilder response;
      g_variant_builder_init (&response, G_VARIANT_TYPE ("a{sv}"));
      {
        std::lock_guard lock (this->mutex);
        this->refreshJournal ();
        if (!this->closing && g_variant_n_children (input.get ()) <= 8 &&
            g_variant_lookup (input.get (), "token", "&s", &token) && std::strlen (token) <= 100) {
          const auto snapshot = this->store.read ();
          for (const auto& [key, bytes] : snapshot.records) {
            if (!key.starts_with ("r."))
              continue;
            auto record = decode (bytes);
            const auto expected = string (record, "token");
            if (expected.size () != std::strlen (token) ||
                sodium_memcmp (expected.data (), token, expected.size ()) != 0)
              continue;
            if (this->bus->owner (string (record, "distributor")) != sender)
              break;
            const auto id = string (record, "id");
            if (method == "NewEndpoint") {
              const char* address = nullptr;
              if (!g_variant_lookup (input.get (), "endpoint", "&s", &address) ||
                  !endpoint (address, this->allowLoopbackHTTP))
                break;
              if (string (record, "endpoint") != address || string (record, "phase") != "registered") {
                record["endpoint"] = address;
                record["generation"] = uuid ();
                record["enrollmentPending"] = true;
                record["enrollmentAttempts"] = 0;
              }
              record["phase"] = "registered";
              this->save (key, record);
            } else if (method == "Message") {
              if (string (record, "phase") != "registered")
                break;
              auto message = g_variant_lookup_value (input.get (), "message", G_VARIANT_TYPE ("ay"));
              if (!message)
                break;
              const auto content = DesktopBus::Variant (message, g_variant_unref);
              gsize size = 0;
              const auto bytes =
                  static_cast<const unsigned char*> (g_variant_get_fixed_array (content.get (), &size, 1));
              if (size < 1 || size > 4096)
                break;
              const char* identifier = "";
              if (g_variant_lookup (input.get (), "id", "&s", &identifier)) {
                if (std::strlen (identifier) > 128)
                  break;
                g_variant_builder_add (&response, "{sv}", "id", g_variant_new_string (identifier));
              }
              const auto messageKey =
                  "m." + digest (expected + std::string (reinterpret_cast<const char*> (bytes), size));
              bool handled = false;
              if (const auto found = snapshot.records.find ("handled"); found != snapshot.records.end ()) {
                const auto markers = decode (found->second);
                handled = markers.has (messageKey);
              }
              if (!handled && !snapshot.records.contains (messageKey)) {
                size_t count = 0;
                for (const auto& [key, value] : snapshot.records)
                  if (key.starts_with ("m."))
                    count++;
                if (count >= 64)
                  throw StateError ("ENOSPC");
                const JSON::Object event (JSON::Object::Entries{{"registration", id},
                                                                {"token", expected},
                                                                {"payload", webPushBase64 ({bytes, size})},
                                                                {"attempts", 0},
                                                                {"receivedAt", now ()}});
                this->save (messageKey, event);
              }
            } else if (method == "Unregistered") {
              RecordChanges remove{{key, std::nullopt}, {"v." + digest (id), std::nullopt}};
              if (const auto found = this->active.find (id); found != this->active.end ())
                found->second->cancel ();
              for (const auto& [key, bytes] : snapshot.records) {
                if (key.starts_with ("m.") && string (decode (bytes), "registration") == id)
                  remove[key] = std::nullopt;
              }
              this->store.apply (snapshot.revision, remove);
            }
            break;
          }
        }
      }
      g_dbus_method_invocation_return_value (invocation, g_variant_new ("(a{sv})", &response));
      this->notify ();
      this->drain ();
    }

    void drain () {
      {
        std::lock_guard lock (this->mutex);
        if (this->closing)
          return;
        this->refreshJournal ();
        if (this->draining) {
          this->drainRequested = true;
          return;
        }
        this->draining = true;
      }
      const auto state = this->shared_from_this ();
      try {
        std::thread ([state] {
          {
            std::lock_guard lock (state->mutex);
            state->worker = std::this_thread::get_id ();
          }
          while (true) {
            {
              std::lock_guard lock (state->mutex);
              state->drainRequested = false;
            }
            try {
              state->process ();
            } catch (...) {
            }
            {
              std::lock_guard lock (state->mutex);
              if (state->closing || !state->drainRequested) {
                state->draining = false;
                state->drained.notify_all ();
                break;
              }
            }
          }
          state->notify ();
        }).detach ();
      } catch (...) {
        std::lock_guard lock (this->mutex);
        this->draining = false;
        this->drained.notify_all ();
        throw StateError ("EAGAIN");
      }
    }

    void process () {
      std::vector<std::string> registrations;
      {
        std::lock_guard lock (this->mutex);
        for (const auto& [key, bytes] : this->store.read ().records)
          if (key.starts_with ("r."))
            registrations.push_back (string (decode (bytes), "id"));
      }
      for (const auto& id : registrations) {
        DesktopPushHandler handler;
        JSON::Object record;
        auto cancellation = std::make_shared<ExecutionCancellation> (60000);
        {
          std::lock_guard lock (this->mutex);
          if (this->closing)
            return;
          try {
            record = this->record (id);
          } catch (const StateError&) {
            continue;
          }
          const auto found = this->handlers.find (string (record, "kind"));
          if (found == this->handlers.end () || string (record, "phase") != "registered")
            continue;
          handler = found->second;
          this->active[id] = cancellation;
        }
        const auto reference = native::reference (record);
        try {
          if (!handler.authorize (reference, *cancellation) || cancellation->cancelled ())
            throw StateError ("EACCES");
          if (flag (record, "enrollmentPending") && number (record, "enrollmentAttempts") < 5 && handler.enrollment) {
            JSON::Object enrollment;
            {
              std::lock_guard lock (this->mutex);
              const auto current = this->record (id);
              if (string (current, "generation") != string (record, "generation"))
                throw StateError ("ESTALE");
              record["enrollmentAttempts"] = number (record, "enrollmentAttempts") + 1;
              this->save ("r." + digest (id), record);
              enrollment = this->publicRegistration (record);
            }
            const auto enrolled = handler.enrollment (reference, enrollment, *cancellation);
            if (enrolled && !cancellation->cancelled () && handler.authorize (reference, *cancellation)) {
              std::lock_guard lock (this->mutex);
              auto current = this->record (id);
              if (string (current, "generation") == string (record, "generation")) {
                current["enrollmentPending"] = false;
                this->save ("r." + digest (id), current);
              }
            }
          }
          std::vector<std::string> messages;
          {
            std::lock_guard lock (this->mutex);
            for (const auto& [key, bytes] : this->store.read ().records) {
              if (key.starts_with ("m.") && string (decode (bytes), "registration") == id)
                messages.push_back (key);
            }
          }
          for (const auto& key : messages) {
            if (cancellation->cancelled () || !handler.authorize (reference, *cancellation))
              break;
            JSON::Object message;
            {
              std::lock_guard lock (this->mutex);
              if (this->closing)
                break;
              const auto snapshot = this->store.read ();
              const auto found = snapshot.records.find (key);
              if (found == snapshot.records.end ())
                continue;
              message = decode (found->second);
              const auto current = this->record (id);
              if (string (message, "token") != string (current, "token") || number (message, "attempts") >= 5)
                continue;
              if (number (message, "receivedAt") < now () - 604800000) {
                this->store.apply (snapshot.revision, {{key, std::nullopt}});
                continue;
              }
              message["attempts"] = number (message, "attempts") + 1;
              this->save (key, message);
              record = current;
            }
            try {
              auto plaintext = decryptWebPush (keys (record), webPushDecode (string (message, "payload")));
              struct Wipe {
                RecordBytes& value;
                ~Wipe () {
                  sodium_memzero (value.data (), value.size ());
                }
              } wipe{plaintext};
              const auto handled = handler.message (reference, plaintext, *cancellation);
              if (!handled || cancellation->cancelled () || !handler.authorize (reference, *cancellation))
                continue;
              std::lock_guard lock (this->mutex);
              if (this->closing || string (this->record (id), "token") != string (message, "token"))
                continue;
              const auto snapshot = this->store.read ();
              if (!snapshot.records.contains (key))
                continue;
              JSON::Object markers;
              if (const auto found = snapshot.records.find ("handled"); found != snapshot.records.end ())
                markers = decode (found->second);
              for (const auto& [name, time] : JSON::Object (markers)) {
                if (time.as<JSON::Number> ().value () < now () - 604800000)
                  markers.data.erase (name);
              }
              while (markers.size () >= 256) {
                auto oldest = markers.data.begin ();
                for (auto i = markers.data.begin (); i != markers.data.end (); ++i) {
                  if (i->second.as<JSON::Number> ().value () < oldest->second.as<JSON::Number> ().value ())
                    oldest = i;
                }
                markers.data.erase (oldest);
              }
              markers[key] = now ();
              this->store.apply (snapshot.revision, {{key, std::nullopt}, {"handled", encode (markers)}});
            } catch (...) {
            }
          }
        } catch (...) {
        }
        {
          std::lock_guard lock (this->mutex);
          if (this->active[id] == cancellation)
            this->active.erase (id);
        }
      }
    }
  };

  DesktopPush::DesktopPush (std::shared_ptr<DesktopBus> bus, RecordStore store, const std::string& application,
                            Schedule schedule, std::shared_ptr<void> lease, std::function<void ()> changed,
                            bool allowLoopbackHTTP, std::shared_ptr<PushJournalLedger> journal, JournalFactory factory)
      : state (std::make_shared<State> (std::move (bus), std::move (store), application, std::move (schedule),
                                        std::move (changed))) {
    if (!lease)
      throw StateError ("EINVAL");
    this->state->lease = std::move (lease);
    this->state->allowLoopbackHTTP = allowLoopbackHTTP;
    this->state->journal = std::move (journal);
    this->state->journalFactory = std::move (factory);
    if (this->state->journal) {
      if (!this->state->journalFactory)
        throw StateError ("EINVAL");
      this->state->journalEpoch = this->state->journal->epoch ();
      this->state->store = this->state->journalFactory (this->state->journalEpoch);
    }
    if (!this->state->bus || !this->state->schedule || !g_dbus_is_name (application.c_str ()))
      throw StateError ("EINVAL");
    const std::weak_ptr<State> weak = this->state;
    this->object = this->state->bus->exportObject (
        connectorPath, connectorXml,
        [weak] (const auto& sender, const auto& method, GVariant* parameters, GDBusMethodInvocation* invocation) {
          const auto state = weak.lock ();
          if (!state) {
            g_dbus_method_invocation_return_dbus_error (invocation, "org.oro.Error.Unavailable",
                                                        "Connector unavailable");
            return;
          }
          if (g_variant_get_size (parameters) > 8192) {
            g_dbus_method_invocation_return_dbus_error (invocation, "org.oro.Error.TooLarge", "Message exceeds limit");
            return;
          }
          const auto input = std::shared_ptr<GVariant> (g_variant_ref (parameters), g_variant_unref);
          const auto reply = std::shared_ptr<GDBusMethodInvocation> (
              static_cast<GDBusMethodInvocation*> (g_object_ref (invocation)), g_object_unref);
          if (!state->schedule ([state, sender, method, input, reply] {
                try {
                  state->receive (sender, method, input.get (), reply.get ());
                } catch (...) {
                  g_dbus_method_invocation_return_dbus_error (reply.get (), "org.oro.Error.Failed",
                                                              "Push request did not complete");
                }
              }))
            g_dbus_method_invocation_return_dbus_error (invocation, "org.oro.Error.Busy",
                                                        "Connector capacity exhausted");
        });
    try {
      this->state->bus->own (application);
    } catch (...) {
      this->state->bus->unexport (this->object);
      this->object = 0;
      throw;
    }
  }

  DesktopPush::~DesktopPush () {
    this->shutdown ();
    this->state->bus->unexport (this->object);
  }

  void DesktopPush::registerHandler (const std::string& name, DesktopPushHandler handler) {
    std::lock_guard lock (this->state->mutex);
    if (name.empty () || name.size () > 64 || name.front () < 'a' || name.front () > 'z' ||
        name.find_first_not_of ("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos || !handler.authorize ||
        !handler.message)
      throw StateError ("EINVAL");
    if (this->state->handlers.contains (name))
      throw StateError ("EEXIST");
    if (this->state->handlers.size () >= 16)
      throw StateError ("ENOSPC");
    this->state->handlers[name] = std::move (handler);
  }

  std::vector<std::string> DesktopPush::distributors () const {
    std::vector<std::string> result;
    for (const auto& name : this->state->bus->names ())
      if (name.starts_with ("org.unifiedpush.Distributor."))
        result.push_back (name);
    return result;
  }

  std::vector<std::string> DesktopPush::registeredKinds () const {
    std::lock_guard lock (this->state->mutex);
    std::vector<std::string> names;
    for (const auto& [name, handler] : this->state->handlers)
      names.push_back (name);
    return names;
  }

  std::string DesktopPush::selectedDistributor () const {
    std::lock_guard lock (this->state->mutex);
    this->state->refreshJournal ();
    const auto snapshot = this->state->store.read ();
    if (const auto found = snapshot.records.find ("distributor"); found != snapshot.records.end ())
      return string (decode (found->second), "name");
    return "";
  }

  void DesktopPush::selectDistributor (const std::string& name) {
    const auto available = this->distributors ();
    if (std::find (available.begin (), available.end (), name) == available.end ())
      throw StateError ("ENOENT");
    std::lock_guard lock (this->state->mutex);
    this->state->refreshJournal ();
    const auto snapshot = this->state->store.read ();
    if (const auto selected = snapshot.records.find ("distributor");
        selected != snapshot.records.end () && string (decode (selected->second), "name") == name)
      return;
    for (const auto& [key, value] : snapshot.records)
      if (key.starts_with ("r."))
        throw StateError ("EBUSY");
    this->state->store.apply (snapshot.revision, {{"distributor", encode (JSON::Object::Entries{{"name", name}})}});
  }

  JSON::Object DesktopPush::registerSubscription (const DesktopPushRegistration& reference, const std::string& vapid,
                                                  const std::string& description) {
    if (!opaque (reference.id) || !opaque (reference.authorityRef) || !opaque (reference.accountRef) ||
        !opaque (reference.targetRef) || vapid.size () != 87 || description.empty () || description.size () > 100 ||
        description.find ('\0') != std::string::npos ||
        !g_utf8_validate (description.c_str (), description.size (), nullptr))
      throw StateError ("EINVAL");
    validateWebPushPublicKey (webPushDecode (vapid));
    const auto distributor = this->selectedDistributor ();
    if (distributor.empty ())
      throw StateError ("ENOTREADY");
    JSON::Object record;
    {
      std::lock_guard lock (this->state->mutex);
      if (this->state->closing)
        throw StateError ("ESHUTDOWN");
      if (!this->state->handlers.contains (reference.kind))
        throw StateError ("ENOSYS");
      this->state->refreshJournal ();
      const auto snapshot = this->state->store.read ();
      const auto selected = snapshot.records.find ("distributor");
      if (selected == snapshot.records.end () || string (decode (selected->second), "name") != distributor)
        throw StateError ("EAGAIN");
      const auto key = "r." + digest (reference.id);
      if (const auto found = snapshot.records.find (key); found != snapshot.records.end ()) {
        record = decode (found->second);
        const auto saved = native::reference (record);
        if (saved.kind != reference.kind || saved.authorityRef != reference.authorityRef ||
            saved.accountRef != reference.accountRef || saved.targetRef != reference.targetRef ||
            string (record, "applicationServerKey") != vapid)
          throw StateError ("EEXIST");
      } else {
        size_t count = 0;
        for (const auto& [key, value] : snapshot.records)
          if (key.starts_with ("r."))
            count++;
        if (count >= 16)
          throw StateError ("ENOSPC");
        const auto material = WebPushKeys::generate ();
        record = JSON::Object::Entries{{"id", reference.id},
                                       {"kind", reference.kind},
                                       {"authorityRef", reference.authorityRef},
                                       {"accountRef", reference.accountRef},
                                       {"targetRef", reference.targetRef},
                                       {"applicationServerKey", vapid},
                                       {"token", uuid ()},
                                       {"generation", uuid ()},
                                       {"phase", "registering"},
                                       {"endpoint", ""},
                                       {"enrollmentPending", true},
                                       {"enrollmentAttempts", 0},
                                       {"distributor", distributor},
                                       {"privateKey", webPushBase64 (material.privateKey)},
                                       {"p256dh", webPushBase64 (material.publicKey)},
                                       {"auth", webPushBase64 (material.authentication)},
                                       {"description", description}};
      }
      this->state->store.apply (
          snapshot.revision, {{key, encode (record)}, {"v." + digest (reference.id), encode (JSON::String (uuid ()))}});
    }
    GVariantBuilder parameters;
    g_variant_builder_init (&parameters, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&parameters, "{sv}", "service", g_variant_new_string (this->state->application.c_str ()));
    g_variant_builder_add (&parameters, "{sv}", "token", g_variant_new_string (string (record, "token").c_str ()));
    g_variant_builder_add (&parameters, "{sv}", "vapid", g_variant_new_string (vapid.c_str ()));
    g_variant_builder_add (&parameters, "{sv}", "description", g_variant_new_string (description.c_str ()));
    try {
      const auto result =
          this->state->bus->call (distributor, "/org/unifiedpush/Distributor", "org.unifiedpush.Distributor2",
                                  "Register", g_variant_new ("(a{sv})", &parameters), G_VARIANT_TYPE ("(a{sv})"));
      GVariant* dictionary = nullptr;
      g_variant_get (result.get (), "(@a{sv})", &dictionary);
      const auto data = DesktopBus::Variant (dictionary, g_variant_unref);
      const char* success = nullptr;
      if (!g_variant_lookup (data.get (), "success", "&s", &success) ||
          std::string (success) != "REGISTRATION_SUCCEEDED")
        throw StateError ("ENOTREADY");
    } catch (...) {
      std::lock_guard lock (this->state->mutex);
      auto current = this->state->record (reference.id);
      if (string (current, "token") == string (record, "token")) {
        current["phase"] = "registration-failed";
        this->state->save ("r." + digest (reference.id), current);
      }
      throw;
    }
    return this->registration (reference.id);
  }

  JSON::Object DesktopPush::registration (const std::string& id) const {
    std::lock_guard lock (this->state->mutex);
    return this->state->publicRegistration (this->state->record (id));
  }

  void DesktopPush::acknowledge (const std::string& id, const std::string& generation) {
    std::lock_guard lock (this->state->mutex);
    auto current = this->state->record (id);
    if (string (current, "phase") != "registered" || string (current, "generation") != generation)
      throw StateError ("EAGAIN");
    current["enrollmentPending"] = false;
    this->state->save ("r." + digest (id), current);
  }

  void DesktopPush::unregister (const std::string& id) {
    this->unregisterScoped (id, nullptr, "");
  }

  std::vector<PushRecoveryRegistration> DesktopPush::discoverRegistrations (const PushRecoveryScope& scope) {
    validatePushRecoveryScope (scope);
    std::lock_guard lock (this->state->mutex);
    if (this->state->closing)
      throw StateError ("ESHUTDOWN");
    this->state->refreshJournal ();
    const auto snapshot = this->state->store.read ();
    RecordChanges changes;
    std::vector<PushRecoveryRegistration> result;
    for (const auto& [key, bytes] : snapshot.records) {
      if (!key.starts_with ("r."))
        continue;
      const auto record = decode (bytes);
      const auto saved = reference (record);
      if (saved.authorityRef != scope.authorityRef || saved.accountRef != scope.accountRef || saved.kind != scope.kind)
        continue;
      const auto versionKey = "v." + digest (saved.id);
      const auto found = snapshot.records.find (versionKey);
      const auto revision =
          found == snapshot.records.end ()
              ? uuid ()
              : JSON::parse (std::string (found->second.begin (), found->second.end ())).as<JSON::String> ().value ();
      if (!g_uuid_string_is_valid (revision.c_str ()))
        throw StateError ("EBADMSG");
      if (found == snapshot.records.end ())
        changes[versionKey] = encode (JSON::String (revision));
      result.push_back ({saved.id, saved.kind, string (record, "phase"), revision});
      if (result.size () > 16)
        throw StateError ("E2BIG");
    }
    if (!changes.empty ())
      this->state->store.apply (snapshot.revision, changes);
    return result;
  }

  bool DesktopPush::unregisterIfCurrent (const PushRecoveryScope& scope, const std::string& id,
                                         const std::string& revision) {
    validatePushRecoveryScope (scope);
    if (!g_uuid_string_is_valid (revision.c_str ()))
      throw StateError ("EINVAL");
    return this->unregisterScoped (id, &scope, revision);
  }

  bool DesktopPush::unregisterScoped (const std::string& id, const PushRecoveryScope* scope,
                                      const std::string& revision) {
    if (!opaque (id))
      throw StateError ("EINVAL");
    JSON::Object record;
    {
      std::lock_guard lock (this->state->mutex);
      if (this->state->closing)
        throw StateError ("ESHUTDOWN");
      this->state->refreshJournal ();
      auto snapshot = this->state->store.read ();
      if (!snapshot.records.contains ("r." + digest (id)))
        return false;
      record = this->state->record (id);
      if (scope) {
        const auto saved = reference (record);
        const auto found = snapshot.records.find ("v." + digest (id));
        if (saved.authorityRef != scope->authorityRef || saved.accountRef != scope->accountRef ||
            saved.kind != scope->kind || found == snapshot.records.end () ||
            std::string (found->second.begin (), found->second.end ()) != JSON::String (revision).str ())
          throw StateError ("EAGAIN");
      }
      if (const auto active = this->state->active.find (id); active != this->state->active.end ())
        active->second->cancel ();
      RecordChanges remove{{"r." + digest (id), std::nullopt}, {"v." + digest (id), std::nullopt}};
      for (const auto& [key, bytes] : snapshot.records)
        if (key.starts_with ("m.") && string (decode (bytes), "registration") == id)
          remove[key] = std::nullopt;
      this->state->store.apply (snapshot.revision, remove);
    }
    GVariantBuilder parameters;
    g_variant_builder_init (&parameters, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&parameters, "{sv}", "token", g_variant_new_string (string (record, "token").c_str ()));
    try {
      this->state->bus->call (string (record, "distributor"), "/org/unifiedpush/Distributor",
                              "org.unifiedpush.Distributor2", "Unregister", g_variant_new ("(a{sv})", &parameters),
                              G_VARIANT_TYPE ("(a{sv})"));
    } catch (...) {
      throw StateError ("EUNKNOWN");
    }
    return true;
  }

  void DesktopPush::retryPending () {
    {
      std::lock_guard lock (this->state->mutex);
      if (this->state->closing)
        throw StateError ("ESHUTDOWN");
      this->state->refreshJournal ();
      const auto snapshot = this->state->store.read ();
      RecordChanges changes;
      for (const auto& [key, bytes] : snapshot.records) {
        if (!key.starts_with ("m.") && !key.starts_with ("r."))
          continue;
        auto value = decode (bytes);
        value[key.starts_with ("m.") ? "attempts" : "enrollmentAttempts"] = 0;
        changes[key] = encode (value);
        if (key.starts_with ("r."))
          changes["v." + key.substr (2)] = encode (JSON::String (uuid ()));
      }
      if (!changes.empty ())
        this->state->store.apply (snapshot.revision, changes);
    }
    this->state->drain ();
  }

  void DesktopPush::resume () {
    std::vector<JSON::Object> saved;
    {
      std::lock_guard lock (this->state->mutex);
      if (this->state->closing)
        throw StateError ("ESHUTDOWN");
      this->state->refreshJournal ();
      for (const auto& [key, bytes] : this->state->store.read ().records) {
        if (key.starts_with ("r."))
          saved.push_back (decode (bytes));
      }
    }
    for (const auto& record : saved) {
      try {
        this->registerSubscription (reference (record), string (record, "applicationServerKey"),
                                    record.has ("description") ? string (record, "description") : "Push notifications");
      } catch (...) {
      }
    }
    this->state->drain ();
  }

  PushJournalInspection DesktopPush::inspectJournalRecovery () {
    std::lock_guard lock (this->state->mutex);
    const auto snapshot = this->state->journalSnapshot ();
    return this->state->journal->inspect (snapshot);
  }
  PushJournalReceipt DesktopPush::quarantineUnreadableJournal (const PushJournalConsent& consent) {
    std::lock_guard lock (this->state->mutex);
    if (this->state->closing)
      throw StateError ("ESHUTDOWN");
    const auto snapshot = this->state->journalSnapshot ();
    try {
      const auto receipt = this->state->journal->quarantine (snapshot, consent, !this->state->draining);
      this->state->store = this->state->journalFactory (receipt.epoch);
      this->state->journalEpoch = receipt.epoch;
      return receipt;
    } catch (const StateError& error) {
      if (error.code == "EBUSY" && consent.installationScopeConfirmed && consent.archiveAndStopLocalDeliveryConfirmed)
        for (const auto& [id, signal] : this->state->active)
          signal->cancel ();
      throw;
    }
  }

  void DesktopPush::shutdown () {
    std::lock_guard lock (this->state->mutex);
    this->state->closing = true;
    this->state->changed = nullptr;
    for (const auto& [id, cancellation] : this->state->active)
      cancellation->cancel ();
  }

  void DesktopPush::drain () {
    std::unique_lock lock (this->state->mutex);
    if (this->state->draining && this->state->worker == std::this_thread::get_id ())
      throw StateError ("EDEADLK");
    this->state->drained.wait (lock, [&] { return !this->state->draining; });
  }
}  // namespace oro::runtime::native
#endif
