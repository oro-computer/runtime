#include "linux_platform.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "desktop_notifications.hh"
#include "desktop_launcher.hh"
#include "web_push.hh"
#include "desktop_credentials.hh"
#include "protected_keys.hh"
#include "desktop_service.hh"
#include "../env.hh"
#include "../core/services/platform.hh"
#include "../runtime.hh"
#include "../../extension/extension.hh"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <condition_variable>
#include <cmath>
#include <regex>
#include <fstream>

namespace oro::runtime::native {
  namespace {
    std::string text (const JSON::Object& value, const std::string& key, size_t maximum,
                      const std::string& fallback = "") {
      if (!value.has(key))
        return fallback;
      const auto field = value.get(key);
      if (field.type != JSON::Type::String)
        throw StateError("EINVAL");
      const auto result = field.as<JSON::String>().value();
      if (result.size() > maximum || result.find('\0') != std::string::npos ||
          !g_utf8_validate(result.c_str(), result.size(), nullptr))
        throw StateError("EINVAL");
      return result;
    }

    bool flag (const JSON::Object& value, const std::string& key, bool fallback = false) {
      if (!value.has(key))
        return fallback;
      if (value.get(key).type != JSON::Type::Boolean)
        throw StateError("EINVAL");
      return value.get(key).as<JSON::Boolean>().value();
    }

    void fields (const JSON::Object& value, std::initializer_list<std::string> allowed) {
      for (const auto& [key, field] : value) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
          throw StateError("EINVAL");
      }
    }

    int64_t integer (const JSON::Object& value, const std::string& key, int64_t fallback) {
      if (!value.has(key))
        return fallback;
      if (value.get(key).type != JSON::Type::Number)
        throw StateError("EINVAL");
      const auto number = value.get(key).as<JSON::Number>().value();
      if (!std::isfinite(number) || std::floor(number) != number || number < 0 || number > 604800000)
        throw StateError("EINVAL");
      return static_cast<int64_t>(number);
    }

    bool kind (const std::string& value, bool extension = false) {
      if (value.empty() || value.size() > 64 || value.front() < 'a' || value.front() > 'z')
        return false;
      return value.find_first_not_of(extension ? "abcdefghijklmnopqrstuvwxyz0123456789._-"
                                               : "abcdefghijklmnopqrstuvwxyz0123456789.-") == std::string::npos;
    }

    JSON::Object publicStatus (const ExecutionStatus& value, bool continuous) {
      JSON::Object result(JSON::Object::Entries{{"id", value.task.id},
                                                {"kind", value.task.kind},
                                                {"generation", value.generation},
                                                {"phase", value.phase},
                                                {"outcome", value.outcome},
                                                {"updatedAt", value.updatedAt}});
      if (!continuous)
        result["durationMs"] = value.durationMs;
      return result;
    }

    struct Lease {
      int descriptor = -1;
      explicit Lease(const std::filesystem::path& path) {
        descriptor = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (descriptor < 0)
          throw StateError("EACCES");
        struct stat info {};
        if (fstat(descriptor, &info) || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_nlink != 1) {
          close(descriptor);
          descriptor = -1;
          throw StateError("EACCES");
        }
        if (flock(descriptor, LOCK_EX | LOCK_NB)) {
          close(descriptor);
          descriptor = -1;
          throw StateError("EBUSY");
        }
      }
      ~Lease() {
        if (descriptor >= 0)
          close(descriptor);
      }
    };

    RecordBytes encoded (const JSON::Any& value) {
      const auto source = value.str();
      return {source.begin(), source.end()};
    }

    JSON::Object decoded (const RecordBytes& value) {
      try {
        return JSON::parse(std::string(value.begin(), value.end())).as<JSON::Object>();
      } catch (...) {
        throw StateError("EBADMSG");
      }
    }
  }  // namespace

  struct LinuxPlatform::State : std::enable_shared_from_this<State> {
    core::services::Platform& service;
    std::mutex mutex;
    std::condition_variable drained;
    std::mutex control;
    std::atomic<bool> closing = false;
    bool shutdownComplete = false;
    size_t outstanding = 0;
    std::set<std::string> nativeModules;
    std::map<std::string, std::shared_ptr<ExecutionCancellation>> requests;
    std::map<std::string, std::chrono::steady_clock::time_point> cancelled;
    std::map<std::string, Extension> extensions;
    std::map<std::string, DesktopPushHandler> pushHandlers;
    std::map<std::string, DeferredExecutor> deferredReads;
    struct Maintenance {
      std::set<std::string> operations;
      bool retrySafe;
      DeferredExecutor executor;
    };
    std::map<std::string, Maintenance> deferredMaintenance;
    std::map<std::string, ExecutionController::Executor> finiteExecutors;
    std::map<std::string, ExecutionController::Executor> continuousExecutors;
    std::shared_ptr<DesktopBus> bus;
    std::shared_ptr<DesktopNotifications> notifications;
    std::shared_ptr<void> lease;
    std::unique_ptr<ExecutionController> finite;
    std::unique_ptr<ExecutionController> continuous;
    std::function<void(const std::string&)> finiteCancellation;
    std::function<void(const std::string&)> continuousCancellation;
    std::unique_ptr<RecordStore> notificationStore;
    std::unique_ptr<DesktopLauncher> launcher;
    std::unique_ptr<DesktopPush> push;
    std::shared_ptr<DeferredController> deferred;
    gulong networkSignal = 0;
    std::shared_ptr<ExecutionCancellation> admissionCancellation;
    std::atomic<bool> credentialsBusy = false;
    std::shared_ptr<ProtectedKeys> protectedKeys;
    std::unique_ptr<DesktopService> host;
    std::shared_ptr<DesktopServiceClient> hostClient;
    gulong hostBusClosed = 0;

    explicit State(core::services::Platform& service) : service(service) {
    }

    Runtime& runtime () {
      return *this->service.context.getRuntime();
    }

    std::string config (const std::string& key) {
      const auto& values = this->runtime().userConfig;
      const auto found = values.find(key);
      return found == values.end() ? "" : found->second;
    }

    bool enabled (const std::string& key) {
      return this->config(key) == "true";
    }

    bool remoteHost () {
      return enabled("linux_native_service_enabled") && !enabled("linux_native_service_host");
    }

    Pkcs11Options authenticationOptions () {
      Pkcs11Options options;
      const auto provider = config("authentication_linux_provider");
      if (!provider.empty()) options.provider = provider;
      options.module = config("authentication_linux_module");
      options.tokenSerial = config("authentication_linux_token_serial");
      const auto bits = config("authentication_linux_rsa_bits");
      if (!bits.empty()) {
        if (bits != "2048" && bits != "3072" && bits != "4096") throw StateError("EINVAL");
        options.rsaBits = static_cast<unsigned int>(std::stoul(bits));
      }
      return options;
    }

    std::shared_ptr<ProtectedKeys> authentication () {
      std::lock_guard lock(control);
      if (!protectedKeys) {
        const auto directory = service.services.state.databasePath();
        if (!service.services.state.isReady() || directory.empty()) throw StateError("ENOTREADY");
        auto lock = std::make_shared<Lease>(directory / "native-authentication.lock");
        RecordStore journal(directory / "native-authentication-v2.sqlite",
          service.services.secureStorage.nativeRecordKeys(), "protected-keys",
          {config("meta_bundle_identifier"), "runtime", "protected-keys"});
        protectedKeys = std::make_shared<ProtectedKeys>(std::move(journal), authenticationOptions(),
          config("meta_bundle_identifier"), std::move(lock));
      }
      return protectedKeys;
    }

    void activateNotification (const std::string& receipt) {
      if (!enabled("linux_native_service_host")) return;
      std::error_code failure;
      const auto executable = std::filesystem::read_symlink("/proc/self/exe", failure);
      if (failure || executable.empty()) throw StateError("ENOENT");
      const auto protocol = config("meta_application_protocol");
      const auto uri = protocol.empty() ? "" : protocol + "://oro-native-notification/" + receipt;
      GError* error = nullptr;
      auto process = uri.empty()
        ? g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, executable.c_str(), nullptr)
        : g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, executable.c_str(), uri.c_str(), nullptr);
      if (!process) { if (error) g_error_free(error); throw StateError("EIO"); }
      g_object_unref(process);
    }

    RecordStore hostAuthentication () {
      const auto directory = service.services.state.databasePath();
      if (!service.services.state.isReady() || directory.empty()) throw StateError("ENOTREADY");
      return RecordStore(directory / "native-service-v2.sqlite", service.services.secureStorage.nativeRecordKeys(),
        "service-auth", {config("meta_bundle_identifier"), "runtime", "native-service"});
    }

    void emit (const std::string& name, const JSON::Any& detail = JSON::Object{}) {
      if (this->closing) return;
      if (this->enabled("linux_native_service_host")) {
        if (this->host) this->host->changed(name);
        return;
      }
      const auto weak = this->weak_from_this();
      this->runtime().dispatch([weak, name, detail] {
        if (const auto state = weak.lock(); state && !state->closing)
          state->runtime().windowManager.emit(name, detail);
      });
    }

    RecordStore store (const std::string& name) {
      const auto directory = this->service.services.state.databasePath();
      if (!this->service.services.state.isReady() || directory.empty())
        throw StateError("ENOTREADY");
      if (!this->lease)
        this->lease = std::make_shared<Lease>(directory / "native-platform.lock");
      const auto bundle = this->config("meta_bundle_identifier");
      return RecordStore(directory / "native-platform-v2.sqlite",
                         this->service.services.secureStorage.nativeRecordKeys(), name,
                         {bundle, "runtime", "native-platform"});
    }

    void initializeBus () {
      if (this->bus)
        return;
      this->bus = std::make_shared<DesktopBus>();
      const auto weak = this->weak_from_this();
      this->notifications = std::make_shared<DesktopNotifications>(
        this->bus, this->config("build_name").empty() ? this->config("meta_bundle_identifier") : this->config("build_name"),
        this->config("meta_title").empty() ? this->config("build_name") : this->config("meta_title"),
        [weak] (const auto& id, const auto& tag, const auto& action) {
          if (const auto state = weak.lock())
            state->response(id, tag, action);
        });
    }

    bool canNotify () {
      if (!this->service.services.notifications.permissions.hasRuntimePermission("notifications"))
        return false;
      try {
        this->initializeBus();
        const auto capabilities = this->notifications->capabilities();
        return std::find(capabilities.begin(), capabilities.end(), "actions") != capabilities.end();
      } catch (const StateError&) {
        return false;
      }
    }

    ExecutionController& controller (bool monitoring) {
      auto& controller = monitoring ? this->continuous : this->finite;
      if (controller)
        return *controller;
      this->initializeBus();
      auto journal = this->store(monitoring ? "continuous" : "foreground");
      const auto weak = this->weak_from_this();
      controller = std::make_unique<ExecutionController>(
        std::move(journal), monitoring, this->lease,
        [weak, monitoring] (const ExecutionStatus& status) {
          const auto state = weak.lock();
          if (!state || !state->canNotify())
            return false;
          {
            std::lock_guard lock(state->mutex);
            if (state->closing || (state->admissionCancellation && state->admissionCancellation->cancelled()))
              return false;
          }
          DesktopNotification notification;
          notification.id = monitoring ? "oro.runtime.continuous" : "oro.runtime.foreground";
          notification.tag = status.generation;
          notification.title = monitoring ? "User-started monitoring" : "User-started operation";
          notification.body = monitoring ? "Monitoring is running. Choose Stop to end it."
                                         : "An operation is running. Choose Stop to end it.";
          notification.icon = state->config("build_name").empty() ? state->config("meta_bundle_identifier") : state->config("build_name");
          notification.silent = true;
          notification.ongoing = true;
          notification.actions = {{"stop", "Stop"}};
          state->notifications->submit(notification);
          bool cancelled = false;
          {
            std::lock_guard lock(state->mutex);
            cancelled = state->closing || (state->admissionCancellation && state->admissionCancellation->cancelled());
          }
          if (cancelled)
            state->notifications->close(notification.id, notification.tag);
          return !cancelled;
        },
        [weak, monitoring] (const ExecutionStatus& value) {
          if (const auto state = weak.lock()) {
            try {
              state->notifications->close(monitoring ? "oro.runtime.continuous" : "oro.runtime.foreground",
                                          value.generation);
            } catch (...) {
            }
          }
        });
      const auto& executors = monitoring ? this->continuousExecutors : this->finiteExecutors;
      for (const auto& [name, executor] : executors)
        controller->registerExecutor(name, executor);
      {
        std::lock_guard lock(this->mutex);
        (monitoring ? this->continuousCancellation : this->finiteCancellation) = controller->cancellationHandler();
      }
      return *controller;
    }

    static bool constraints (const std::string& network, bool charging, bool batteryNotLow) {
      const auto monitor = g_network_monitor_get_default();
      if (!g_network_monitor_get_network_available(monitor) ||
          (network == "unmetered" && g_network_monitor_get_network_metered(monitor)))
        return false;
      if (!charging && !batteryNotLow)
        return true;
      std::error_code failure;
      const std::filesystem::path root("/sys/class/power_supply");
      if (!std::filesystem::exists(root, failure))
        return !failure;
      size_t count = 0;
      for (const auto& entry : std::filesystem::directory_iterator(root, failure)) {
        if (failure || ++count > 64)
          return false;
        std::ifstream type(entry.path() / "type");
        std::string kind;
        std::getline(type, kind);
        if (kind != "Battery")
          continue;
        std::ifstream statusFile(entry.path() / "status");
        std::string status;
        std::getline(statusFile, status);
        if (charging && status != "Charging" && status != "Full" && status != "Not charging")
          return false;
        if (batteryNotLow) {
          std::ifstream capacityFile(entry.path() / "capacity");
          int capacity = -1;
          if (!(capacityFile >> capacity) || capacity < 15 || capacity > 100)
            return false;
        }
      }
      return !failure;
    }

    DeferredController& deferredController () {
      if (this->deferred)
        return *this->deferred;
      if (!this->enabled("background_deferred_enabled"))
        throw StateError("EACCES");
      auto journal = this->store("deferred");
      this->deferred = std::make_shared<DeferredController>(std::move(journal), this->lease, constraints);
      for (const auto& [name, executor] : this->deferredReads)
        this->deferred->registerRead(name, executor);
      for (const auto& [name, registered] : this->deferredMaintenance)
        this->deferred->registerMaintenance(name, registered.operations, registered.retrySafe, registered.executor);
      const auto weak = new std::weak_ptr<DeferredController>(this->deferred);
      this->networkSignal = g_signal_connect_data(
        g_network_monitor_get_default(), "network-changed",
        G_CALLBACK(+[] (GNetworkMonitor*, gboolean, gpointer pointer) {
          if (const auto controller = static_cast<std::weak_ptr<DeferredController>*>(pointer)->lock())
            controller->wake();
        }),
        weak,
        [] (gpointer pointer, GClosure*) {
          delete static_cast<std::weak_ptr<DeferredController>*>(pointer);
        },
        static_cast<GConnectFlags>(0));
      return *this->deferred;
    }

    JSON::Any deferredRequest (const std::string& action, const JSON::Object& options) {
      if (action == "support") {
        fields(options, {});
        JSON::Array reads;
        JSON::Array maintenance;
        JSON::Object operations;
        for (const auto& [name, executor] : this->deferredReads)
          reads.push(name);
        for (const auto& [name, registered] : this->deferredMaintenance) {
          maintenance.push(name);
          JSON::Array values;
          for (const auto& operation : registered.operations)
            values.push(operation);
          operations[name] = values;
        }
        return JSON::Object::Entries{{"available", this->enabled("background_deferred_enabled")},
                                     {"registeredKinds", reads},
                                     {"registeredMaintenanceKinds", maintenance},
                                     {"maintenanceOperations", operations},
                                     {"maximumTasks", 32},
                                     {"maximumAttempts", 5},
                                     {"readOnly", true},
                                     {"queuedWrites", false},
                                     {"backgroundJavaScript", false},
                                     {"survivesForceStop", false},
                                     {"periodicReads", true},
                                     {"minimumIntervalMs", 900000},
                                     {"minimumFlexMs", 300000},
                                     {"platform", "linux"},
                                     {"executionScope", "application-process"},
                                     {"survivesProcessExit", false},
                                     {"automaticResume", false},
                                     {"batteryPolicy", "sysfs-15-percent"}};
      }
      return this->deferredController().request(action, options);
    }

    DesktopPush& pushTransport () {
      if (this->push)
        return *this->push;
      if (!this->enabled("push_enabled") || this->config("push_provider") != "unifiedpush")
        throw StateError("EACCES");
#if !ORO_RUNTIME_ENABLE_MBEDTLS
      throw StateError("ENOSYS");
#endif
      this->initializeBus();
      const auto weak = this->weak_from_this();
      const auto directory = this->service.services.state.databasePath();
      if (!this->service.services.state.isReady() || directory.empty()) throw StateError("ENOTREADY");
      if (!this->lease)
        this->lease = std::make_shared<Lease>(directory / "native-platform.lock");
      const auto recovery = std::make_shared<PushJournalLedger>(directory / "push-recovery" / "selector.sqlite",
        this->config("meta_bundle_identifier"));
      const auto factory = [weak, directory](const std::string& epoch) {
        const auto state = weak.lock();
        if (!state) throw StateError("ESHUTDOWN");
        if (epoch.empty()) return state->store("push-transport");
        return RecordStore(directory / "push-recovery" / ("journal-" + epoch + ".sqlite"),
          state->service.services.secureStorage.nativeRecordKeys(), "push-transport",
          {state->config("meta_bundle_identifier"), "runtime", "native-platform"});
      };
      this->push = std::make_unique<DesktopPush>(
        this->bus, factory(recovery->epoch()), this->config("meta_bundle_identifier") + ".NativePush",
        [weak] (std::function<void()> work) {
          const auto state = weak.lock();
          if (!state)
            return false;
          {
            std::lock_guard lock(state->mutex);
            if (state->closing || state->outstanding >= 32)
              return false;
            state->outstanding++;
          }
          state->service.queue.push([state, work] {
            try {
              work();
            } catch (...) {
            }
            std::lock_guard lock(state->mutex);
            state->outstanding--;
            state->drained.notify_all();
          });
          return true;
        },
        this->lease,
        [weak] {
          if (const auto state = weak.lock())
            state->emit("pushchange");
        }, this->enabled("push_development_allow_loopback_http"), recovery, factory);
      for (const auto& [name, handler] : this->pushHandlers)
        this->push->registerHandler(name, handler);
      return *this->push;
    }

    JSON::Any pushRequest (const std::string& action, const JSON::Object& options) {
      if (action == "support") {
        fields(options, {});
        JSON::Array kinds;
        for (const auto& [name, handler] : this->pushHandlers)
          kinds.push(name);
        JSON::Array distributors;
        try {
          this->initializeBus();
          for (const auto& name : this->bus->names())
            if (name.starts_with("org.unifiedpush.Distributor."))
              distributors.push(name);
        } catch (const StateError&) {
        }
        return JSON::Object::Entries{
          {"available",
           this->enabled("push_enabled") && this->config("push_provider") == "unifiedpush" && webPushAvailable()},
          {"provider", "unifiedpush"},
          {"platform", "linux"},
          {"selectedDistributor", this->push ? this->push->selectedDistributor() : ""},
          {"registeredKinds", kinds},
          {"availableDistributors", distributors},
          {"maximumRegistrations", 16},
          {"maximumPendingMessages", 64},
          {"maximumMessageBytes", 4096},
          {"backgroundJavaScript", false},
          {"survivesForceStop", false},
          {"coldActivationConfigured", false},
          {"serverEnrollmentRequired", true}};
      }
      auto& push = this->pushTransport();
      if (action == "distributors") {
        fields(options, {});
        JSON::Array names;
        for (const auto& name : push.distributors())
          names.push(name);
        return names;
      }
      if (action == "selectDistributor") {
        fields(options, {"distributor"});
        auto name = text(options, "distributor", 255, this->config("push_distributor"));
        if (name.empty())
          name = env::get("UNIFIEDPUSH_DISTRIBUTOR");
        const auto available = push.distributors();
        if (name.empty() && available.size() == 1)
          name = available.front();
        if (name.empty())
          throw StateError("EAGAIN");
        push.selectDistributor(name);
        return JSON::Object::Entries{{"submitted", true}};
      }
      if (action == "register") {
        fields(options,
               {"id", "kind", "authorityRef", "accountRef", "targetRef", "applicationServerKey", "description"});
        return push.registerSubscription(
          {text(options, "id", 128), text(options, "kind", 64), text(options, "authorityRef", 128),
           text(options, "accountRef", 128), text(options, "targetRef", 128)},
          text(options, "applicationServerKey", 87), text(options, "description", 100, "Push notifications"));
      }
      if (action == "registration") {
        fields(options, {"id"});
        return push.registration(text(options, "id", 128));
      }
      if (action == "unregister") {
        fields(options, {"id"});
        push.unregister(text(options, "id", 128));
        return nullptr;
      }
      if (action == "acknowledgeRegistration") {
        fields(options, {"id", "generation"});
        push.acknowledge(text(options, "id", 128), text(options, "generation", 36));
        return nullptr;
      }
      if (action == "retryPending") {
        fields(options, {});
        push.retryPending();
        return JSON::Object::Entries{{"submitted", true}};
      }
      throw StateError("EINVAL");
    }

    RecordStore& notificationPolicy () {
      if (!this->notificationStore)
        this->notificationStore = std::make_unique<RecordStore>(this->store("notification-policy"));
      return *this->notificationStore;
    }

    void response (const std::string& id, const std::string& tag, const std::string& action) {
      const auto weak = this->weak_from_this();
      std::function<void(const std::string&)> cancel;
      if (action == "stop" || action == "dismiss" || action == "unavailable") {
        std::lock_guard lock(this->mutex);
        if (id == "oro.runtime.continuous")
          cancel = this->continuousCancellation;
        if (id == "oro.runtime.foreground")
          cancel = this->finiteCancellation;
      }
      if (cancel)
        cancel(tag);
      {
        std::lock_guard lock(this->mutex);
        if (this->closing || this->outstanding >= 32)
          return;
        this->outstanding++;
      }
      this->service.queue.push([weak, id, tag, action] {
        if (const auto state = weak.lock()) {
          try {
            std::lock_guard lock(state->control);
            if (id == "oro.runtime.continuous" || id == "oro.runtime.foreground") {
              auto& controller = id == "oro.runtime.continuous" ? state->continuous : state->finite;
              if (controller && (action == "stop" || action == "dismiss" || action == "unavailable")) {
                for (const auto& status : controller->list()) {
                  if (status.generation == tag)
                    controller->stop(status.task.id, tag);
                }
              }
            } else {
              if (action == "unavailable") throw StateError("ENOTREADY");
              auto& store = state->notificationPolicy();
              const auto snapshot = store.read();
              RecordChanges changes;
              size_t count = 0;
              std::pair<int64_t, std::string> oldest{INT64_MAX, ""};
              const auto current = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
              for (const auto& [key, bytes] : snapshot.records) {
                if (!key.starts_with("receipt."))
                  continue;
                const auto receipt = decoded(bytes);
                const auto time = receipt.get("receivedAt").as<JSON::Number>().value();
                if (time < current - 604800000)
                  changes[key] = std::nullopt;
                else {
                  count++;
                  if (time < oldest.first)
                    oldest = {time, key};
                }
              }
              if (count >= 64 && !oldest.second.empty())
                changes[oldest.second] = std::nullopt;
              auto uuid = g_uuid_string_random();
              const std::string receiptId(uuid);
              g_free(uuid);
              const JSON::Object receipt(JSON::Object::Entries{
                {"receiptId", receiptId}, {"id", id}, {"tag", tag}, {"action", action}, {"receivedAt", current}});
              changes["receipt." + receiptId] = encoded(receipt);
              store.apply(snapshot.revision, changes);
              state->emit("notificationresponse", JSON::Object::Entries{{"requiresRefresh", true}});
              if (action != "dismiss") state->activateNotification(receiptId);
            }
          } catch (...) {
          }
          {
            std::lock_guard lock(state->mutex);
            state->outstanding--;
            state->drained.notify_all();
          }
        }
      });
    }

    JSON::Any foreground (const std::string& originalAction, const JSON::Object& options,
                          const std::shared_ptr<ExecutionCancellation>& cancellation) {
      const bool monitoring = originalAction.starts_with("continuous.");
      const auto action = monitoring ? originalAction.substr(11) : originalAction;
      const bool enabled = this->enabled(monitoring ? "foreground_continuous_enabled" : "foreground_enabled");
      if (action == "support") {
        fields(options, {});
        JSON::Array kinds;
        const auto& registered = monitoring ? this->continuousExecutors : this->finiteExecutors;
        for (const auto& [name, executor] : registered)
          kinds.push(name);
        return JSON::Object::Entries{
          {"available", enabled},         {"canStart", enabled && !registered.empty() && this->canNotify()},
          {"registeredKinds", kinds},     {"maximumDurationMs", monitoring ? JSON::Any(nullptr) : JSON::Any(1800000)},
          {"maximumTasks", 16},           {"maximumConcurrentExecutors", 1},
          {"automaticRestart", false},    {"backgroundJavaScript", false},
          {"platform", "linux"},          {"executionScope", "application-process"},
          {"survivesProcessExit", false}, {"notificationAttentionGuaranteed", false}};
      }
      if (!enabled)
        throw StateError("EACCES");
      auto& controller = this->controller(monitoring);
      if (action == "list") {
        fields(options, {});
        JSON::Array values;
        for (const auto& status : controller.list())
          values.push(publicStatus(status, monitoring));
        return values;
      }
      const auto id = text(options, "id", 128);
      if (action == "status") {
        fields(options, {"id"});
        return publicStatus(controller.status(id), monitoring);
      }
      if (action == "stop" || action == "release") {
        fields(options, {"id", "generation"});
        const auto generation = text(options, "generation", 36);
        if (action == "stop")
          return publicStatus(controller.stop(id, generation), monitoring);
        controller.release(id, generation);
        return nullptr;
      }
      if (action != "start")
        throw StateError("EINVAL");
      if (monitoring)
        fields(options, {"id", "kind", "authorityRef", "accountRef", "targetRef", "operationRef", "userInitiated"});
      else
        fields(options, {"id", "kind", "authorityRef", "accountRef", "targetRef", "operationRef", "userInitiated",
                         "durationMs"});
      this->admissionCancellation = cancellation;
      struct Reset {
        State& state;
        ~Reset() {
          state.admissionCancellation.reset();
        }
      } reset{*this};
      return publicStatus(
        controller.start(
          {id, text(options, "kind", 64), text(options, "authorityRef", 128), text(options, "accountRef", 128),
           text(options, "targetRef", 128), text(options, "operationRef", 128)},
          flag(options, "userInitiated"), monitoring ? 0 : integer(options, "durationMs", 900000)),
        monitoring);
    }

    JSON::Any notification (const std::string& action, const JSON::Object& options) {
      if (action == "deliveryStatus") {
        fields(options, {"channel"});
        const auto channel = text(options, "channel", 100, "default");
        bool enabled = this->service.services.notifications.permissions.hasRuntimePermission("notifications");
        bool transport = false;
        try {
          this->initializeBus();
          this->notifications->capabilities();
          transport = true;
        } catch (const StateError&) {
        }
        JSON::Object policy(JSON::Object::Entries{{"importance", "default"}, {"sound", "default"}});
        bool exists = channel == "default";
        if (!exists) {
          const auto snapshot = this->notificationPolicy().read();
          if (const auto found = snapshot.records.find("channel." + channel); found != snapshot.records.end()) {
            policy = decoded(found->second);
            exists = true;
          }
        }
        const auto importance = text(policy, "importance", 16, "default");
        return JSON::Object::Entries{
          {"enabled", enabled},
          {"channelId", channel},
          {"exists", exists},
          {"blocked", !enabled},
          {"importance", exists ? JSON::Any(importance == "min"    ? 1
                                            : importance == "low"  ? 2
                                            : importance == "high" ? 4
                                                                   : 3)
                                : JSON::Any(nullptr)},
          {"sound", exists && text(policy, "sound", 160, "default") != "none"},
          {"vibration", false},
          {"canSubmit", enabled && exists && transport},
          {"deliveryBoundary", "submission-only"},
          {"pushAvailable",
           this->enabled("push_enabled") && this->config("push_provider") == "unifiedpush" && webPushAvailable()},
          {"systemChannel", false},
          {"systemPolicyKnown", false},
          {"platform", "linux"}};
      }
      if (!this->service.services.notifications.permissions.hasRuntimePermission("notifications"))
        throw StateError("EACCES");
      this->initializeBus();
      if (action == "support") {
        JSON::Array values;
        for (const auto& value : this->notifications->capabilities())
          values.push(value);
        return JSON::Object::Entries{{"available", true},
                                     {"platform", "linux"},
                                     {"capabilities", values},
                                     {"channelPolicy", "application"},
                                     {"systemChannels", false},
                                     {"grouping", false},
                                     {"attentionGuaranteed", false},
                                     {"coldActionRouting", false}};
      }
      if (action == "configureChannels") {
        fields(options, {"channels"});
        if (!options.has("channels") || options.get("channels").type != JSON::Type::Array)
          throw StateError("EINVAL");
        const auto channels = options.get("channels").as<JSON::Array>();
        if (channels.size() < 1 || channels.size() > 32)
          throw StateError("EINVAL");
        auto& store = this->notificationPolicy();
        const auto snapshot = store.read();
        RecordChanges changes;
        JSON::Array result;
        std::set<std::string> ids;
        size_t count = 0;
        for (const auto& [key, value] : snapshot.records)
          if (key.starts_with("channel."))
            count++;
        for (const auto& item : channels) {
          if (item.type != JSON::Type::Object)
            throw StateError("EINVAL");
          const auto channel = item.as<JSON::Object>();
          fields(channel, {"id", "name", "description", "importance", "sound", "vibration", "badge"});
          text(channel, "description", 512);
          flag(channel, "badge", true);
          const auto id = text(channel, "id", 100);
          if (!std::regex_match(id, std::regex("[a-zA-Z0-9][a-zA-Z0-9_.-]{0,99}")) || id == "default" ||
              !ids.insert(id).second || text(channel, "name", 128).empty())
            throw StateError("EINVAL");
          const auto importance = text(channel, "importance", 16, "default");
          if (importance != "min" && importance != "low" && importance != "default" && importance != "high")
            throw StateError("EINVAL");
          const auto sound = text(channel, "sound", 160, "default");
          if ((sound != "default" && sound != "none") || flag(channel, "vibration"))
            throw StateError("ENOSYS");
          const auto key = "channel." + id;
          const auto bytes = encoded(channel);
          if (const auto found = snapshot.records.find(key); found != snapshot.records.end()) {
            if (found->second != bytes)
              throw StateError("EEXIST");
          } else {
            if (++count > 32)
              throw StateError("ENOSPC");
            changes[key] = bytes;
          }
          const auto level = importance == "min" ? 1 : importance == "low" ? 2 : importance == "high" ? 4 : 3;
          result.push(JSON::Object::Entries{{"id", id},
                                            {"importance", level},
                                            {"sound", sound != "none"},
                                            {"vibration", false},
                                            {"blocked", false},
                                            {"systemChannel", false},
                                            {"badgeSupported", false}});
        }
        if (!changes.empty())
          store.apply(snapshot.revision, changes);
        return result;
      }
      if (action == "responses") {
        fields(options, {});
        JSON::Array values;
        const auto snapshot = this->notificationPolicy().read();
        const auto now =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
        for (const auto& [key, value] : snapshot.records) {
          if (key.starts_with("receipt.")) {
            const auto receipt = decoded(value);
            if (receipt.get("receivedAt").as<JSON::Number>().value() >= now - 604800000)
              values.push(receipt);
          }
        }
        return values;
      }
      if (action == "acknowledge") {
        fields(options, {"receiptId"});
        const auto id = text(options, "receiptId", 36);
        if (!std::regex_match(id, std::regex("[a-f0-9-]{36}")))
          throw StateError("EINVAL");
        auto& store = this->notificationPolicy();
        const auto snapshot = store.read();
        if (snapshot.records.contains("receipt." + id))
          store.apply(snapshot.revision, {{"receipt." + id, std::nullopt}});
        return nullptr;
      }
      if (action == "cancel") {
        fields(options, {"id", "tag"});
        const auto id = text(options, "id", 256);
        if (id.starts_with("oro.runtime.")) throw StateError("EACCES");
        this->notifications->close(id, text(options, "tag", 256));
        return nullptr;
      }
      if (action != "submit")
        throw StateError("EINVAL");
      fields(options, {"id", "title", "body", "tag", "channel", "category", "silent", "renotify", "requireInteraction",
                       "actions", "group", "groupSummary", "groupAlertBehavior", "smallIcon", "publicVersion"});
      if (flag(options, "groupSummary") || text(options, "groupAlertBehavior", 16, "all") != "all")
        throw StateError("ENOSYS");
      DesktopNotification notification;
      notification.id = text(options, "id", 256);
      if (notification.id.starts_with("oro.runtime.")) throw StateError("EACCES");
      notification.tag = text(options, "tag", 256);
      notification.title = text(options, "title", 256);
      notification.body = text(options, "body", 4096);
      if (options.has("publicVersion")) {
        if (options.get("publicVersion").type != JSON::Type::Object)
          throw StateError("EINVAL");
        const auto publicVersion = options.get("publicVersion").as<JSON::Object>();
        fields(publicVersion, {"title", "body"});
        notification.title = text(publicVersion, "title", 160);
        notification.body = text(publicVersion, "body", 512);
      }
      notification.category = text(options, "category", 128);
      notification.group = text(options, "group", 128);
      notification.icon = text(options, "smallIcon", 128,
        this->config("build_name").empty() ? this->config("meta_bundle_identifier") : this->config("build_name"));
      notification.silent = flag(options, "silent");
      notification.renotify = flag(options, "renotify");
      notification.ongoing = flag(options, "requireInteraction");
      const auto channel = text(options, "channel", 100, "default");
      if (channel != "default") {
        const auto snapshot = this->notificationPolicy().read();
        const auto found = snapshot.records.find("channel." + channel);
        if (found == snapshot.records.end())
          throw StateError("ENOENT");
        const auto policy = decoded(found->second);
        const auto importance = text(policy, "importance", 16, "default");
        notification.urgency = importance == "min" || importance == "low" ? 0 : importance == "high" ? 2 : 1;
        notification.silent = notification.silent || text(policy, "sound", 160, "default") == "none";
      }
      notification.actions = {{"default", "Open"}};
      if (options.has("actions")) {
        if (options.get("actions").type != JSON::Type::Array)
          throw StateError("EINVAL");
        const auto actions = options.get("actions").as<JSON::Array>();
        if (actions.size() > 3)
          throw StateError("EINVAL");
        for (const auto& entry : actions) {
          if (entry.type != JSON::Type::Object)
            throw StateError("EINVAL");
          const auto action = entry.as<JSON::Object>();
          fields(action, {"action", "title"});
          const auto id = text(action, "action", 128);
          if (id == "default" || id == "dismiss")
            throw StateError("EINVAL");
          notification.actions.emplace_back(id, text(action, "title", 128));
        }
      }
      const auto serverId = this->notifications->submit(notification);
      return JSON::Object::Entries{
        {"id", notification.id},
        {"tag", notification.tag},
        {"submitted", serverId != 0},
        {"presentationObserved", false},
        {"attentionGuaranteed", false},
        {"groupingSupported", false},
        {"visibilityPolicy",
         options.has("publicVersion") ? "public-version-on-all-surfaces" : "caller-provided-public-content"}};
    }

    JSON::Any perform (const std::string& feature, const std::string& action, const JSON::Object& options,
                       const std::shared_ptr<ExecutionCancellation>& cancellation) {
      if (cancellation->cancelled())
        throw StateError("ABORT_ERR");
      if (feature == "foreground")
        return this->foreground(action, options, cancellation);
      if (feature == "notifications")
        return this->notification(action, options);
      if (feature == "push")
        return this->pushRequest(action, options);
      if (feature == "deferred")
        return this->deferredRequest(action, options);
      if (feature == "launcher") {
        if (action == "status") {
          fields(options, {});
          return JSON::Object::Entries{
            {"available", this->enabled("launcher_enabled") && !this->config("meta_application_protocol").empty()},
            {"platform", "linux"},
            {"pinSupported", false},
            {"maximumPerActivity", 16},
            {"rateLimited", false},
            {"authorizationBoundary", "resolve-after-authentication"},
            {"presentation", "desktop-actions"}};
        }
        auto& launcher = this->desktopLauncher();
        if (action == "receive") {
          fields(options, {"uri"});
          return launcher.receive(text(options, "uri", 512));
        }
        if (action == "replace") {
          fields(options, {"destinations"});
          if (!options.has("destinations") || options.get("destinations").type != JSON::Type::Array)
            throw StateError("EINVAL");
          return launcher.replace(options.get("destinations").as<JSON::Array>());
        }
        if (action == "responses") {
          fields(options, {});
          return launcher.responses();
        }
        if (action == "acknowledge") {
          fields(options, {"receiptId"});
          launcher.acknowledge(text(options, "receiptId", 36));
          return nullptr;
        }
        throw StateError(action == "pin" ? "ENOSYS" : "EINVAL");
      }
      if (feature == "extension") {
        fields(options, {"extension", "payload"});
        const auto name = text(options, "extension", 64);
        if (!kind(name, true) || !std::regex_match(action, std::regex("[a-zA-Z][a-zA-Z0-9_.-]{0,63}")) ||
            !options.has("payload") || options.get("payload").type != JSON::Type::Object)
          throw StateError("EINVAL");
        Extension extension;
        {
          std::lock_guard lock(this->mutex);
          const auto found = this->extensions.find(name);
          if (found == this->extensions.end())
            throw StateError("ENOSYS");
          extension = found->second;
        }
        return extension(action, options.get("payload").as<JSON::Object>(), *cancellation);
      }
      throw StateError("ENOSYS");
    }

    DesktopLauncher& desktopLauncher () {
      if (!this->enabled("launcher_enabled"))
        throw StateError("EACCES");
      if (!this->launcher) {
        const auto title = this->config("meta_title").empty() ? this->config("build_name") : this->config("meta_title");
        this->launcher =
          std::make_unique<DesktopLauncher>(this->store("launcher"), this->config("meta_bundle_identifier"), title,
                                            this->config("meta_application_protocol"));
      }
      return *this->launcher;
    }
  };

  std::shared_ptr<LinuxPlatform> LinuxPlatform::fromRuntime (Runtime& runtime) {
    return runtime.services.platform.nativeLinux();
  }

  LinuxPlatform::LinuxPlatform(core::services::Platform& service) : state(std::make_shared<State>(service)) {
  }
  LinuxPlatform::~LinuxPlatform() {
    try {
      this->shutdown();
    } catch (...) {
    }
  }

  std::shared_ptr<LinuxPlatform> LinuxPlatform::fromExtension (const oapi_context_t* context) {
    if (!context || !oapi_context_get_router(context)) {
      throw StateError("EACCES");
    }
    auto owner = static_cast<const oro::extension::Extension::Context*>(context);
    for (size_t depth = 0; owner && !owner->extension && depth < 8; depth++) owner = owner->context;
    const auto extension = owner ? owner->extension : nullptr;
    if (!extension || extension->path.empty()) throw StateError("EACCES");
    const auto runtime = context->router->bridge.getRuntime();
    if (!runtime) throw StateError("ENOTREADY");
    const auto backend = runtime->services.platform.nativeLinux();
    std::lock_guard lock(backend->state->mutex);
    if (backend->state->closing) throw StateError("ESHUTDOWN");
    const auto name = extension->name;
    if (!backend->state->nativeModules.contains(name)) {
      if (backend->state->nativeModules.size() >= 32) throw StateError("ENOSPC");
#if defined(RTLD_NODELETE)
      // Registered std::function targets can outlive a page's extension context.
      // Retain the already loaded compiled module, never resolve a new page path.
      const auto handle = dlopen(extension->path.c_str(), RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
      if (!handle) throw StateError("ENOTREADY");
      dlclose(handle);
#else
      throw StateError("ENOSYS");
#endif
      backend->state->nativeModules.insert(name);
    }
    return backend;
  }

  RecordStore LinuxPlatform::openRecords (const std::string& name, const RecordIdentity& identity) {
    const auto state = this->state;
    if (state->closing) throw StateError("ESHUTDOWN");
    if (!name.starts_with("product.")) throw StateError("EINVAL");
    const auto directory = state->service.services.state.databasePath();
    if (!state->service.services.state.isReady() || directory.empty()) throw StateError("ENOTREADY");
    return RecordStore(directory / "native-extension-v2.sqlite",
      state->service.services.secureStorage.nativeRecordKeys(), name, identity);
  }

  std::vector<PushRecoveryRegistration> LinuxPlatform::discoverPushRegistrations(const PushRecoveryScope& scope) {
    std::lock_guard control(this->state->control);
    if (this->state->closing) throw StateError("ESHUTDOWN");
    if (this->state->remoteHost()) throw StateError("ENOTSUP");
    return this->state->pushTransport().discoverRegistrations(scope);
  }

  bool LinuxPlatform::unregisterPushIfCurrent(const PushRecoveryScope& scope, const std::string& id,
                                               const std::string& revision) {
    std::lock_guard control(this->state->control);
    if (this->state->closing) throw StateError("ESHUTDOWN");
    if (this->state->remoteHost()) throw StateError("ENOTSUP");
    return this->state->pushTransport().unregisterIfCurrent(scope, id, revision);
  }

  PushJournalInspection LinuxPlatform::inspectPushJournalRecovery () {
    std::lock_guard control(this->state->control);
    if (this->state->closing) throw StateError("ESHUTDOWN");
    if (this->state->remoteHost()) throw StateError("ENOTSUP");
    return this->state->pushTransport().inspectJournalRecovery();
  }
  PushJournalReceipt LinuxPlatform::quarantineUnreadablePushJournal (const PushJournalConsent& consent) {
    std::lock_guard control(this->state->control);
    if (this->state->closing) throw StateError("ESHUTDOWN");
    if (this->state->remoteHost()) throw StateError("ENOTSUP");
    return this->state->pushTransport().quarantineUnreadableJournal(consent);
  }

  void LinuxPlatform::registerExecutor(bool continuous, const std::string& name,
                                       ExecutionController::Executor executor) {
    std::lock_guard control(this->state->control);
    std::lock_guard lock(this->state->mutex);
    auto& executors = continuous ? this->state->continuousExecutors : this->state->finiteExecutors;
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (!kind(name) || !executor)
      throw StateError("EINVAL");
    if (executors.contains(name))
      throw StateError("EEXIST");
    if (executors.size() >= 16)
      throw StateError("ENOSPC");
    auto& controller = continuous ? this->state->continuous : this->state->finite;
    if (controller)
      controller->registerExecutor(name, executor);
    executors[name] = std::move(executor);
  }

  void LinuxPlatform::registerExtension(const std::string& name, Extension extension) {
    std::lock_guard lock(this->state->mutex);
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (!kind(name, true) || !extension)
      throw StateError("EINVAL");
    if (this->state->extensions.contains(name))
      throw StateError("EEXIST");
    if (this->state->extensions.size() >= 32)
      throw StateError("ENOSPC");
    this->state->extensions[name] = std::move(extension);
  }

  void LinuxPlatform::registerPushHandler(const std::string& name, DesktopPushHandler handler) {
    std::lock_guard control(this->state->control);
    std::lock_guard lock(this->state->mutex);
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (!kind(name) || !handler.authorize || !handler.message)
      throw StateError("EINVAL");
    if (this->state->pushHandlers.contains(name))
      throw StateError("EEXIST");
    if (this->state->pushHandlers.size() >= 16)
      throw StateError("ENOSPC");
    if (this->state->push)
      this->state->push->registerHandler(name, handler);
    this->state->pushHandlers[name] = std::move(handler);
  }

  void LinuxPlatform::registerDeferredRead(const std::string& name, DeferredExecutor executor) {
    std::lock_guard control(this->state->control);
    if (!kind(name) || !executor.authorize || !executor.execute)
      throw StateError("EINVAL");
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (this->state->deferredReads.contains(name))
      throw StateError("EEXIST");
    if (this->state->deferredReads.size() >= 16)
      throw StateError("ENOSPC");
    if (this->state->deferred)
      this->state->deferred->registerRead(name, executor);
    this->state->deferredReads[name] = std::move(executor);
  }

  void LinuxPlatform::registerDeferredMaintenance(const std::string& name, const std::set<std::string>& operations,
                                                  bool retrySafe, DeferredExecutor executor) {
    std::lock_guard control(this->state->control);
    if (!kind(name) || !retrySafe || operations.empty() || !executor.authorize || !executor.execute)
      throw StateError("EINVAL");
    for (const auto& operation : operations)
      if (operation != "cleanup" && operation != "revoke")
        throw StateError("EINVAL");
    if (this->state->closing)
      throw StateError("ESHUTDOWN");
    if (this->state->deferredMaintenance.contains(name))
      throw StateError("EEXIST");
    if (this->state->deferredMaintenance.size() >= 16)
      throw StateError("ENOSPC");
    if (this->state->deferred)
      this->state->deferred->registerMaintenance(name, operations, retrySafe, executor);
    this->state->deferredMaintenance[name] = {operations, retrySafe, std::move(executor)};
  }

  void LinuxPlatform::initializeNativeServices() {
    std::lock_guard control(this->state->control);
    if (this->state->remoteHost()) return;
    if (this->state->enabled("push_enabled") && this->state->config("push_provider") == "unifiedpush") {
      this->state->pushTransport().resume();
    }
    if (this->state->enabled("background_deferred_enabled"))
      this->state->deferredController();
  }

  void LinuxPlatform::serveNativeHost (std::function<void()> onBusLost) {
    std::lock_guard control(this->state->control);
    if (!state->enabled("linux_native_service_enabled") || !state->enabled("linux_native_service_host") || state->host)
      throw StateError("EACCES");
    state->initializeBus();
    state->host = std::make_unique<DesktopService>(state->bus, state->hostAuthentication(),
      state->config("meta_bundle_identifier") + ".NativeRuntime", this->state->service.nativeLinux());
    if (onBusLost) state->hostBusClosed = state->bus->onClosed(std::move(onBusLost));
  }

  bool LinuxPlatform::receiveLauncherURI(const std::string& uri) {
    auto state = this->state;
    const auto notification = state->config("meta_application_protocol") + "://oro-native-notification/";
    if (uri.starts_with(notification)) {
      const auto id = uri.substr(notification.size());
      if (id.size() == 36 && g_uuid_string_is_valid(id.c_str()))
        state->emit("notificationresponse", JSON::Object::Entries{{"receiptId", id}, {"requiresRefresh", true}});
      return true;
    }
    const auto prefix = state->config("meta_application_protocol") + "://oro-native-launcher/";
    if (!uri.starts_with(prefix))
      return false;
    // URI input is an untrusted routing hint. Only current published IDs become
    // receipts; the application must reauthorize before opening any target.
    const auto weak = std::weak_ptr<State>(state);
    try {
      this->request("launcher-entry:" + std::to_string(crypto::rand64()), "launcher", "receive",
                    JSON::Object::Entries{{"uri", uri}}, [weak] (auto value, auto error) {
                      if (const auto state = weak.lock())
                        state->emit(
                          "launcherchange",
                          JSON::Object::Entries{{"committed", error.empty() && value.type == JSON::Type::Boolean &&
                                                                value.template as<JSON::Boolean>().value()},
                                                {"code", error}});
                    });
    } catch (const StateError& error) {
      state->emit("launcherchange", JSON::Object::Entries{{"committed", false}, {"code", error.code}});
    }
    return true;
  }

  void LinuxPlatform::request(const std::string& key, const std::string& feature, const std::string& action,
                              const JSON::Object& options, Completion callback, GtkWindow* interactiveParent) {
    auto state = this->state;
    const auto signal = std::make_shared<ExecutionCancellation>(90000);
    {
      std::lock_guard lock(state->mutex);
      if (state->closing || state->service.queue.destroyed())
        throw StateError("ESHUTDOWN");
      if (key.empty() || key.size() > 256 || options.str().size() > 196608)
        throw StateError("EINVAL");
      if (state->outstanding >= 32)
        throw StateError("EBUSY");
      if (state->requests.contains(key))
        throw StateError("EEXIST");
      const auto now = std::chrono::steady_clock::now();
      for (auto i = state->cancelled.begin(); i != state->cancelled.end();) {
        if (i->second < now)
          i = state->cancelled.erase(i);
        else
          ++i;
      }
      if (state->cancelled.erase(key))
        throw StateError("ABORT_ERR");
      state->requests[key] = signal;
      state->outstanding++;
    }
    const auto parent = interactiveParent
      ? std::shared_ptr<GtkWindow>(GTK_WINDOW(g_object_ref(interactiveParent)), [](GtkWindow* value) {
          g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer value) -> gboolean {
            g_object_unref(value);
            return G_SOURCE_REMOVE;
          }, value, nullptr);
        }) : nullptr;
    state->service.queue.push ([state, signal, key, feature, action, options, callback, parent] {
      JSON::Any value;
      std::string failure;
      try {
        std::unique_lock control (state->control, std::defer_lock);
        if (feature == "authentication") {
          if (action == "status") {
            fields (options, {"policy"});
            bool busy = state->credentialsBusy.load ();
            {
              std::lock_guard lock (state->control);
              busy = busy || (state->protectedKeys && state->protectedKeys->busy ());
            }
            value = ProtectedKeys::status (state->authenticationOptions (), text (options, "policy", 32),
                                           state->enabled ("permissions_allow_biometrics"), busy);
          } else {
            if (!state->enabled ("permissions_allow_biometrics"))
              throw StateError ("EACCES");
            if (state->credentialsBusy.load ())
              throw StateError ("EBUSY");
            value = state->authentication ()->request (action, options, parent.get (), signal);
          }
        } else if (feature == "credentials") {
          if (action == "status") {
            fields (options, {});
            value = JSON::Object::Entries{{"platform", "linux"},
                                          {"supported", DesktopCredentials::available ()},
                                          {"enabled", state->enabled ("credentials_enabled")},
                                          {"busy", state->credentialsBusy.load ()},
                                          {"provider", "secret-service"},
                                          {"association", "application-attribute"},
                                          {"autofill", false}};
          } else if (action == "selectPassword") {
            fields (options, {"username"});
            if (!state->enabled ("credentials_enabled"))
              throw StateError ("EACCES");
            if (state->credentialsBusy.exchange (true))
              throw StateError ("EBUSY");
            struct Busy {
              std::atomic<bool>& value;
              ~Busy () {
                value = false;
              }
            } busy{state->credentialsBusy};
            const auto selected = DesktopCredentials::select (state->config ("meta_bundle_identifier"),
                                                              text (options, "username", 320), parent.get (), signal);
            value = JSON::Object::Entries{{"username", selected.username}, {"password", selected.password}};
          } else {
            throw StateError ("EINVAL");
          }
        } else {
          if (feature != "extension" || state->remoteHost()) control.lock ();
          if (state->remoteHost () && feature != "back" && action != "support" && action != "continuous.support" &&
              !(feature == "launcher" && action == "status")) {
            std::shared_ptr<DesktopServiceClient> client;
            {
              std::lock_guard lock (state->mutex);
              client = state->hostClient;
            }
            if (!client) {
              state->initializeBus ();
              client = std::make_shared<DesktopServiceClient> (
                  state->bus, state->hostAuthentication (), state->config ("meta_bundle_identifier") + ".NativeRuntime",
                  [weak = std::weak_ptr<State> (state)] (const auto& name) {
                    if (const auto state = weak.lock ())
                      state->emit (name, JSON::Object::Entries{{"requiresRefresh", true}});
                  });
              std::lock_guard lock (state->mutex);
              state->hostClient = client;
            }
            value = client->request (key, feature, action, options, *signal);
          } else {
            value = state->perform (feature, action, options, signal);
          }
        }
        if (value.str ().size () > 524288)
          throw StateError ("E2BIG");
        if (signal->cancelled ())
          throw StateError ("ABORT_ERR");
      } catch (const StateError& error) {
        failure = error.code;
      } catch (...) {
        failure = "EIO";
      }
      try {
        callback (value, failure);
      } catch (...) {
      }
      {
        std::lock_guard lock (state->mutex);
        state->requests.erase (key);
        state->outstanding--;
        state->drained.notify_all ();
      }
    });
  }

  void LinuxPlatform::cancelRequest(const std::string& key) {
    std::lock_guard lock(this->state->mutex);
    if (state->hostClient) {
      try { state->hostClient->cancel(key); } catch (...) {}
    }
    if (const auto found = this->state->requests.find(key); found != this->state->requests.end())
      found->second->cancel();
    else {
      const auto now = std::chrono::steady_clock::now();
      for (auto i = this->state->cancelled.begin(); i != this->state->cancelled.end();) {
        if (i->second < now)
          i = this->state->cancelled.erase(i);
        else
          ++i;
      }
      if (this->state->cancelled.size() >= 128)
        throw StateError("EBUSY");
      this->state->cancelled[key] = now + std::chrono::seconds(100);
    }
  }

  void LinuxPlatform::cancelDocument(const std::string& prefix) {
    std::lock_guard lock(this->state->mutex);
    for (const auto& [key, cancellation] : this->state->requests)
      if (key.starts_with(prefix)) {
        cancellation->cancel();
        if (state->hostClient) {
          try { state->hostClient->cancel(key); } catch (...) {}
        }
      }
  }

  void LinuxPlatform::shutdown() {
    auto state = this->state;
    {
      std::lock_guard lock(state->mutex);
      if (state->closing)
        return;
      state->closing = true;
      for (const auto& [key, cancellation] : state->requests) {
        cancellation->cancel();
        if (state->hostClient) {
          try { state->hostClient->cancel(key); } catch (...) {}
        }
      }
    }
    {
      std::unique_lock lock(state->mutex);
      state->drained.wait(lock, [&] {
        return state->outstanding == 0;
      });
    }
    std::lock_guard control(state->control);
    if (state->hostBusClosed) {
      state->bus->removeClosedHandler(state->hostBusClosed);
      state->hostBusClosed = 0;
    }
    if (state->host) state->host->shutdown();
    std::exception_ptr failure;
    const auto cleanup = [&] (const auto& work) {
      try {
        work();
      } catch (...) {
        if (!failure)
          failure = std::current_exception();
      }
    };
    if (state->protectedKeys) {
      cleanup([&] { state->protectedKeys->shutdown(); });
      cleanup([&] { state->protectedKeys->drain(); });
    }
    if (state->push)
      cleanup([&] {
        state->push->shutdown();
      });
    if (state->finite)
      cleanup([&] {
        state->finite->shutdown();
      });
    if (state->continuous)
      cleanup([&] {
        state->continuous->shutdown();
      });
    if (state->push)
      cleanup([&] {
        state->push->drain();
      });
    if (state->finite)
      cleanup([&] {
        state->finite->drain();
      });
    if (state->continuous)
      cleanup([&] {
        state->continuous->drain();
      });
    if (state->networkSignal) {
      g_signal_handler_disconnect(g_network_monitor_get_default(), state->networkSignal);
      state->networkSignal = 0;
    }
    if (state->deferred)
      cleanup([&] {
        state->deferred->shutdown();
      });
    if (state->notifications) {
      for (const auto& [id, tag] : state->notifications->list()) {
        if (id == "oro.runtime.foreground" || id == "oro.runtime.continuous") {
          cleanup([&] {
            state->notifications->close(id, tag);
          });
        }
      }
    }
    state->shutdownComplete = true;
    if (failure)
      std::rethrow_exception(failure);
  }

  void LinuxPlatform::restart() {
    auto state = this->state;
    std::lock_guard control(state->control);
    std::lock_guard lock(state->mutex);
    if (!state->closing)
      return;
    if (!state->shutdownComplete || state->outstanding)
      throw StateError("EBUSY");
    state->push.reset();
    state->protectedKeys.reset();
    state->deferred.reset();
    state->finite.reset();
    state->continuous.reset();
    state->finiteCancellation = nullptr;
    state->continuousCancellation = nullptr;
    state->notificationStore.reset();
    state->launcher.reset();
    state->notifications.reset();
    state->host.reset();
    state->hostClient.reset();
    state->bus.reset();
    state->lease.reset();
    state->cancelled.clear();
    state->shutdownComplete = false;
    state->closing = false;
  }
}  // namespace oro::runtime::native
#endif
