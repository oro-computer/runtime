#define ORO_RUNTIME_DESKTOP_EXTENSION 1

#include "../../runtime.hh"

#include "../extension.hh"

#include <chrono>
#include <memory>

using namespace oro;
using namespace oro::desktop;
using namespace oro::runtime::types;

namespace JSON = oro::runtime::JSON;
namespace ipc = oro::runtime::ipc;

using oro::runtime::config::getUserConfig;
using oro::runtime::bridge::Bridge;
using oro::runtime::Runtime;
using oro::runtime::setcwd;
using oro::runtime::app::App;

#if ORO_RUNTIME_PLATFORM_LINUX
#if defined(__cplusplus)
extern "C" {
#endif
  static bool isMainApplicationDebugEnabled = false; // TODO(@jwerle): handle how this value is configured

  static SharedPointer<Bridge> sharedBridge = nullptr;
  static WebExtensionContext sharedContext;
  static GMainContext* sharedMainContext = nullptr;
  static JSCContext* sharedJSCContext = nullptr;
  static Mutex sharedMutex;

  struct SyncRouteState {
    BinarySemaphore semaphore {0};
    Mutex mutex;
    bool completed = false;
    ipc::Result result;
  };

  static void setSharedMainContext (GMainContext* context) {
    Lock lock(sharedMutex);
    if (context == nullptr) {
      context = g_main_context_default();
    }

    if (sharedMainContext == context) {
      return;
    }

    if (sharedMainContext != nullptr) {
      g_main_context_unref(sharedMainContext);
    }

    sharedMainContext = g_main_context_ref(context);
  }

  static GMainContext* getSharedMainContext () {
    Lock lock(sharedMutex);
    if (sharedMainContext == nullptr) {
      sharedMainContext = g_main_context_ref(g_main_context_default());
    }

    return g_main_context_ref(sharedMainContext);
  }

  static void setSharedJSCContext (JSCContext* context) {
    Lock lock(sharedMutex);
    if (sharedJSCContext == context) {
      return;
    }

    if (sharedJSCContext != nullptr) {
      g_object_unref(sharedJSCContext);
    }

    sharedJSCContext = context;
    if (sharedJSCContext != nullptr) {
      g_object_ref(sharedJSCContext);
    }
  }

  static JSCContext* getSharedJSCContext () {
    Lock lock(sharedMutex);
    if (sharedJSCContext != nullptr) {
      g_object_ref(sharedJSCContext);
    }

    return sharedJSCContext;
  }

  static void dispatchToWebExtensionContext (Function<void()> callback) {
    if (callback == nullptr) {
      return;
    }

    auto context = getSharedMainContext();
    // invoke_full may execute inline on a worker when the context is idle.
    // JSC results must always be delivered by WebKit's owning main context.
    auto source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
      source,
      +[](gpointer userData) -> gboolean {
        auto callback = reinterpret_cast<Function<void()>*>(userData);
        (*callback)();
        return G_SOURCE_REMOVE;
      },
      new Function<void()>(std::move(callback)),
      +[](gpointer userData) {
        delete reinterpret_cast<Function<void()>*>(userData);
      }
    );
    g_source_attach(source, context);
    g_source_unref(source);
    g_main_context_unref(context);
  }

  static SharedPointer<Bridge> getSharedBridge (JSCContext* context) {
    static auto app = App::sharedApplication();
    Lock lock(sharedMutex);

    setSharedJSCContext(context);

    if (sharedBridge == nullptr) {
      sharedBridge = app->runtime.bridgeManager.get(0, {});
      sharedBridge->userConfig = app->runtime.userConfig;
      sharedBridge->dispatchHandler = [](auto callback) { callback(); };
      sharedBridge->dispatchRouterCallbacksWithBridge = true;
      sharedBridge->evaluateJavaScriptHandler = [] (const auto source) {
        dispatchToWebExtensionContext([source] () {
          auto context = getSharedJSCContext();
          if (context == nullptr) {
            return;
          }

          (void) jsc_context_evaluate(context, source.c_str(), source.size());
          g_object_unref(context);
        });
      };
      sharedBridge->init();
    }

    return sharedBridge;
  }

  static void onMessageResolver (
    JSCValue* resolve,
    JSCValue* reject,
    runtime::ipc::Message* message
  ) {
    auto context = jsc_value_get_context(resolve);
    auto bridge = getSharedBridge(context);

    g_object_ref(context);
    g_object_ref(resolve);
    g_object_ref(reject);

    auto routed = bridge->route(message->str(), message->buffer, [=](auto result) {
      dispatchToWebExtensionContext([=] () {
        if (result.queuedResponse.body != nullptr) {
          auto array = jsc_value_new_typed_array(
            context,
            JSC_TYPED_ARRAY_UINT8,
            result.queuedResponse.length
          );

          gsize size = 0;
          auto bytes = jsc_value_typed_array_get_data(array, &size);
          memcpy(bytes, result.queuedResponse.body.get(), size);

          const auto _ = jsc_value_function_call(
            resolve,
            JSC_TYPE_VALUE,
            array,
            G_TYPE_NONE
          );
        } else {
          const auto json = result.json().str();
          if (json.size() > 0) {
            auto _ = jsc_value_function_call(
              resolve,
              JSC_TYPE_VALUE,
              jsc_value_new_string(context, json.c_str()),
              G_TYPE_NONE
            );
          }
        }

        g_object_unref(context);
        g_object_unref(resolve);
        g_object_unref(reject);
      });
    });

    if (!routed) {
      const auto json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"message", "Not found"},
          {"type", "NotFoundError"},
          {"source", message->name}
        }}
      };

      const auto _ = jsc_value_function_call(
        resolve,
        JSC_TYPE_VALUE,
        jsc_value_new_string(context, JSON::Object(json).str().c_str()),
        G_TYPE_NONE
      );

      g_object_unref(context);
      g_object_unref(resolve);
      g_object_unref(reject);
    }

    delete message;
  }

  static JSCValue* onMessage (const char* source, JSCValue* value, gpointer userData) {
    auto context = reinterpret_cast<JSCContext*>(userData);
    auto Promise = jsc_context_get_value(context, "Promise");
    auto message = new ipc::Message(source);

    if (value != nullptr && jsc_value_is_typed_array(value)) {
      size_t size = 0;
      auto bytes = jsc_value_typed_array_get_data(value, &size);
      if (bytes != nullptr && size > 0) {
        message->buffer.push(reinterpret_cast<const char*>(bytes), size);
      }
    }

    if (message->get("__sync__") == "true") {
      auto bridge = getSharedBridge(context);
      auto state = std::make_shared<SyncRouteState>();
      const auto operation = message->name;
      if (oro::runtime::env::has("ORO_DEBUG_DISPATCH")) {
        debug("Linux sync IPC: dedicated=%d bridgeDispatch=%d alive=%d",
          bridge->getRuntime()->loop.options.dedicatedThread,
          bridge->dispatchRouterCallbacksWithBridge,
          bridge->getRuntime()->loop.alive());
      }

      const auto routed = bridge->route(
        message->str(),
        message->buffer,
        [state] (auto result) mutable {
          bool shouldRelease = false;
          {
            Lock lock(state->mutex);
            if (!state->completed) {
              state->result = std::move(result);
              state->completed = true;
              shouldRelease = true;
            }
          }

          if (shouldRelease) {
            state->semaphore.release();
          }
        }
      );

      delete message;

      if (routed) {
        const auto completed = state->semaphore.try_acquire_for(
          std::chrono::seconds(30)
        );

        if (!completed) {
          const auto json = JSON::Object::Entries {
            {"err", JSON::Object::Entries {
              {"message", "Desktop Linux extension IPC timed out"},
              {"type", "TimeoutError"},
              {"source", operation}
            }}
          };

          return jsc_value_new_string(context, JSON::Object(json).str().c_str());
        }

        ipc::Result returnResult;
        {
          Lock lock(state->mutex);
          returnResult = std::move(state->result);
        }

        if (returnResult.queuedResponse.body != nullptr) {
          auto array = jsc_value_new_typed_array(
            context,
            JSC_TYPED_ARRAY_UINT8,
            returnResult.queuedResponse.length
          );

          gsize size = 0;
          auto bytes = jsc_value_typed_array_get_data(array, &size);
          memcpy(bytes, returnResult.queuedResponse.body.get(), size);
          return array;
        } else {
          auto json = returnResult.json().str();
          if (json.size() > 0) {
            return jsc_value_new_string(context, json.c_str());
          }
        }
      }

      const auto json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"message", "Not found"},
          {"type", "NotFoundError"},
          {"source", operation}
        }}
      };

      return jsc_value_new_string(context, JSON::Object(json).str().c_str());
    }

    auto resolver = jsc_value_new_function(
      context,
      nullptr,
      G_CALLBACK(onMessageResolver),
      message,
      nullptr,
      G_TYPE_NONE,
      2,
      JSC_TYPE_VALUE,
      JSC_TYPE_VALUE
    );

    auto promise = jsc_value_constructor_call(
      Promise,
      JSC_TYPE_VALUE,
      resolver,
      G_TYPE_NONE
    );

    g_object_unref(Promise);
    g_object_unref(resolver);
    return promise;
  }

  static void onDocumentLoaded (
    WebKitWebPage* page,
    gpointer userData
  ) {
    // WebKitGTK 4.1 deprecates webkit_web_page_get_main_frame without a
    // non-deprecated replacement yet; suppress the deprecation locally and
    // obtain the JS context for the default script world.
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    #elif defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #endif
    WebKitFrame* frame = webkit_web_page_get_main_frame(page);
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #elif defined(__GNUC__)
    #pragma GCC diagnostic pop
    #endif
    auto context = webkit_frame_get_js_context_for_script_world(
      frame,
      webkit_script_world_get_default()
    );
    auto mainContext = g_main_context_ref_thread_default();
    setSharedMainContext(mainContext);
    if (mainContext != nullptr) {
      g_main_context_unref(mainContext);
    }

    auto __global_ipc_extension_handler = jsc_value_new_function(
      context,
      "__global_ipc_extension_handler",
      G_CALLBACK(onMessage),
      context,
      nullptr,
      JSC_TYPE_VALUE,
      2,
      G_TYPE_STRING,
      JSC_TYPE_VALUE
    );

    jsc_context_set_value(
      context,
      "__global_ipc_extension_handler",
      __global_ipc_extension_handler
    );
  }

  static gboolean onLifecycleMessage (
    WebKitWebPage* page,
    WebKitUserMessage* message,
    gpointer userData
  ) {
    if (g_strcmp0(webkit_user_message_get_name(message), "oro.runtime.lifecycle") != 0) {
      return false;
    }
    const auto parameters = webkit_user_message_get_parameters(message);
    if (parameters == nullptr || !g_variant_is_of_type(parameters, G_VARIANT_TYPE_STRING)) {
      return false;
    }
    const auto event = g_variant_get_string(parameters, nullptr);
    const auto app = App::sharedApplication();
    if (app == nullptr) {
      return false;
    }
    if (g_strcmp0(event, "applicationpause") == 0) {
      app->pause();
    } else if (g_strcmp0(event, "applicationresume") == 0) {
      app->resume();
    } else {
      return false;
    }
    return true;
  }

  static void onPageCreated (
    WebKitWebExtension* extension,
    WebKitWebPage* page,
    gpointer userData
  ) {
    auto userConfig = getUserConfig();
    g_signal_connect(page, "user-message-received", G_CALLBACK(onLifecycleMessage), nullptr);
    g_signal_connect(
      page,
      "document-loaded",
      G_CALLBACK(onDocumentLoaded),
      nullptr
    );
  }

  G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data (
    WebKitWebExtension* extension,
    const GVariant* userData
  ) {
    g_signal_connect(
      extension,
      "page-created",
      G_CALLBACK(onPageCreated),
      nullptr
    );

    if (!sharedContext.config.bytes && userData != nullptr) {
      auto mutableUserData = const_cast<GVariant*>(userData);
      GVariant* bytesVariant = nullptr;
      gint32 formatValue = 0;

      if (g_variant_is_of_type(mutableUserData, G_VARIANT_TYPE("(ayi)"))) {
        g_variant_get(mutableUserData, "(@ayi)", &bytesVariant, &formatValue);
      } else {
        bytesVariant = mutableUserData;
        g_variant_ref(bytesVariant);
      }

      if (bytesVariant != nullptr) {
        sharedContext.config.size = g_variant_get_size(bytesVariant);
        if (sharedContext.config.size) {
          sharedContext.config.bytes = reinterpret_cast<char*>(new unsigned char[sharedContext.config.size]{0});
          memcpy(
            sharedContext.config.bytes,
            g_variant_get_data(bytesVariant),
            sharedContext.config.size
          );
        }
        sharedContext.config.format = static_cast<int>(formatValue);
        g_variant_unref(bytesVariant);
      }
    }

    Runtime::Features features;
    features.usePlatform = true;
    features.useTimers = true;
    features.useFS = true;

    features.useNotifications = false;
    features.useNetworkStatus = false;
    features.usePermissions = false;
    features.useGeolocation = false;
    features.useConduit = false;
    features.useUDP = false;
    features.useDNS = false;
    features.useAI = false;
#if ORO_RUNTIME_HAS_IROH_FFI
    features.useIroh = true;
#else
    features.useIroh = false;
#endif

    auto userConfig = sharedContext.config.bytes && sharedContext.config.size > 0
      ? oro::runtime::config::parseUserConfigSource(
        String(sharedContext.config.bytes, sharedContext.config.size),
        sharedContext.config.format == static_cast<int>(oro::runtime::config::UserConfigFormat::Toml)
          ? oro::runtime::config::UserConfigFormat::Toml
          : oro::runtime::config::UserConfigFormat::Ini
      )
      : getUserConfig();
    const auto cwd = userConfig["web-process-extension_cwd"];

    // The core archive is also linked by WebKit's helper executable. Its
    // executable directory is not the application's resource directory.
    if (!cwd.empty() && oro::runtime::filesystem::Resource::setResourcesPath(cwd)) {
      setcwd(cwd);
      uv_chdir(cwd.c_str());
    }

    static App app(App::Options {
      .userConfig = userConfig,
      .loop = { .dedicatedThread = true },
      .features = features
    });

    // The web extension is initialized while WebKit is still constructing the
    // WebProcess. Entering GTK's main loop here can run pending WebKit work
    // before ServiceWorkerProvider is installed, which WebKitGTK 2.52 rejects
    // when worker clients register with the service worker subsystem.
    app.isStarted = true;
  }

  const unsigned char* oro_runtime_init_get_user_config_bytes () {
    return reinterpret_cast<const unsigned char*>(sharedContext.config.bytes);
  }

  unsigned int oro_runtime_init_get_user_config_bytes_size () {
    return sharedContext.config.size;
  }

  bool oro_runtime_init_is_debug_enabled () {
    return isMainApplicationDebugEnabled;
  }

  const char* oro_runtime_init_get_dev_host () {
    auto userConfig = getUserConfig();
    if (userConfig.contains("web-process-extension_host")) {
      return userConfig["web-process-extension_host"].c_str();
    }
    return "";
  }

  int oro_runtime_init_get_dev_port () {
    auto userConfig = getUserConfig();
    if (userConfig.contains("web-process-extension_port")) {
      try {
        return std::stoi(userConfig["web-process-extension_port"]);;
      } catch (...) {}
    }

    return 0;
  }

  int oro_runtime_init_get_user_config_format () {
    return sharedContext.config.format;
  }
#if defined(__cplusplus)
}
#endif
#endif
