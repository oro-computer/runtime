#include "network_status.hh"

namespace oro::runtime::core::services {
  NetworkStatus::NetworkStatus (const Options& options)
    : core::Service(options) {
  #if ORO_RUNTIME_PLATFORM_APPLE
    dispatch_queue_attr_t attrs = dispatch_queue_attr_make_with_qos_class(
      DISPATCH_QUEUE_SERIAL,
      QOS_CLASS_UTILITY,
      DISPATCH_QUEUE_PRIORITY_DEFAULT
    );

    this->queue = dispatch_queue_create(
      "oro.runtime.queue.ipc.network-status",
      attrs
    );

  #elif ORO_RUNTIME_PLATFORM_LINUX
    this->monitor = g_network_monitor_get_default();
  #elif !ORO_RUNTIME_PLATFORM_ANDROID
    // Native network monitoring is unavailable on this platform.
    this->enabled = false;
  #endif
  }

  NetworkStatus::~NetworkStatus () {
  #if ORO_RUNTIME_PLATFORM_ANDROID || ORO_RUNTIME_PLATFORM_LINUX
    this->stop();
  #endif
  #if ORO_RUNTIME_PLATFORM_APPLE
    this->stop();
    dispatch_release(this->queue);
    this->monitor = nullptr;
    this->queue = nullptr;
  #endif
  }

  bool NetworkStatus::start () {
  #if !ORO_RUNTIME_PLATFORM_ANDROID
    this->stop();
  #endif
  #if ORO_RUNTIME_PLATFORM_APPLE
    this->monitor = nw_path_monitor_create();

    nw_path_monitor_set_queue(this->monitor, this->queue);
    nw_path_monitor_set_update_handler(this->monitor, ^(nw_path_t path) {
      if (path == nullptr) {
        return;
      }

      nw_path_status_t status = nw_path_get_status(path);

      String name;
      String message;

      switch (status) {
        case nw_path_status_invalid: {
          name = "offline";
          message = "Network path is invalid";
          break;
        }
        case nw_path_status_satisfied: {
          name = "online";
          message = "Network is usable";
          break;
        }
        case nw_path_status_satisfiable: {
          name = "online";
          message = "Network may be usable";
          break;
        }
        case nw_path_status_unsatisfied: {
          name = "offline";
          message = "Network is not usable";
          break;
        }
      }

      const auto json = JSON::Object::Entries {
        {"name", name},
        {"message", message}
      };

      this->observers.dispatch(json);
    });
    nw_path_monitor_start(this->monitor);
    return true;
  #elif ORO_RUNTIME_PLATFORM_LINUX
    this->signal = g_signal_connect(
      this->monitor,
      "network-changed",
      G_CALLBACK((+[](GNetworkMonitor* monitor, gboolean networkAvailable, gpointer userData) {
        auto coreNetworkStatus = reinterpret_cast<NetworkStatus*>(userData);
        if (coreNetworkStatus) {
          coreNetworkStatus->observers.dispatch(coreNetworkStatus->snapshot());
        }
      })),
      this
    );
    for (const auto name : {"notify::network-metered", "notify::connectivity"}) {
      g_signal_connect(this->monitor, name, G_CALLBACK(+[](GObject*, GParamSpec*, gpointer pointer) {
        const auto service = static_cast<NetworkStatus*>(pointer);
        service->observers.dispatch(service->snapshot());
      }), this);
    }
    return true;
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    this->monitoring = true;
    const auto& androidContext = this->context.android;
    if (!androidContext.jvm || !androidContext.self) {
      return true;
    }
    const auto attachment = android::JNIEnvironmentAttachment(androidContext.jvm);
    return CallClassMethodFromAndroidEnvironment(
      attachment.env, Boolean, androidContext.self, "startNetworkStatusMonitor", "()Z"
    );
  #endif
    return false;
  }

  bool NetworkStatus::stop () {
  #if ORO_RUNTIME_PLATFORM_APPLE
    if (this->monitor) {
      nw_path_monitor_cancel(this->monitor);
      nw_release(this->monitor);
    }
    this->monitor = nullptr;
    return true;
  #elif ORO_RUNTIME_PLATFORM_LINUX
    if (this->signal) {
      g_signal_handlers_disconnect_by_data(this->monitor, this);
      this->signal = 0;
    }
    return true;
  #elif ORO_RUNTIME_PLATFORM_ANDROID
    this->monitoring = false;
    const auto& androidContext = this->context.android;
    if (!androidContext.jvm || !androidContext.self) {
      return true;
    }
    const auto attachment = android::JNIEnvironmentAttachment(androidContext.jvm);
    return CallClassMethodFromAndroidEnvironment(
      attachment.env, Boolean, androidContext.self, "stopNetworkStatusMonitor", "()Z"
    );
  #endif
    return false;
  }

#if ORO_RUNTIME_PLATFORM_LINUX
  JSON::Object NetworkStatus::snapshot () const {
    const bool available = this->enabled && this->monitor;
    const bool connected = available && g_network_monitor_get_network_available(this->monitor);
    const auto connectivity = available ? g_network_monitor_get_connectivity(this->monitor) : G_NETWORK_CONNECTIVITY_LOCAL;
    const bool validated = connected && connectivity == G_NETWORK_CONNECTIVITY_FULL;
    return JSON::Object::Entries {
      {"platform", "linux"}, {"available", available}, {"connected", connected}, {"validated", validated},
      {"captivePortal", connected && connectivity == G_NETWORK_CONNECTIVITY_PORTAL},
      {"metered", available && g_network_monitor_get_network_metered(this->monitor)},
      {"transports", JSON::Array {}}, {"name", validated ? "online" : "offline"},
      {"transportDetailsAvailable", false}, {"serverReachabilityKnown", false}
    };
  }
#endif

  bool NetworkStatus::addObserver (const Observer& observer, const Observer::Callback callback) {
    return this->observers.add(observer, callback);
  }

#if ORO_RUNTIME_PLATFORM_ANDROID
  void NetworkStatus::report (const JSON::Object& status) {
    Lock lock(this->statusMutex);
    this->latestStatus = status;
    if (this->updatePending || !this->monitoring) {
      return;
    }
    this->updatePending = true;
    if (!this->loop.dispatch([this]() {
      JSON::Object current;
      {
        Lock lock(this->statusMutex);
        current = this->latestStatus;
        this->updatePending = false;
      }
      if (this->monitoring) {
        this->observers.dispatch(current);
      }
    })) {
      this->updatePending = false;
    }
  }
#endif

  bool NetworkStatus::removeObserver (const Observer& observer) {
    return this->observers.remove(observer);
  }
}
