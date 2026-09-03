#include <fstream>
#include <sys/utsname.h>

#include "../filesystem.hh"
#include "../javascript.hh"
#include "../runtime.hh"
#include "../version.hh"
#include "../config.hh"
#include "../string.hh"
#include "../bytes.hh"
#include "../color.hh"
#include "../cwd.hh"
#include "../env.hh"
#include "../app.hh"
#include "../url.hh"
#include "../webview/tls_pins.hh"

#include "../window.hh"

#include <gio/gio.h>

using oro::runtime::javascript::getResolveMenuSelectionJavaScript;
using oro::runtime::javascript::getEmitToRenderProcessJavaScript;

using oro::runtime::config::getDevHost;
using oro::runtime::config::getDevPort;
using oro::runtime::config::UserConfigFormat;

using oro::runtime::string::toLowerCase;
using oro::runtime::string::replace;
using oro::runtime::string::split;
using oro::runtime::string::trim;
using oro::runtime::string::tmpl;

using oro::runtime::bytes::decodeHexString;

using oro::runtime::webview::TlsPinMap;
using oro::runtime::webview::parseTlsPinConfig;
using oro::runtime::webview::isCertificateAllowedForHost;
using oro::runtime::webview::hasPinsForHost;
using oro::runtime::webview::normaliseTlsPinHost;
using oro::runtime::url::URL;

using oro::runtime::color::Color;

using oro::runtime::app::App;
using oro::runtime::String;

static GtkTargetEntry droppableTypes[] = {
  { (char*) "text/uri-list", 0, 0 }
};

#define DEFAULT_MONITOR_WIDTH 720
#define DEFAULT_MONITOR_HEIGHT 364

  static String escapeTomlString (const String& value) {
    String escaped;
    escaped.reserve(value.size());

    for (const auto ch : value) {
      switch (ch) {
        case '\\': escaped.append("\\\\"); break;
        case '"':  escaped.append("\\\""); break;
        case '\n': escaped.append("\\n"); break;
        case '\r': escaped.append("\\r"); break;
        case '\t': escaped.append("\\t"); break;
        default:   escaped.push_back(ch); break;
      }
    }

    return escaped;
  }

namespace oro::runtime::window {
  static Map<String, WebKitWebContext*> webContexts;
  static Mutex webContextMutex;

  static double clamp01 (double value) {
    if (value < 0.0) {
      return 0.0;
    } else if (value > 1.0) {
      return 1.0;
    }

    return value;
  }

  static GtkCssProvider* ensureBackgroundProvider (GtkWidget* widget) {
    if (widget == nullptr) {
      return nullptr;
    }

    auto provider = static_cast<GtkCssProvider*>(
      g_object_get_data(G_OBJECT(widget), "oro-runtime-background-provider")
    );

    if (provider != nullptr) {
      return provider;
    }

    provider = gtk_css_provider_new();

    auto context = gtk_widget_get_style_context(widget);
    gtk_style_context_add_provider(
      context,
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    g_object_set_data_full(
      G_OBJECT(widget),
      "oro-runtime-background-provider",
      provider,
      (GDestroyNotify) g_object_unref
    );

    return provider;
  }

  static void setWidgetBackgroundColor (GtkWidget* widget, const GdkRGBA& color) {
    auto provider = ensureBackgroundProvider(widget);
    if (provider == nullptr) {
      return;
    }

    char css[128];
    const double red = clamp01(color.red);
    const double green = clamp01(color.green);
    const double blue = clamp01(color.blue);
    const double alpha = clamp01(color.alpha);

    snprintf(
      css,
      sizeof(css),
      "* { background-color: rgba(%d, %d, %d, %.3f); }",
      static_cast<int>(red * 255.0 + 0.5),
      static_cast<int>(green * 255.0 + 0.5),
      static_cast<int>(blue * 255.0 + 0.5),
      alpha
    );

    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_widget_queue_draw(widget);
  }

  static bool gtkThemeNameIsDark (const char* themeName) {
    if (themeName == nullptr) {
      return false;
    }

    const auto lowered = toLowerCase(String(themeName));
    return lowered.find("dark") != String::npos;
  }

  static bool gtkSettingsPreferDarkTheme (GtkWidget* widget) {
    GtkSettings* settings = nullptr;

    if (widget != nullptr) {
      settings = gtk_widget_get_settings(widget);
    }

    if (settings == nullptr) {
      settings = gtk_settings_get_default();
    }

    if (settings == nullptr) {
      return false;
    }

    gboolean preferDark = FALSE;
    auto klass = G_OBJECT_GET_CLASS(settings);

    if (g_object_class_find_property(klass, "gtk-application-prefer-dark-theme")) {
      g_object_get(settings, "gtk-application-prefer-dark-theme", &preferDark, nullptr);
      if (preferDark) {
        return true;
      }
    }

    gchar* themeName = nullptr;

    if (g_object_class_find_property(klass, "gtk-theme-name")) {
      g_object_get(settings, "gtk-theme-name", &themeName, nullptr);
      if (gtkThemeNameIsDark(themeName)) {
        g_free(themeName);
        return true;
      }
    }

    if (themeName) {
      g_free(themeName);
    }

    return false;
  }

  static bool widgetBackgroundIsDark (GtkWidget* widget) {
    if (widget == nullptr) {
      return false;
    }

    auto context = gtk_widget_get_style_context(widget);
    if (context == nullptr) {
      return false;
    }

    const auto state = gtk_style_context_get_state(context);
    GdkRGBA backgroundColor = {0};

    gtk_style_context_get(
      context,
      state,
      "background-color",
      &backgroundColor,
      nullptr
    );

    const bool isTransparent = backgroundColor.alpha <= 0.0;

    if (isTransparent) {
      return false;
    }

    const double luminance = (0.299 * backgroundColor.red) +
                             (0.587 * backgroundColor.green) +
                             (0.114 * backgroundColor.blue);

    return luminance < 0.5;
  }

  static bool detectKdeDarkMode () {
    static const auto paths = filesystem::Resource::getWellKnownPaths();
    static const auto kdeglobals = paths.home / ".config" / "kdeglobals";

    if (!filesystem::Resource::isFile(kdeglobals)) {
      return false;
    }

    auto file = filesystem::Resource(kdeglobals);

    if (!file.exists() || !file.hasAccess()) {
      return false;
    }

    const auto bytes = reinterpret_cast<const char*>(file.read());
    const auto lines = split(bytes, '\n');

    for (const auto& line : lines) {
      if (toLowerCase(line).find("dark") != String::npos) {
        return true;
      }
    }

    return false;
  }

  static bool detectGnomeDarkMode (GtkWidget* widget) {
    if (gtkSettingsPreferDarkTheme(widget)) {
      return true;
    }

    return widgetBackgroundIsDark(widget);
  }

  static bool detectSystemDarkMode (GtkWidget* widget) {
    const gchar* desktop = getenv("XDG_CURRENT_DESKTOP");

    if (desktop != nullptr) {
      if (g_str_has_prefix(desktop, "GNOME")) {
        return detectGnomeDarkMode(widget);
      }

      if (g_str_has_prefix(desktop, "KDE")) {
        return detectKdeDarkMode();
      }
    }

    if (gtkSettingsPreferDarkTheme(widget)) {
      return true;
    }

    return widgetBackgroundIsDark(widget);
  }

  static void applyWindowAppearance (Window* window) {
    if (window == nullptr || window->window == nullptr) {
      return;
    }

    const bool hasDarkValue = window->options.backgroundColorDark.size();
    const bool hasLightValue = window->options.backgroundColorLight.size();

    window->isDarkMode = window->options.followSystemTheme
      ? detectSystemDarkMode(window->window)
      : window->options.preferDarkTheme;

    if (hasDarkValue || hasLightValue) {
      GdkRGBA color = {0};
      bool parsedColor = false;

      if (window->isDarkMode && hasDarkValue) {
        parsedColor = gdk_rgba_parse(&color, window->options.backgroundColorDark.c_str());
      } else if (!window->isDarkMode && hasLightValue) {
        parsedColor = gdk_rgba_parse(&color, window->options.backgroundColorLight.c_str());
      } else if (hasDarkValue) {
        parsedColor = gdk_rgba_parse(&color, window->options.backgroundColorDark.c_str());
      } else if (hasLightValue) {
        parsedColor = gdk_rgba_parse(&color, window->options.backgroundColorLight.c_str());
      }

      if (parsedColor) {
        setWidgetBackgroundColor(window->window, color);
        window->backgroundColor = Color(
          static_cast<unsigned int>(color.red * 255.0 + 0.5),
          static_cast<unsigned int>(color.green * 255.0 + 0.5),
          static_cast<unsigned int>(color.blue * 255.0 + 0.5),
          static_cast<float>(color.alpha)
        );
        return;
      }
    }

    auto context = gtk_widget_get_style_context(window->window);

    if (context != nullptr) {
      GdkRGBA fallbackColor = {0};
      gtk_style_context_get(
        context,
        gtk_widget_get_state_flags(window->window),
        "background-color",
        &fallbackColor,
        nullptr
      );

      window->backgroundColor = Color(
        static_cast<unsigned int>(fallbackColor.red * 255.0 + 0.5),
        static_cast<unsigned int>(fallbackColor.green * 255.0 + 0.5),
        static_cast<unsigned int>(fallbackColor.blue * 255.0 + 0.5),
        static_cast<float>(fallbackColor.alpha)
      );
    } else {
      window->backgroundColor = Color();
    }
  }

  struct GtkThemeMonitorData {
    GtkSettings* settings = nullptr;
    gulong preferHandler = 0;
    gulong nameHandler = 0;
  };

  static void disconnectGtkThemeMonitor (gpointer data) {
    auto monitor = static_cast<GtkThemeMonitorData*>(data);

    if (monitor == nullptr) {
      return;
    }

    if (monitor->settings != nullptr) {
      if (monitor->preferHandler > 0) {
        g_signal_handler_disconnect(monitor->settings, monitor->preferHandler);
      }

      if (monitor->nameHandler > 0) {
        g_signal_handler_disconnect(monitor->settings, monitor->nameHandler);
      }

      g_object_unref(monitor->settings);
    }

    g_free(monitor);
  }

  static void onGtkThemeSettingChanged (
    GtkSettings* /*settings*/,
    GParamSpec* /*pspec*/,
    gpointer userData
  ) {
    auto window = static_cast<Window*>(userData);

    if (window == nullptr || !window->options.followSystemTheme) {
      return;
    }

    const bool previousDarkMode = window->isDarkMode;
    applyWindowAppearance(window);

    if (previousDarkMode == window->isDarkMode) {
      return;
    }

    JSON::Object payload = JSON::Object::Entries {
      {"isDarkMode", window->isDarkMode},
      {"followSystemTheme", window->options.followSystemTheme},
      {"preferDarkTheme", window->options.preferDarkTheme},
      {"backgroundColor", window->backgroundColor.str()}
    };

    window->eval(getEmitToRenderProcessJavaScript(
      "systemthemechange",
      payload.str()
    ));
  }

  static void ensureGtkThemeMonitor (Window* window) {
    if (window == nullptr || window->window == nullptr) {
      return;
    }

    if (g_object_get_data(G_OBJECT(window->window), "oro-runtime-theme-monitor")) {
      return;
    }

    GtkSettings* settings = gtk_widget_get_settings(window->window);

    if (settings == nullptr) {
      settings = gtk_settings_get_default();
    }

    if (settings == nullptr) {
      return;
    }

    auto monitor = static_cast<GtkThemeMonitorData*>(
      g_malloc0(sizeof(GtkThemeMonitorData))
    );

    monitor->settings = GTK_SETTINGS(g_object_ref(settings));
    monitor->preferHandler = g_signal_connect(
      settings,
      "notify::gtk-application-prefer-dark-theme",
      G_CALLBACK(onGtkThemeSettingChanged),
      window
    );
    monitor->nameHandler = g_signal_connect(
      settings,
      "notify::gtk-theme-name",
      G_CALLBACK(onGtkThemeSettingChanged),
      window
    );

    g_object_set_data_full(
      G_OBJECT(window->window),
      "oro-runtime-theme-monitor",
      monitor,
      disconnectGtkThemeMonitor
    );
  }

  struct WebContextNotificationPermissionsData {
    String bundleIdentifier;
    bool areNotificationsAllowed = false;
  };

  struct WeakWindowCallbackData {
    std::weak_ptr<Window> weak;
  };

  static WebKitWebContext* initializeWebContextFromWindow (Window* window) {
    Lock lock(webContextMutex);
    WebKitWebContext* webContext = nullptr;
    const auto bundleIdentifier = window->bridge->userConfig["meta_bundle_identifier"];

    if (webContexts.contains(bundleIdentifier)) {
      window->bridge->webContext = webContexts.at(bundleIdentifier);
      return window->bridge->webContext;
    }

    webContext = webkit_web_context_new();
    window->bridge->webContext = webContext;
    webContexts[bundleIdentifier] = webContext;

    auto cookieManager = webkit_web_context_get_cookie_manager(webContext);
    if (cookieManager != nullptr) {
      const auto areCookiesAllowed = (
        !window->bridge->userConfig.contains("permissions_allow_cookies") ||
        window->bridge->userConfig.at("permissions_allow_cookies") != "false"
      );

      if (areCookiesAllowed) {
        const auto paths = filesystem::Resource::getWellKnownPaths();
        if (!paths.data.empty()) {
          const auto cookieDirectory = paths.data / "webkit";
          std::error_code error;
          fs::create_directories(cookieDirectory, error);

          if (!error) {
            const auto cookieDirectoryString = cookieDirectory.string();
            if (!cookieDirectoryString.empty()) {
              webkit_web_context_add_path_to_sandbox(
                webContext,
                cookieDirectoryString.c_str(),
                false
              );
            }

            const auto cookieStorePath = (cookieDirectory / "cookies.sqlite").string();
            webkit_cookie_manager_set_persistent_storage(
              cookieManager,
              cookieStorePath.c_str(),
              WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE
            );
          }
        }
      } else {
        webkit_cookie_manager_set_accept_policy(cookieManager, WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
      }
    }

    // Enable WebKit automation when CDP/remote debugging is enabled so external
    // automation tools (e.g. Playwright WebKit drivers) can attach.
    if (
      window->bridge->userConfig.contains("cdp_remote_debugging_port") ||
      env::has("ORO_REMOTE_DEBUGGING_PORT") ||
      env::has("ORO_RUNTIME_REMOTE_DEBUGGING_PORT")
    ) {
      webkit_web_context_set_automation_allowed(webContext, TRUE);
    }

    // mounts are configured for all contexts just once
    window->bridge->configureNavigatorMounts();

    auto* notificationPermissionsData = new WebContextNotificationPermissionsData {
      .bundleIdentifier = bundleIdentifier,
      .areNotificationsAllowed = (
        !window->bridge->userConfig.contains("permissions_allow_notifications") ||
        window->bridge->userConfig.at("permissions_allow_notifications") != "false"
      )
    };

    g_signal_connect_data(
      G_OBJECT(webContext),
      "initialize-notification-permissions",
      G_CALLBACK(+[](
        WebKitWebContext* webContext,
        gpointer userData
      ) {
        const auto* data = static_cast<WebContextNotificationPermissionsData*>(userData);

        if (data == nullptr) {
          return;
        }

        auto configurePermissionForScheme = [&] (const String& scheme) {
          const auto uri = scheme + "://" + data->bundleIdentifier;
          const auto origin = webkit_security_origin_new_for_uri(uri.c_str());

          if (!origin) {
            return;
          }

          GList* allowed = nullptr;
          GList* disallowed = nullptr;

          webkit_security_origin_ref(origin);

          if (data->areNotificationsAllowed) {
            disallowed = g_list_append(disallowed, (gpointer) origin);
          } else {
            allowed = g_list_append(allowed, (gpointer) origin);
          }

          if (allowed || disallowed) {
            webkit_web_context_initialize_notification_permissions(
              webContext,
              allowed,
              disallowed
            );
          }

          if (allowed) {
            g_list_free(allowed);
          }

          if (disallowed) {
            g_list_free(disallowed);
          }

          webkit_security_origin_unref(origin);
        };

        configurePermissionForScheme("oro");
      }),
      notificationPermissionsData,
      +[](gpointer userData, GClosure* /*closure*/) {
        delete static_cast<WebContextNotificationPermissionsData*>(userData);
      },
      GConnectFlags(0)
    );

    const auto extensionsPath = filesystem::Resource::getResourcePath(Path("lib/extensions"));
    if (filesystem::Resource::isDirectory(extensionsPath)) {
      webkit_web_context_set_web_extensions_directory(
        webContext,
        extensionsPath.c_str()
      );

    #if ORO_RUNTIME_PLATFORM_LINUX
      webkit_web_context_add_path_to_sandbox(
        webContext,
        extensionsPath.c_str(),
        false
      );
    #endif
    }

    const auto bytes = oro_runtime_init_get_user_config_bytes();
    const auto size = oro_runtime_init_get_user_config_bytes_size();

    static String extensionInitializationData;
    if (bytes != nullptr && size > 0) {
      extensionInitializationData.assign(
        reinterpret_cast<const char*>(bytes),
        size
      );

      auto cwd = getcwd();
      if (cwd.empty()) {
        cwd = filesystem::Resource::getResourcesPath().string();
      }

      const auto configFormatValue = oro_runtime_init_get_user_config_format();
      const bool appendAsToml = configFormatValue == static_cast<int>(UserConfigFormat::Toml);
      auto appendStringKV = [&] (const char* key, const String& value) {
        if (value.empty()) {
          return;
        }
        extensionInitializationData += key;
        extensionInitializationData += " = ";
        if (appendAsToml) {
          extensionInitializationData += "\"";
          extensionInitializationData += escapeTomlString(value);
          extensionInitializationData += "\"\n";
        } else {
          extensionInitializationData += value;
          extensionInitializationData += "\n";
        }
      };
      auto appendIntKV = [&] (const char* key, int value) {
        extensionInitializationData += key;
        extensionInitializationData += " = ";
        extensionInitializationData += std::to_string(value);
        extensionInitializationData += "\n";
      };

      extensionInitializationData += "\n[web-process-extension]\n";
      appendStringKV("cwd", cwd);
      appendStringKV("host", getDevHost());
      appendIntKV("port", getDevPort());

    #if ORO_RUNTIME_PLATFORM_LINUX
      if (!cwd.empty()) {
        // Ensure the extension can reach its working directory.
        webkit_web_context_add_path_to_sandbox(
          webContext,
          cwd.c_str(),
          false
        );
      }
    #endif
    }

    if (!extensionInitializationData.empty()) {
      const auto configFormatValue = oro_runtime_init_get_user_config_format();
      auto bytesVariant = g_variant_new_from_data(
        G_VARIANT_TYPE("ay"),
        extensionInitializationData.c_str(),
        extensionInitializationData.size(),
        true,
        nullptr,
        nullptr
      );

      const auto formatValue = static_cast<gint32>(configFormatValue);
      GVariantBuilder builder;
      g_variant_builder_init(&builder, G_VARIANT_TYPE_TUPLE);
      g_variant_builder_add(&builder, "@ay", bytesVariant);
      g_variant_builder_add(&builder, "i", formatValue);
      auto userData = g_variant_builder_end(&builder);

      webkit_web_context_set_web_extensions_initialization_user_data(
        webContext,
        userData
      );
    }

    return webContext;
  }

  Window::Window (SharedPointer<bridge::Bridge> bridge, const Window::Options& options)
    : options(options),
      bridge(bridge),
      hotkey(this),
      dialog(this) {
    env::set("GTK_OVERLAY_SCROLLING", "1");

    auto userConfig = options.userConfig;
    auto webContext = initializeWebContextFromWindow(this);

    this->settings = webkit_settings_new();
    // TODO(@jwerle); make configurable with '[permissions] allow_media'
    webkit_settings_set_zoom_text_only(this->settings, false);
    webkit_settings_set_media_playback_allows_inline(this->settings, true);
    // TODO(@jwerle); make configurable with '[permissions] allow_dialogs'
    webkit_settings_set_allow_modal_dialogs(this->settings, true);
    webkit_settings_set_hardware_acceleration_policy(
      this->settings,
      userConfig["permissions_hardware_acceleration_disabled"] == "true"
        ? WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
        : WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS
    );

    webkit_settings_set_enable_webgl(this->settings, true);
    webkit_settings_set_enable_media(this->settings, true);
    webkit_settings_set_enable_webaudio(this->settings, true);
    webkit_settings_set_enable_mediasource(this->settings, true);
    webkit_settings_set_enable_encrypted_media(this->settings, true);
    webkit_settings_set_enable_smooth_scrolling(this->settings, true);
    webkit_settings_set_enable_developer_extras(this->settings, options.debug);

    webkit_settings_set_enable_back_forward_navigation_gestures(
      this->settings,
      userConfig["webview_navigator_enable_navigation_gestures"] == "true"
    );

    webkit_settings_set_media_content_types_requiring_hardware_support(
      this->settings,
      nullptr
    );

    auto userAgent = String(webkit_settings_get_user_agent(settings));

    webkit_settings_set_user_agent(
      settings,
      (userAgent + " " + "OroRuntime/" + version::VERSION_STRING).c_str()
    );

    webkit_settings_set_enable_media_stream(
      this->settings,
      userConfig["permissions_allow_user_media"] != "false"
    );

    webkit_settings_set_enable_media_capabilities(
      this->settings,
      userConfig["permissions_allow_user_media"] != "false"
    );

    webkit_settings_set_enable_webrtc(
      this->settings,
      userConfig["permissions_allow_user_media"] != "false"
    );

    webkit_settings_set_javascript_can_access_clipboard(
      this->settings,
      userConfig["permissions_allow_clipboard"] != "false"
    );

    webkit_settings_set_enable_fullscreen(
      this->settings,
      userConfig["permissions_allow_fullscreen"] != "false"
    );

    webkit_settings_set_enable_html5_local_storage(
      this->settings,
      userConfig["permissions_allow_data_access"] != "false"
    );

    webkit_settings_set_enable_html5_database(
      this->settings,
      userConfig["permissions_allow_data_access"] != "false"
    );

    this->accelGroup = gtk_accel_group_new();
    this->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    auto cookieManager = webkit_web_context_get_cookie_manager(webContext);
    if (cookieManager != nullptr) {
      webkit_cookie_manager_set_accept_policy(
        cookieManager,
        userConfig["permissions_allow_cookies"] != "false"
          ? WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS
          : WEBKIT_COOKIE_POLICY_ACCEPT_NEVER
      );
    }

    this->userContentManager = webkit_user_content_manager_new();
    webkit_user_content_manager_register_script_message_handler(this->userContentManager, "external");

    auto preloadUserScriptSource = webview::Preload::compile({
      .features = webview::Preload::Options::Features {
        .useGlobalCommonJS = false,
        .useGlobalNodeJS = false,
        .useTestScript = false,
        .useHTMLMarkup = false,
        .useESM = false,
        .useGlobalArgs = true
      },
      .client = this->bridge->client,
      .index = options.index,
      .argv = options.argv,
      .userScript = options.userScript,
      .userConfig = options.userConfig,
      .conduit = {
        {"port", static_cast<runtime::Runtime&>(this->bridge->context).services.conduit.port},
        {"hostname", static_cast<runtime::Runtime&>(this->bridge->context).services.conduit.hostname},
        {"sharedKey", static_cast<runtime::Runtime&>(this->bridge->context).services.conduit.sharedKey}
      }
    });

    auto preloadUserScript = webkit_user_script_new(
      preloadUserScriptSource.str().c_str(),
      WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
      nullptr,
      nullptr
    );

    webkit_user_content_manager_add_script(
      this->userContentManager,
      preloadUserScript
    );

    this->policies = webkit_website_policies_new_with_policies(
      "autoplay", userConfig["permission_allow_autoplay"] != "false"
        ? WEBKIT_AUTOPLAY_ALLOW
        : WEBKIT_AUTOPLAY_DENY,
      nullptr
    );

    this->webview = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
      "user-content-manager", this->userContentManager,
      "website-policies", this->policies,
      "web-context", webContext,
      "settings", this->settings,
      nullptr
    ));

    gtk_widget_set_can_focus(GTK_WIDGET(this->webview), true);

    this->index = this->options.index;
    this->dragStart = {0,0};
    this->shouldDrag = false;
    this->contextMenu = nullptr;
    this->contextMenuID = 0;

    this->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    this->bridge->navigateHandler  = [this] (const auto url) {
      this->navigate(url);
    };

    this->bridge->evaluateJavaScriptHandler = [this] (const auto source) {
      this->eval(source);
    };

    this->bridge->client.preload = webview::Preload::compile({
      .features = options.features,
      .client = this->bridge->client,
      .index = options.index,
      .argv = options.argv,
      .userScript = options.userScript,
      .userConfig = options.userConfig,
      .conduit = {
        {"port", static_cast<runtime::Runtime&>(this->bridge->context).services.conduit.port},
        {"hostname", static_cast<runtime::Runtime&>(this->bridge->context).services.conduit.hostname},
        {"sharedKey", static_cast<runtime::Runtime&>(this->bridge->context).services.conduit.sharedKey}
      }
    });

    gtk_box_pack_end(GTK_BOX(this->vbox), GTK_WIDGET(this->webview), true, true, 0);

    gtk_container_add(GTK_CONTAINER(this->window), this->vbox);

    gtk_widget_add_events(this->window, GDK_ALL_EVENTS_MASK);
    gtk_widget_grab_focus(GTK_WIDGET(this->webview));
    gtk_widget_realize(GTK_WIDGET(this->window));

    // Application-level lifecycle hints for GTK:
    // Propagate lifecycle signals via App::{pause,resume} which, on desktop,
    // only emit events and do not actually pause/resume the runtime.
    g_signal_connect(
      G_OBJECT(this->window),
      "focus-in-event",
      G_CALLBACK(+[](
        GtkWidget* /*widget*/,
        GdkEventFocus* /*event*/,
        gpointer userData
      ) -> gboolean {
        auto self = reinterpret_cast<Window*>(userData);
        if (self) self->evalDomFocusThrottled();
        auto app = App::sharedApplication();
        if (app) app->resume();
        return FALSE;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->window),
      "focus-out-event",
      G_CALLBACK(+[](
        GtkWidget* /*widget*/,
        GdkEventFocus* /*event*/,
        gpointer userData
      ) -> gboolean {
        auto self = reinterpret_cast<Window*>(userData);
        if (self) self->evalDomBlurThrottled();
        auto app = App::sharedApplication();
        if (app) app->pause();
        return FALSE;
      }),
      this
    );

    // Iconify/deiconify: emit lifecycle events (desktop handlers are emit-only)
    g_signal_connect(
      G_OBJECT(this->window),
      "window-state-event",
      G_CALLBACK(+[](
        GtkWidget* /*widget*/,
        GdkEventWindowState* event,
        gpointer userData
      ) -> gboolean {
        auto app = App::sharedApplication();
        if (!app) return FALSE;
        auto self = reinterpret_cast<Window*>(userData);
        if (event->changed_mask & GDK_WINDOW_STATE_ICONIFIED) {
          if (event->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
            if (self) self->evalDomBlurThrottled();
            app->pause();
          } else {
            if (self) self->evalDomFocusThrottled();
            app->resume();
          }
        }
        return FALSE;
      }),
      this
    );

    if (options.resizable) {
      gtk_window_set_default_size(GTK_WINDOW(this->window), options.width, options.height);
    } else {
      gtk_widget_set_size_request(this->window, options.width, options.height);
    }

    gtk_window_set_decorated(GTK_WINDOW(this->window), options.frameless == false);
    gtk_window_set_resizable(GTK_WINDOW(this->window), options.resizable);
    gtk_window_set_position(GTK_WINDOW(this->window), GTK_WIN_POS_CENTER);
    gtk_widget_set_can_focus(GTK_WIDGET(this->window), true);

    GdkRGBA webviewBackground = {0.0, 0.0, 0.0, 0.0};

    applyWindowAppearance(this);

    if (this->options.followSystemTheme) {
      ensureGtkThemeMonitor(this);
    }

    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(webview), &webviewBackground);

    this->hotkey.init();
    this->bridge->init();
    this->bridge->configureSchemeHandlers({
      .webview = settings
    });

    this->bridge->configureWebView(this->webview);

    // TLS certificate pinning hook for WebKit.
    g_signal_connect(
      G_OBJECT(this->webview),
      "load-failed-with-tls-errors",
      G_CALLBACK(+[](
        WebKitWebView* webview,
        gchar* failingUri,
        GTlsCertificate* certificate,
        GTlsCertificateFlags errors,
        gpointer userData
      ) -> gboolean {
        auto window = static_cast<Window*>(userData);

        if (!window || !certificate || !failingUri) {
          return FALSE;
        }

        auto& userConfig = window->bridge->userConfig;
        auto it = userConfig.find("webview_tls_pins");
        if (it == userConfig.end() || it->second.empty()) {
          it = userConfig.find("webview.tls_pins");
          if (it == userConfig.end() || it->second.empty()) {
            return FALSE;
          }
        }

        const auto pins = parseTlsPinConfig(it->second, true);

        const auto uri = String(failingUri);
        const auto components = URL::Components::parse(uri);
        auto authority = trim(components.authority);
        const auto at = authority.rfind('@');
        if (at != String::npos) {
          authority = authority.substr(at + 1);
        }
        authority = trim(authority);

        if (authority.empty()) {
          return FALSE;
        }

        const auto scheme = toLowerCase(trim(components.scheme));
        const bool schemeHttps = scheme == "https";
        const bool schemeHttp = scheme == "http";

        const auto hasExplicitPort = [](const String& host) -> bool {
          if (host.empty()) {
            return false;
          }

          if (host.front() == '[') {
            const auto close = host.find(']');
            if (close == String::npos) {
              return false;
            }
            const auto suffix = close + 1 < host.size()
              ? host.substr(close + 1)
              : "";
            if (suffix.empty() || suffix.front() != ':' || suffix.size() == 1) {
              return false;
            }
            for (size_t i = 1; i < suffix.size(); ++i) {
              if (host[i + close + 1] < '0' || host[i + close + 1] > '9') {
                return false;
              }
            }
            return true;
          }

          const auto colon = host.rfind(':');
          if (colon == String::npos || colon == 0) {
            return false;
          }
          if (host.find(':') != colon) {
            return false;
          }
          const auto port = colon + 1 < host.size()
            ? host.substr(colon + 1)
            : "";
          if (port.empty()) {
            return false;
          }
          for (const auto ch : port) {
            if (ch < '0' || ch > '9') {
              return false;
            }
          }
          return true;
        };

        auto pinHost = authority;
        if ((schemeHttps || schemeHttp) && !hasExplicitPort(pinHost)) {
          const int defaultPort = schemeHttps ? 443 : 80;
          if (pinHost.find(':') != String::npos && pinHost.front() != '[') {
            // Likely an unbracketed IPv6 literal; do not append a port.
          } else {
            pinHost += ":" + std::to_string(defaultPort);
          }
        }

        // Preserve the platform default TLS behaviour when no pins apply to this host.
        if (!hasPinsForHost(pins, pinHost)) {
          return FALSE;
        }

        auto allowHost = authority;

        // Bracketed IPv6.
        if (!allowHost.empty() && allowHost.front() == '[') {
          const auto close = allowHost.find(']');
          if (close != String::npos && close > 1) {
            allowHost = allowHost.substr(1, close - 1);
          }
        } else {
          // Strip ':port' only when it looks numeric.
          const auto colon = allowHost.rfind(':');
          if (colon != String::npos) {
            const auto port = colon + 1 < allowHost.size()
              ? allowHost.substr(colon + 1)
              : "";
            bool isNumeric = !port.empty();
            for (const auto ch : port) {
              if (ch < '0' || ch > '9') {
                isNumeric = false;
                break;
              }
            }

            if (isNumeric && colon > 0 && allowHost.find(':') == colon) {
              allowHost = allowHost.substr(0, colon);
            }
          }
        }

        allowHost = toLowerCase(trim(allowHost));
        const auto host = normaliseTlsPinHost(allowHost);

        if (host.empty()) {
          return FALSE;
        }

        // Compute the SHA-256 digest of the DER certificate using GLib helpers.
        GByteArray* der = nullptr;
        g_object_get(G_OBJECT(certificate), "certificate", &der, nullptr);

        if (der == nullptr || der->data == nullptr || der->len == 0) {
          if (der != nullptr) {
            g_byte_array_unref(der);
          }
          return FALSE;
        }

        Vector<uint8_t> digest(32);
        gsize digestSize = digest.size();

        auto checksum = g_checksum_new(G_CHECKSUM_SHA256);
        if (checksum == nullptr) {
          g_byte_array_unref(der);
          return FALSE;
        }

        g_checksum_update(checksum, der->data, der->len);
        g_checksum_get_digest(checksum, digest.data(), &digestSize);
        g_checksum_free(checksum);
        g_byte_array_unref(der);

        if (digestSize != digest.size()) {
          return FALSE;
        }

        if (digest.empty()) {
          return FALSE;
        }

        if (!isCertificateAllowedForHost(pins, pinHost, digest)) {
          // Leave the TLS error in place.
          return FALSE;
        }

        auto webContext = window->bridge->webContext;
        if (!webContext) {
          webContext = webkit_web_view_get_context(webview);
        }

        if (!webContext) {
          return FALSE;
        }

        webkit_web_context_allow_tls_certificate_for_host(
          webContext,
          certificate,
          host.c_str()
        );
        if (!allowHost.empty() && allowHost != host) {
          webkit_web_context_allow_tls_certificate_for_host(
            webContext,
            certificate,
            allowHost.c_str()
          );
        }

        // Retry the navigation now that the certificate is pinned for this host.
        webkit_web_view_load_uri(webview, failingUri);
        return TRUE;
      }),
      this
    );

    g_signal_connect(
      this->userContentManager,
      "script-message-received::external",
      G_CALLBACK(+[](
        WebKitUserContentManager* userContentManager,
        WebKitJavascriptResult* result,
        gpointer ptr
      ) {
        auto window = static_cast<Window*>(ptr);
        if (!window) return;
        auto value = webkit_javascript_result_get_js_value(result);
        if (!value) return;
        auto valueString = jsc_value_to_string(value);
        if (!valueString) return;
        auto str = String(valueString);

        if (!window->bridge->route(str, nullptr, 0)) {
          if (window->onMessage != nullptr) {
            window->onMessage(str);
          }
        }

        g_free(valueString);
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "show-notification",
      G_CALLBACK(+[](
        WebKitWebView* webview,
        WebKitNotification* notification,
        gpointer userData
      ) -> bool {
        const auto window = reinterpret_cast<Window*>(userData);

        if (window == nullptr) {
          return true;
        }

        auto userConfig = window->bridge->userConfig;
        if (userConfig["permissions_allow_notifications"] == "false") {
          return true;
        }

        return false;
      }),
      this
    );

    // handle `navigator.permissions.query()`
    g_signal_connect(
      G_OBJECT(this->webview),
      "query-permission-state",
      G_CALLBACK((+[](
        WebKitWebView* webview,
        WebKitPermissionStateQuery* query,
        gpointer userData
      ) -> bool {
        const auto window = reinterpret_cast<Window*>(userData);
        const auto name = String(webkit_permission_state_query_get_name(query));

        if (window == nullptr) {
          return true;
        }

        const auto stateFor = [&](const char* key) {
          return window->bridge->userConfig[key] == "false"
            ? WEBKIT_PERMISSION_STATE_DENIED
            : WEBKIT_PERMISSION_STATE_GRANTED;
        };

        if (name == "geolocation") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_geolocation"));
        } else if (name == "notifications") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_notifications"));
        } else if (name == "camera") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_camera"));
        } else if (name == "microphone") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_microphone"));
        } else if (name == "clipboard-read" || name == "clipboard-write" || name == "clipboard") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_clipboard"));
        } else if (name == "persistent-storage") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_data_access"));
        } else if (name == "pointer-lock") {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_pointer_lock"));
        } else if (
          name == "accelerometer" ||
          name == "magnetometer" ||
          name == "gyroscope"
        ) {
          webkit_permission_state_query_finish(query, stateFor("permissions_allow_sensors"));
        } else {
          webkit_permission_state_query_finish(query, WEBKIT_PERMISSION_STATE_PROMPT);
        }
        return true;
      })),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "permission-request",
      G_CALLBACK((+[](
        WebKitWebView* webview,
        WebKitPermissionRequest* request,
        gpointer userData
      ) -> bool {
        Window* window = reinterpret_cast<Window*>(userData);
        auto& userConfig = window->bridge->userConfig;
        auto result = false;
        String name = "";
        String description = "{{meta_title}} would like permission to use a an unknown feature.";

        if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) {
          name = "geolocation";
          result = userConfig["permissions_allow_geolocation"] != "false";
          description = "{{meta_title}} would like access to your location.";
        } else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) {
          name = "notifications";
          result = userConfig["permissions_allow_notifications"] != "false";
          description = "{{meta_title}} would like display notifications.";
        } else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
          if (userConfig["permissions_allow_user_media"] != "false") {
            if (webkit_user_media_permission_is_for_audio_device(WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request))) {
              name = "microphone";
              result = userConfig["permissions_allow_microphone"] != "false";
              description = "{{meta_title}} would like access to your microphone.";
            }

            if (webkit_user_media_permission_is_for_video_device(WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request))) {
              name = "camera";
              result = userConfig["permissions_allow_camera"] != "false";
              description = "{{meta_title}} would like access to your camera.";
            }
          }
        } else if (WEBKIT_IS_POINTER_LOCK_PERMISSION_REQUEST(request)) {
          name = "pointer-lock";
          result = userConfig["permissions_allow_pointer_lock"] != "false";
          description = "{{meta_title}} would like to capture your pointer.";
        } else if (WEBKIT_IS_CLIPBOARD_PERMISSION_REQUEST(request)) {
          name = "clipboard";
          result = userConfig["permissions_allow_clipboard"] != "false";
          description = "{{meta_title}} would like to access your clipboard.";
        } else if (WEBKIT_IS_WEBSITE_DATA_ACCESS_PERMISSION_REQUEST(request)) {
          name = "storage-access";
          result = userConfig["permissions_allow_data_access"] != "false";
          description = "{{meta_title}} would like access to local storage.";
        } else if (WEBKIT_IS_DEVICE_INFO_PERMISSION_REQUEST(request)) {
          result = userConfig["permissions_allow_device_info"] != "false";
          description = "{{meta_title}} would like access to your device information.";
          if (result) {
            webkit_permission_request_allow(request);
            return result;
          }
        } else if (WEBKIT_IS_MEDIA_KEY_SYSTEM_PERMISSION_REQUEST(request)) {
          result = userConfig["permissions_allow_media_key_system"] != "false";
          description = "{{meta_title}} would like access to your media key system.";
        }

        if (result) {
          auto title = userConfig["meta_title"];
          GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(window->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "%s",
            tmpl(description, userConfig).c_str()
          );

          gtk_widget_show(dialog);
          if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            webkit_permission_request_allow(request);
          } else {
            webkit_permission_request_deny(request);
          }

          gtk_widget_destroy(dialog);
        } else {
          webkit_permission_request_deny(request);
        }

        if (name.size() > 0) {
          JSON::Object json = JSON::Object::Entries {
            {"name", name},
            {"state", result ? "granted" : "denied"}
          };
          // TODO: return this response data to callers instead of dropping it.
          // TODO: consider dispatching this state change to the webview.
        }

        return result;
      })),
      this
    );

    // Calling gtk_drag_source_set interferes with text selection
    /* gtk_drag_source_set(
      webview,
      (GdkModifierType)(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
      droppableTypes,
      G_N_ELEMENTS(droppableTypes),
      GDK_ACTION_COPY
    );

    gtk_drag_dest_set(
      webview,
      GTK_DEST_DEFAULT_ALL,
      droppableTypes,
      1,
      GDK_ACTION_MOVE
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "button-release-event",
      G_CALLBACK(+[](GtkWidget* wv, GdkEventButton* event, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        w->shouldDrag = false;
        w->dragStart.x = 0;
        w->dragStart.y = 0;
        w->dragging.x = 0;
        w->dragging.y = 0;
        return FALSE;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "button-press-event",
      G_CALLBACK(+[](GtkWidget* wv, GdkEventButton* event, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        w->shouldDrag = false;

        if (event->button == GDK_BUTTON_PRIMARY) {
          auto win = GDK_WINDOW(gtk_widget_get_window(w->window));
          gint initialX;
          gint initialY;

          gdk_window_get_position(win, &initialX, &initialY);

          w->dragStart.x = initialX;
          w->dragStart.y = initialY;

          w->dragging.x = event->x_root;
          w->dragging.y = event->y_root;

          GdkDevice* device;

          gint x = event->x;
          gint y = event->y;
          String sx = std::to_string(x);
          String sy = std::to_string(y);

          String js(
            "(() => {                                                                      "
            "  const v = '--app-region';                                                   "
            "  let el = document.elementFromPoint(" + sx + "," + sy + ");                  "
            "                                                                              "
            "  while (el) {                                                                "
            "    if (getComputedStyle(el).getPropertyValue(v) == 'drag') return 'movable'; "
            "    el = el.parentElement;                                                    "
            "  }                                                                           "
            "  return ''                                                                   "
            "})()                                                                          "
          );

          webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(wv),
            js.c_str(),
            -1,
            nullptr,
            nullptr,
            nullptr,
            [](GObject* src, GAsyncResult* result, gpointer arg) {
              std::unique_ptr<WeakWindowCallbackData> data(static_cast<WeakWindowCallbackData*>(arg));
              if (!data) {
                return;
              }

              const auto window = data->weak.lock();
              if (window == nullptr) {
                return;
              }

              GError* error = NULL;
              auto value = webkit_web_view_evaluate_javascript_finish(
                WEBKIT_WEB_VIEW(src),
                result,
                &error
              );

              if (error != nullptr) {
                g_error_free(error);
                return;
              }

              if (!value || !jsc_value_is_string(value)) {
                return;
              }

              auto* matchString = jsc_value_to_string(value);
              if (matchString == nullptr) {
                return;
              }
              window->shouldDrag = String(matchString) == "movable";
              g_free(matchString);
              return;
            },
            new WeakWindowCallbackData { w->getWeakSelf() }
          );
        }

        return FALSE;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "focus",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GtkDirectionType direction,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-data-get",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GdkDragContext* context,
        GtkSelectionData* data,
        guint info,
        guint time,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;

        if (w->isDragInvokedInsideWindow) {
          // FIXME: Once, write a single tmp file `/tmp/{i64}.download` and
          // add it to the draggablePayload, then start the fanotify watcher
          // for that particular file.
          return;
        }

        if (w->draggablePayload.size() == 0) return;

        gchar* uris[w->draggablePayload.size() + 1];
        int i = 0;

        for (auto& file : w->draggablePayload) {
          if (file[0] == '/') {
            // file system paths must be proper URIs
            file = String("file://" + file);
          }
          uris[i++] = strdup(file.c_str());
        }

        uris[i] = NULL;

        gtk_selection_data_set_uris(data, uris);
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "motion-notify-event",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GdkEventMotion* event,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return FALSE;

        if (w->shouldDrag && event->state & GDK_BUTTON1_MASK) {
          auto win = GDK_WINDOW(gtk_widget_get_window(w->window));
          gint x;
          gint y;

          GdkRectangle frame_extents;
          gdk_window_get_frame_extents(win, &frame_extents);

          GtkAllocation allocation;
          gtk_widget_get_allocation(wv, &allocation);

          gint menubarHeight = 0;

          if (w->menubar) {
            GtkAllocation allocationMenubar;
            gtk_widget_get_allocation(w->menubar, &allocationMenubar);
            menubarHeight = allocationMenubar.height;
          }

          int offsetWidth = (frame_extents.width - allocation.width) / 2;
          int offsetHeight = (frame_extents.height - allocation.height) - offsetWidth - menubarHeight;

          gdk_window_get_position(win, &x, &y);

          gint offset_x = event->x_root - w->dragging.x;
          gint offset_y = event->y_root - w->dragging.y;

          gint newX = x + offset_x;
          gint newY = y + offset_y;

          gdk_window_move(win, newX - offsetWidth, newY - offsetHeight);

          w->dragging.x = event->x_root;
          w->dragging.y = event->y_root;
        }

        return FALSE;

        //
        // TODO: refactor the native drag/drop plumbing to match the WebView path.
        //
        // char* target_uri = g_file_get_uri(drag_info->target_location);

        int count = w->draggablePayload.size();
        bool inbound = !w->isDragInvokedInsideWindow;

        // w->eval(getEmitToRenderProcessJavaScript("dragend", "{}"));

        // TODO: investigate incorrect focus behavior (toast shown instead)
        gtk_window_present(GTK_WINDOW(w->window));
        // gdk_window_focus(GDK_WINDOW(w->window), nullptr);

        String json = (
          "{\"count\":" + std::to_string(count) + ","
          "\"inbound\":" + (inbound ? "true" : "false") + ","
          "\"x\":" + std::to_string(x) + ","
          "\"y\":" + std::to_string(y) + "}"
        );

        w->eval(getEmitToRenderProcessJavaScript("drag", json));
      }),
      this
    );

    // https://wiki.gnome.org/Newcomers/XdsTutorial
    // https://wiki.gnome.org/action/show/Newcomers/OldDragNDropTutorial?action=show&redirect=Newcomers%2FDragNDropTutorial

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-end",
      G_CALLBACK(+[](GtkWidget* wv, GdkDragContext* context, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;

        // w->isDragInvokedInsideWindow = false;
        // w->draggablePayload.clear();
        w->eval(getEmitToRenderProcessJavaScript("dragend", "{}"));
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-data-received",
      G_CALLBACK(+[](
        GtkWidget* wv,
        GdkDragContext* context,
        gint x,
        gint y,
        GtkSelectionData* data,
        guint info,
        guint time,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        if (!w) return;

        gtk_drag_dest_add_uri_targets(wv);
        gchar** uris = gtk_selection_data_get_uris(data);
        int len = gtk_selection_data_get_length(data) - 1;
        if (!uris) return;

        auto v = &w->draggablePayload;

        for(size_t n = 0; uris[n] != nullptr; n++) {
          gchar* src = g_filename_from_uri(uris[n], nullptr, nullptr);
          if (src) {
            auto s = String(src);
            if (std::find(v->begin(), v->end(), s) == v->end()) {
              v->push_back(s);
            }
            g_free(src);
          }
        }
      }),
      this
    );

    g_signal_connect(
      G_OBJECT(this->webview),
      "drag-drop",
      G_CALLBACK(+[](
        GtkWidget* widget,
        GdkDragContext* context,
        gint x,
        gint y,
        guint time,
        gpointer arg)
      {
        auto* w = static_cast<Window*>(arg);
        auto count = w->draggablePayload.size();
        JSON::Array files;

        for (int i = 0 ; i < count; ++i) {
          files[i] = w->draggablePayload[i];
        }

        JSON::Object json;
        json["files"] = files;
        json["x"] = x;
        json["y"] = y;

        JSON::Object options;
        options["bubbles"] = true;

        w->eval(getEmitToRenderProcessJavaScript(
          "dropin",
          json.str(),
          "globalThis",
          options
        ));

        w->draggablePayload.clear();
        w->eval(getEmitToRenderProcessJavaScript("dragend", "{}"));
        gtk_drag_finish(context, TRUE, TRUE, time);
        return TRUE;
      }),
      this
    );
    */

    g_signal_connect(
      G_OBJECT(this->window),
      "destroy",
      G_CALLBACK((+[](GtkWidget* object, gpointer arg) {
        auto app = App::sharedApplication();
        const auto shouldExit = app == nullptr
          ? true
          : app->shouldExit.load(std::memory_order_relaxed);

        auto clearWindowState = [] (Window* window) {
          if (window == nullptr) {
            return;
          }

          if (window->bridge != nullptr) {
            window->bridge->evaluateJavaScriptHandler = nullptr;
            window->bridge->navigateHandler = nullptr;
            {
              Lock lock(window->bridge->router.mutex);
              window->bridge->router.listeners.clear();
              window->bridge->router.table.clear();
            }
          }

          window->webview = nullptr;
          window->vbox = nullptr;
          window->window = nullptr;
        };

        auto w = reinterpret_cast<Window*>(arg);
        int index = w != nullptr ? w->index : -1;

        if (w != nullptr && w->window == object) {
          clearWindowState(w);
        }

        // If the application is exiting, do not emit events or schedule
        // windowManager.destroyWindow(), but still clear any per-window state
        // so the C++ destructor does not touch freed GTK objects.
        if (app == nullptr) {
          return;
        }

        for (auto& window : app->runtime.windowManager.windows) {
          if (window == nullptr) {
            continue;
          }

          if (window->window == object) {
            index = window->index;
            clearWindowState(window.get());
            break;
          }
        }

        if (shouldExit || index < 0) {
          return;
        }

        for (auto window : app->runtime.windowManager.windows) {
          if (window == nullptr || window->index == index) {
            continue;
          }

          JSON::Object json = JSON::Object::Entries {
            {"data", index}
          };

          window->eval(getEmitToRenderProcessJavaScript("windowclosed", json.str()));
        }

        // Defer native destruction until after GTK has completed the destroy
        // signal emission. Destroying the C++ Window synchronously here can
        // leave other GTK signal handlers with a dangling userData pointer.
        auto* indexPtr = new int(index);
        g_idle_add_full(
          G_PRIORITY_DEFAULT_IDLE,
          +[](gpointer userData) -> gboolean {
            const auto index = *reinterpret_cast<int*>(userData);
            auto app = App::sharedApplication();
            if (app != nullptr && !app->shouldExit.load(std::memory_order_relaxed)) {
              app->runtime.windowManager.destroyWindow(index);
            }
            return G_SOURCE_REMOVE;
          },
          indexPtr,
          +[](gpointer userData) {
            delete reinterpret_cast<int*>(userData);
          }
        );
      })),
      this
    );

    g_signal_connect(
      G_OBJECT(this->window),
      "size-allocate", // https://docs.gtk.org/gtk3/method.Window.get_size.html
      G_CALLBACK(+[](GtkWidget* widget,GtkAllocation* allocation, gpointer arg) {
        auto* w = static_cast<Window*>(arg);
        gtk_window_get_size(GTK_WINDOW(widget), &w->size.width, &w->size.height);
      }),
      this
    );

    if (this->options.aspectRatio.size() > 0) {
      g_signal_connect(
        window,
        "configure-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventConfigure* event, gpointer ptr) {
          auto w = static_cast<Window*>(ptr);
          if (!w) return FALSE;

          // TODO: cache the parsed aspectRatio values so they aren't recalculated on each event
          auto parts = split(w->options.aspectRatio, ':');
          float aspectWidth = 0;
          float aspectHeight = 0;

          try {
            aspectWidth = std::stof(trim(parts[0]));
            aspectHeight = std::stof(trim(parts[1]));
          } catch (...) {
            return FALSE;
          }

          if (aspectWidth > 0 && aspectHeight > 0) {
            GdkGeometry geom;
            geom.min_aspect = aspectWidth / aspectHeight;
            geom.max_aspect = geom.min_aspect;
            gtk_window_set_geometry_hints(GTK_WINDOW(widget), widget, &geom, GdkWindowHints(GDK_HINT_ASPECT));
          }

          return FALSE;
        }),
        this
      );

      // gtk_window_set_aspect_ratio(GTK_WINDOW(window), aspectRatio, TRUE);
    }

    this->prewarmWebviewTlsPins();
  }

  Window::~Window () {
    auto app = App::sharedApplication();

    if (this->bridge != nullptr) {
      this->bridge->evaluateJavaScriptHandler = nullptr;
      this->bridge->navigateHandler = nullptr;
      {
        Lock lock(this->bridge->router.mutex);
        this->bridge->router.listeners.clear();
        this->bridge->router.table.clear();
      }
    }

    if (!app) {
      return;
    }

    if (this->policies) {
      g_object_unref(this->policies);
      this->policies = nullptr;
    }

    if (this->settings) {
      g_object_unref(this->settings);
      this->settings = nullptr;
    }

    if (this->userContentManager) {
      g_object_unref(this->userContentManager);
      this->userContentManager = nullptr;
    }

    if (this->accelGroup) {
      g_object_unref(this->accelGroup);
      this->accelGroup = nullptr;
    }

    if (this->webview) {
      gtk_widget_set_can_focus(GTK_WIDGET(this->webview), false);
      this->webview = nullptr;
    }

    if (this->vbox) {
      if (this->window) {
        gtk_container_remove(GTK_CONTAINER(this->window), this->vbox);
      }
      this->vbox = nullptr;
    }

    if (this->window) {
      auto window = this->window;
      this->window = nullptr;
      g_object_unref(window);
    }
  }

  ScreenSize Window::getScreenSize () {
    auto app = App::sharedApplication();
    int width = 0;
    int height = 0;

    // Prefer an existing toplevel window's monitor geometry
    if (app) {
      auto list = gtk_window_list_toplevels();
      auto defaultWindow = app->runtime.windowManager.getWindow(0);
      if (list != nullptr) {
        for (auto entry = list; entry != nullptr; entry = entry->next) {
          auto widget = (GtkWidget*) entry->data;
          auto window = GTK_WINDOW(widget);

          if (
            (defaultWindow == nullptr && window != nullptr) ||
            (defaultWindow != nullptr && GTK_WINDOW(defaultWindow->window) == window)
          ) {
            auto geometry = GdkRectangle {};
            auto display = gtk_widget_get_display(widget);
            auto monitor = gdk_display_get_monitor_at_window(
              display,
              gtk_widget_get_window(widget)
            );

            if (monitor != nullptr) {
              gdk_monitor_get_geometry(monitor, &geometry);
              if (geometry.width > 0) {
                width = geometry.width;
              }
              if (geometry.height > 0) {
                height = geometry.height;
              }
            }

            break;
          }
        }

        g_list_free(list);
      }
    }

    // Fallback: query the primary monitor directly
    if (!height || !width) {
      auto geometry = GdkRectangle {};
      auto display = gdk_display_get_default();

      if (display != nullptr) {
        auto monitor = gdk_display_get_primary_monitor(display);
        if (monitor == nullptr && gdk_display_get_n_monitors(display) > 0) {
          monitor = gdk_display_get_monitor(display, 0);
        }

        if (monitor != nullptr) {
          gdk_monitor_get_geometry(monitor, &geometry);
          if (geometry.width > 0) {
            width = geometry.width;
          }
          if (geometry.height > 0) {
            height = geometry.height;
          }
        }
      }
    }

    if (!height) {
      height = (int) DEFAULT_MONITOR_HEIGHT;
    }

    if (!width) {
      width = (int) DEFAULT_MONITOR_WIDTH;
    }

    return ScreenSize { width, height };
  }

  void Window::eval (const String& input, const EvalCallback callback) {
    const auto source = input;
    const auto weakSelf = this->getWeakSelf();
    const auto bridge = this->bridge;

    if (bridge == nullptr) {
      if (callback != nullptr) {
        callback(JSON::Error("Window bridge is not available"));
      }
      return;
    }

    bridge->dispatch([=] () {
      const auto self = weakSelf.lock();
      if (self == nullptr || self->webview == nullptr) {
        if (callback != nullptr) {
          callback(JSON::Error("Window is closed"));
        }
        return;
      }

        webkit_web_view_evaluate_javascript(
          self->webview,
          source.c_str(),
          source.size(),
          nullptr, // world name
          nullptr, // source URI
          nullptr, // cancellable
          +[]( // callback
            GObject* object,
            GAsyncResult* result,
            gpointer userData
          ) {
            const auto callback = reinterpret_cast<const EvalCallback*>(userData);
            if (callback == nullptr || *callback == nullptr) {
              auto value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, nullptr);
              return;
            }

            GError* error = nullptr;
            auto value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);

            if (!value) {
              if (error != nullptr) {
                (*callback)(JSON::Error(error->message));
                g_error_free(error);
              } else {
                (*callback)(JSON::Error("An unknown error occurred"));
              }
            } else if (jsc_value_is_string(value)) {
              const auto context = jsc_value_get_context(value);
              const auto exception = jsc_context_get_exception(context);
              const auto stringValue = jsc_value_to_string(value);

              if (exception) {
                const auto message = jsc_exception_get_message(exception);
                (*callback)(JSON::Error(message));
              } else {
                (*callback)(JSON::String(stringValue));
              }

              g_free(stringValue);
            } else if (jsc_value_is_boolean(value)) {
              const auto context = jsc_value_get_context(value);
              const auto exception = jsc_context_get_exception(context);
              const auto booleanValue = jsc_value_to_boolean(value);

              if (exception) {
                const auto message = jsc_exception_get_message(exception);
                (*callback)(JSON::Error(message));
              } else {
                (*callback)(JSON::Boolean(booleanValue));
              }
            } else if (jsc_value_is_null(value)) {
              const auto context = jsc_value_get_context(value);
              const auto exception = jsc_context_get_exception(context);

              if (exception) {
                const auto message = jsc_exception_get_message(exception);
                (*callback)(JSON::Error(message));
              } else {
                (*callback)(JSON::Null());
              }
            } else if (jsc_value_is_number(value)) {
              const auto context = jsc_value_get_context(value);
              const auto exception = jsc_context_get_exception(context);
              const auto numberValue = jsc_value_to_double(value);

              if (exception) {
                const auto message = jsc_exception_get_message(exception);
                (*callback)(JSON::Error(message));
              } else {
                (*callback)(JSON::Number(numberValue));
              }
            } else if (jsc_value_is_undefined(value)) {
              const auto context = jsc_value_get_context(value);
              const auto exception = jsc_context_get_exception(context);

              if (exception) {
                const auto message = jsc_exception_get_message(exception);
                (*callback)(JSON::Error(message));
              } else {
                (*callback)(nullptr);
              }
            } else if (jsc_value_is_array(value) || jsc_value_is_object(value)) {
              const auto context = jsc_value_get_context(value);
              const auto exception = jsc_context_get_exception(context);

              if (exception) {
                const auto message = jsc_exception_get_message(exception);
                (*callback)(JSON::Error(message));
              } else if (jsc_value_is_array(value)) {
                (*callback)(JSON::Array(value));
              } else {
                (*callback)(JSON::Object(value));
              }
            }

            if (value) {
              //webkit_javascript_result_unref(reinterpret_cast<WebKitJavascriptResult*>(value));
            }

            delete callback;
          },
          callback == nullptr
            ? nullptr
            : new EvalCallback(callback)
        );
    });
  }

  void Window::show () {
    const auto weakSelf = this->getWeakSelf();
    const auto bridge = this->bridge;

    if (bridge == nullptr) {
      return;
    }

    bridge->dispatch([=] () {
      const auto self = weakSelf.lock();
      if (self == nullptr || self->window == nullptr) {
        return;
      }

      gtk_widget_realize(self->window);

      self->index = self->options.index;
      if (self->options.headless == false) {
        gtk_widget_show_all(self->window);
        gtk_window_present(GTK_WINDOW(self->window));
          // Mirror DOM focus for desktop parity
        self->eval("window.focus()");
      }
    });
  }

  void Window::hide () {
    gtk_widget_realize(this->window);
    gtk_widget_hide(this->window);
    // Mirror DOM blur for desktop parity
    this->eval("window.blur()");
    JSON::Object json = JSON::Object::Entries {
      {"data", this->index}
    };
    this->eval(getEmitToRenderProcessJavaScript("windowhidden", json.str()));
  }

  void Window::setBackgroundColor (const String& rgba) {
    const auto parts = split(trim(replace(replace(rgba, "rgba(", ""), ")", "")), ',');
    int r = 0, g = 0, b = 0;
    float a = 1.0;

    if (parts.size() == 4) {
      try { r = std::stoi(trim(parts[0])); } catch (...) {}

      try { g = std::stoi(trim(parts[1])); } catch (...) {}

      try { b = std::stoi(trim(parts[2])); } catch (...) {}

      try { a = std::stof(trim(parts[3])); } catch (...) {}

      return this->setBackgroundColor(r, g, b, a);
    }
  }

  void Window::setBackgroundColor (int r, int g, int b, float a) {
    GdkRGBA color;
    color.red = r / 255.0;
    color.green = g / 255.0;
    color.blue = b / 255.0;
    color.alpha = a;

    if (this->window) {
      gtk_widget_realize(this->window);
      setWidgetBackgroundColor(this->window, color);
      this->backgroundColor = Color(r, g, b, a);
    }
  }

  String Window::getBackgroundColor () {
    GtkStyleContext* context = gtk_widget_get_style_context(this->window);

    GdkRGBA color;
    gtk_style_context_get(
      context,
      gtk_widget_get_state_flags(this->window),
      "background-color",
      &color,
      nullptr
    );

    char string[100];

    snprintf(
      string,
      sizeof(string),
      "rgba(%d, %d, %d, %f)",
      (int) (255 * color.red),
      (int) (255 * color.green),
      (int) (255 * color.blue),
      color.alpha
    );

    return string;
  }

  void Window::showInspector () {
    if (this->webview) {
      const auto inspector = webkit_web_view_get_inspector(this->webview);
      if (inspector) {
        webkit_web_inspector_show(inspector);
      }
    }
  }

  namespace {
    struct PrewarmWebviewTlsPinEndpoint {
      String endpointKey;
      String connectHost;
      int connectPort = 0;
      String allowHost;
    };

    bool parsePrewarmEndpoint (const String& endpointKey, String& outHost, int& outPort) {
      outHost = "";
      outPort = 0;

      const auto endpoint = trim(endpointKey);
      if (endpoint.empty()) {
        return false;
      }

      if (endpoint.front() == '[') {
        const auto close = endpoint.find(']');
        if (close == String::npos || close <= 1) {
          return false;
        }

        if (close + 1 >= endpoint.size() || endpoint[close + 1] != ':') {
          return false;
        }

        const auto portString = close + 2 < endpoint.size()
          ? endpoint.substr(close + 2)
          : "";

        if (portString.empty()) {
          return false;
        }

        for (const auto ch : portString) {
          if (ch < '0' || ch > '9') {
            return false;
          }
        }

        try {
          outPort = std::stoi(portString);
        } catch (...) {
          return false;
        }

        if (outPort <= 0 || outPort > 65535) {
          return false;
        }

        outHost = endpoint.substr(1, close - 1);
        return !outHost.empty();
      }

      const auto colon = endpoint.rfind(':');
      if (colon == String::npos || colon == 0) {
        return false;
      }

      // Reject unbracketed IPv6 literals (they contain multiple colons).
      if (endpoint.find(':') != colon) {
        return false;
      }

      const auto hostPart = endpoint.substr(0, colon);
      const auto portString = colon + 1 < endpoint.size()
        ? endpoint.substr(colon + 1)
        : "";

      if (hostPart.empty() || portString.empty()) {
        return false;
      }

      for (const auto ch : portString) {
        if (ch < '0' || ch > '9') {
          return false;
        }
      }

      try {
        outPort = std::stoi(portString);
      } catch (...) {
        return false;
      }

      if (outPort <= 0 || outPort > 65535) {
        return false;
      }

      outHost = hostPart;
      return true;
    }

    struct PrewarmWebviewTlsPinsJob {
      WebKitWebContext* webContext = nullptr;
      TlsPinMap pins;
      Vector<PrewarmWebviewTlsPinEndpoint> endpoints;
    };

    struct PrewarmWebviewTlsAllowData {
      WebKitWebContext* webContext = nullptr;
      GTlsCertificate* certificate = nullptr;
      String host;
    };

    gboolean prewarmWebviewTlsAllowCertificateOnMainThread (gpointer userData) {
      auto* data = static_cast<PrewarmWebviewTlsAllowData*>(userData);
      if (data == nullptr) {
        return G_SOURCE_REMOVE;
      }

      if (data->webContext && data->certificate && !data->host.empty()) {
        webkit_web_context_allow_tls_certificate_for_host(
          data->webContext,
          data->certificate,
          data->host.c_str()
        );
      }

      if (data->certificate) {
        g_object_unref(data->certificate);
      }

      if (data->webContext) {
        g_object_unref(data->webContext);
      }

      delete data;
      return G_SOURCE_REMOVE;
    }

    struct PrewarmWebviewTlsAcceptData {
      const TlsPinMap* pins = nullptr;
      String endpointKey;
      GTlsCertificate* certificate = nullptr;
    };

    gboolean prewarmWebviewTlsAcceptCertificate (
      GTlsConnection* /*connection*/,
      GTlsCertificate* certificate,
      GTlsCertificateFlags /*errors*/,
      gpointer userData
    ) {
      auto* data = static_cast<PrewarmWebviewTlsAcceptData*>(userData);

      if (data == nullptr || data->pins == nullptr || certificate == nullptr) {
        return FALSE;
      }

      GByteArray* der = nullptr;
      g_object_get(G_OBJECT(certificate), "certificate", &der, nullptr);

      if (der == nullptr || der->data == nullptr || der->len == 0) {
        if (der != nullptr) {
          g_byte_array_unref(der);
        }
        return FALSE;
      }

      Vector<uint8_t> digest(32);
      gsize digestSize = digest.size();

      auto checksum = g_checksum_new(G_CHECKSUM_SHA256);
      if (checksum == nullptr) {
        g_byte_array_unref(der);
        return FALSE;
      }

      g_checksum_update(checksum, der->data, der->len);
      g_checksum_get_digest(checksum, digest.data(), &digestSize);
      g_checksum_free(checksum);
      g_byte_array_unref(der);

      if (digestSize != digest.size()) {
        return FALSE;
      }

      if (!isCertificateAllowedForHost(*data->pins, data->endpointKey, digest)) {
        return FALSE;
      }

      if (data->certificate != nullptr) {
        g_object_unref(data->certificate);
        data->certificate = nullptr;
      }

      data->certificate = static_cast<GTlsCertificate*>(g_object_ref(certificate));
      return TRUE;
    }

    gpointer prewarmWebviewTlsPinsThread (gpointer userData) {
      auto* job = static_cast<PrewarmWebviewTlsPinsJob*>(userData);
      if (job == nullptr || job->webContext == nullptr) {
        return nullptr;
      }

      for (const auto& endpoint : job->endpoints) {
        if (endpoint.connectHost.empty() || endpoint.connectPort <= 0 || endpoint.allowHost.empty()) {
          continue;
        }

        GError* error = nullptr;
        auto socketClient = g_socket_client_new();

        if (socketClient == nullptr) {
          continue;
        }

        // Avoid stalling pin updates on unreachable endpoints.
        g_socket_client_set_timeout(socketClient, 5);

        auto socketConnection = g_socket_client_connect_to_host(
          socketClient,
          endpoint.connectHost.c_str(),
          endpoint.connectPort,
          nullptr,
          &error
        );

        if (socketConnection == nullptr) {
          if (error != nullptr) {
            g_error_free(error);
          }
          g_object_unref(socketClient);
          continue;
        }

        auto identity = g_network_address_new(endpoint.connectHost.c_str(), endpoint.connectPort);
        if (identity == nullptr) {
          g_object_unref(socketConnection);
          g_object_unref(socketClient);
          continue;
        }

        auto tlsConnection = g_tls_client_connection_new(
          G_IO_STREAM(socketConnection),
          G_SOCKET_CONNECTABLE(identity),
          &error
        );
        g_object_unref(identity);

        if (tlsConnection == nullptr) {
          if (error != nullptr) {
            g_error_free(error);
          }
          g_object_unref(socketConnection);
          g_object_unref(socketClient);
          continue;
        }

        PrewarmWebviewTlsAcceptData acceptData = {
          .pins = &job->pins,
          .endpointKey = endpoint.endpointKey,
          .certificate = nullptr
        };

        g_signal_connect(
          G_OBJECT(tlsConnection),
          "accept-certificate",
          G_CALLBACK(prewarmWebviewTlsAcceptCertificate),
          &acceptData
        );

        const auto ok = g_tls_connection_handshake(G_TLS_CONNECTION(tlsConnection), nullptr, &error);
        if (!ok) {
          if (error != nullptr) {
            g_error_free(error);
          }
          if (acceptData.certificate != nullptr) {
            g_object_unref(acceptData.certificate);
          }
          g_object_unref(tlsConnection);
          g_object_unref(socketConnection);
          g_object_unref(socketClient);
          continue;
        }

        if (acceptData.certificate != nullptr) {
          auto* allowData = new PrewarmWebviewTlsAllowData {
            .webContext = static_cast<WebKitWebContext*>(g_object_ref(job->webContext)),
            .certificate = acceptData.certificate,
            .host = endpoint.allowHost
          };

          // Allowing the certificate must run on the GTK/WebKit main thread.
          g_main_context_invoke(nullptr, prewarmWebviewTlsAllowCertificateOnMainThread, allowData);

          // Ownership transferred to allowData.
          acceptData.certificate = nullptr;
        }

        g_io_stream_close(G_IO_STREAM(tlsConnection), nullptr, nullptr);
        g_object_unref(tlsConnection);
        g_object_unref(socketConnection);
        g_object_unref(socketClient);
      }

      g_object_unref(job->webContext);
      delete job;
      return nullptr;
    }
  }

  void Window::prewarmWebviewTlsPins () {
    if (this->bridge == nullptr) {
      return;
    }

    auto& userConfig = this->bridge->userConfig;
    auto it = userConfig.find("webview_tls_pins");
    if (it == userConfig.end() || trim(it->second).empty()) {
      it = userConfig.find("webview.tls_pins");
      if (it == userConfig.end() || trim(it->second).empty()) {
        return;
      }
    }

    auto webContext = this->bridge->webContext;
    if (!webContext && this->webview) {
      webContext = webkit_web_view_get_context(this->webview);
    }

    if (!webContext) {
      return;
    }

    auto pins = parseTlsPinConfig(it->second);
    if (pins.empty()) {
      return;
    }

    Vector<PrewarmWebviewTlsPinEndpoint> endpoints;
    for (const auto& entry : pins) {
      if (entry.second.empty()) {
        continue;
      }

      String connectHost;
      int connectPort = 0;
      if (!parsePrewarmEndpoint(entry.first, connectHost, connectPort)) {
        continue;
      }

      const auto allowHost = normaliseTlsPinHost(entry.first);
      if (allowHost.empty()) {
        continue;
      }

      endpoints.push_back(PrewarmWebviewTlsPinEndpoint {
        .endpointKey = entry.first,
        .connectHost = connectHost,
        .connectPort = connectPort,
        .allowHost = allowHost
      });
    }

    if (endpoints.empty()) {
      return;
    }

    auto* job = new PrewarmWebviewTlsPinsJob {
      .webContext = static_cast<WebKitWebContext*>(g_object_ref(webContext)),
      .pins = std::move(pins),
      .endpoints = std::move(endpoints)
    };

    auto thread = g_thread_new("oro-webview-tls-prewarm", prewarmWebviewTlsPinsThread, job);
    g_thread_unref(thread);
  }

  void Window::exit (int code) {
    isExiting = true;
    const auto callback = this->onExit;
    this->onExit = nullptr;
    if (callback != nullptr) {
      callback(code);
    }
  }

  void Window::kill () {}

  void Window::close (int _) {
    if (this->window) {
      gtk_window_close(GTK_WINDOW(this->window));
    }
  }

  void Window::maximize () {
    gtk_window_maximize(GTK_WINDOW(window));
  }

  void Window::minimize () {
    if (this->options.headless) {
      this->eval("window.dispatchEvent(new Event('blur'))");
    } else {
      gtk_window_iconify(GTK_WINDOW(window));
      this->evalDomBlurThrottled();
    }
  }

  void Window::restore () {
    if (this->options.headless) {
      this->eval("window.dispatchEvent(new Event('focus'))");
    } else {
      gtk_window_deiconify(GTK_WINDOW(window));
      this->evalDomFocusThrottled();
    }
  }

  void Window::navigate (const String& url) {
    const auto weakSelf = this->getWeakSelf();
    const auto bridge = this->bridge;

    if (bridge == nullptr) {
      return;
    }

    bridge->dispatch([=] () {
      const auto self = weakSelf.lock();
      if (self == nullptr || self->webview == nullptr) {
        return;
      }

      webkit_web_view_load_uri(self->webview, url.c_str());
    });
  }

  const String Window::getTitle () const {
    if (this->window != nullptr) {
      const auto title = gtk_window_get_title(GTK_WINDOW(this->window));
      if (title != nullptr) {
        return title;
      }
    }

    return "";
  }

  void Window::setTitle (const String& s) {
    const auto title = s;
    if (this->options.headless != true) {
      gtk_widget_realize(this->window);
    }
    gtk_window_set_title(GTK_WINDOW(this->window), title.c_str());
  }

  void Window::about () {
    GtkWidget* dialog = gtk_dialog_new();
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 200);

    GtkWidget* body = gtk_dialog_get_content_area(GTK_DIALOG(GTK_WINDOW(dialog)));
    GtkContainer* content = GTK_CONTAINER(body);

    // TODO(@jwerle): figure out dev path
    String imgPath = "/usr/share/icons/hicolor/256x256/apps/" +
      this->bridge->userConfig["build_name"] +
      ".png";

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
      imgPath.c_str(),
      60,
      60,
      true,
      nullptr
    );

    GtkWidget* img = gtk_image_new_from_pixbuf(pixbuf);
    gtk_widget_set_margin_top(img, 20);
    gtk_widget_set_margin_bottom(img, 20);

    gtk_box_pack_start(GTK_BOX(content), img, false, false, 0);

    String title_value(this->bridge->userConfig["build_name"] + " v" + this->bridge->userConfig["meta_version"]);
    String version_value("Built with oroc v" + version::VERSION_FULL_STRING);

    GtkWidget* label_title = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_title), title_value.c_str());
    gtk_container_add(content, label_title);

    GtkWidget* label_op_version = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_op_version), version_value.c_str());
    gtk_container_add(content, label_op_version);

    GtkWidget* label_copyright = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label_copyright), this->bridge->userConfig["meta_copyright"].c_str());
    gtk_container_add(content, label_copyright);

    g_signal_connect(
      dialog,
      "response",
      G_CALLBACK(gtk_widget_destroy),
      nullptr
    );

    gtk_widget_show_all(body);
    gtk_widget_show_all(dialog);
    gtk_window_set_title(GTK_WINDOW(dialog), "About");

    gtk_dialog_run(GTK_DIALOG(dialog));
  }

  Window::Size Window::getSize () {
    gtk_widget_get_size_request(
      this->window,
      &this->size.width,
      &this->size.height
    );

    return this->size;
  }

  const Window::Size Window::getSize () const {
    return this->size;
  }

  void Window::setSize (int width, int height, int hints) {
    gtk_widget_realize(window);
    gtk_window_set_resizable(GTK_WINDOW(window), hints != WINDOW_HINT_FIXED);

    if (hints == WINDOW_HINT_NONE) {
      gtk_window_resize(GTK_WINDOW(window), width, height);
    } else if (hints == WINDOW_HINT_FIXED) {
      gtk_widget_set_size_request(window, width, height);
    } else {
      GdkGeometry g;
      g.min_width = g.max_width = width;
      g.min_height = g.max_height = height;

      GdkWindowHints h = (hints == WINDOW_HINT_MIN
        ? GDK_HINT_MIN_SIZE
        : GDK_HINT_MAX_SIZE
      );

      gtk_window_set_geometry_hints(GTK_WINDOW(window), nullptr, &g, h);
    }

    this->size.width = width;
    this->size.height = height;
  }

  void Window::setPosition (float x, float y) {
    gtk_window_move(GTK_WINDOW(this->window), (int) x, (int) y);
    this->position.x = x;
    this->position.y = y;
  }

  void Window::focus () {
    if (this->window) {
      if (this->options.headless == false) {
        gtk_window_present(GTK_WINDOW(this->window));
        this->evalDomFocusThrottled();
      } else {
        this->eval("window.dispatchEvent(new Event('focus'))");
      }
    }
  }

  void Window::blur () {
    if (this->window) {
      if (this->options.headless == false) {
        auto gdk = gtk_widget_get_window(this->window);
        if (gdk) {
          gdk_window_lower(gdk);
        }
        this->evalDomBlurThrottled();
      } else {
        this->eval("window.dispatchEvent(new Event('blur'))");
      }
    }
  }

  void Window::setAlwaysOnTop (bool enabled) {
    if (this->window) {
      g_object_set_data(
        G_OBJECT(this->window),
        "oro-always-on-top",
        GINT_TO_POINTER(enabled ? 1 : 0)
      );
      if (!this->options.headless) {
        gtk_window_set_keep_above(GTK_WINDOW(this->window), enabled);
      }
    }
  }

  bool Window::isAlwaysOnTop () {
    if (this->window) {
      return GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(this->window), "oro-always-on-top")
      ) != 0;
    }
    return false;
  }

  void Window::setTrayMenu (const String& value) {
    this->setMenu(value, true);
  }

  void Window::setSystemMenu (const String& value) {
    this->setMenu(value, false);
  }

  void Window::setMenu (const String& menuSource, const bool& isTrayMenu) {
    if (menuSource.empty()) {
      return;
    }

    auto clear = [this](GtkWidget* menu) {
      GList* iter;
      GList* children = gtk_container_get_children(GTK_CONTAINER(menu));

      for (iter = children; iter != nullptr; iter = g_list_next(iter)) {
        if (iter && iter->data) {
          gtk_widget_destroy(GTK_WIDGET(iter->data));
        }
      }

      g_list_free(children);
      return menu;
    };

    if (isTrayMenu) {
      menutray = menutray == nullptr ? gtk_menu_new() : clear(menutray);
    } else {
      menubar = menubar == nullptr ? gtk_menu_bar_new() : clear(menubar);
    }

    GtkStyleContext* context = gtk_widget_get_style_context(this->window);

    GdkRGBA color = {0.0, 0.0, 0.0, 0.0};
    webkit_web_view_set_background_color(WEBKIT_WEB_VIEW(this->webview), &color);
    setWidgetBackgroundColor(menubar, color);

    auto menus = split(menuSource, ';');

    for (auto m : menus) {
      auto menuSource = split(m, '\n');
      if (menuSource.size() == 0) continue;
      auto line = trim(menuSource[0]);
      if (line.empty()) continue;
      auto menuParts = split(line, ':');
      auto menuTitle = menuParts[0];
      // if this is a tray menu, append directly to the tray instead of a submenu.
      auto* ctx = isTrayMenu ? menutray : gtk_menu_new();
      GtkWidget* menuItem = gtk_menu_item_new_with_label(menuTitle.c_str());
      setWidgetBackgroundColor(menuItem, color);

      if (isTrayMenu && menuSource.size() == 1) {
        if (menuParts.size() > 1) {
          gtk_widget_set_name(menuItem, trim(menuParts[1]).c_str());
        }

        g_signal_connect_data(
          G_OBJECT(menuItem),
          "activate",
          G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
            const auto* data = static_cast<WeakWindowCallbackData*>(arg);
            if (data == nullptr) {
              return;
            }

            const auto window = data->weak.lock();
            if (window == nullptr) {
              return;
            }

            auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
            auto parent = gtk_widget_get_name(t);
            window->eval(getResolveMenuSelectionJavaScript("0", title, parent, "tray"));
          }),
          new WeakWindowCallbackData { this->getWeakSelf() },
          +[](gpointer userData, GClosure* /*closure*/) {
            delete static_cast<WeakWindowCallbackData*>(userData);
          },
          GConnectFlags(0)
        );
      }

      for (int i = 1; i < menuSource.size(); i++) {
        auto line = trim(menuSource[i]);
        if (line.empty()) continue;
        auto parts = split(line, ':');
        auto title = parts[0];
        String key = "";

        GtkWidget* item;

        if (parts[0].find("---") != -1) {
          item = gtk_separator_menu_item_new();
        } else {
          item = gtk_menu_item_new_with_label(title.c_str());

          if (parts.size() > 1) {
            auto value = trim(parts[1]);
            key = value == "_" ? "" : value;

            if (key.size() > 0) {
              auto accelerator = split(parts[1], '+');
              if (accelerator.size() <= 1) {
                continue;
              }

              auto modifier = toLowerCase(trim(accelerator[1]));
              key = trim(parts[1]) == "_" ? "" : trim(accelerator[0]);

              GdkModifierType mask = (GdkModifierType)(0);
              bool isShift = String("ABCDEFGHIJKLMNOPQRSTUVWXYZ").find(key) != -1;

              if (accelerator.size() > 1) {
                if (modifier.find("meta") != -1 || modifier.find("super") != -1) {
                  mask = (GdkModifierType)(mask | GDK_META_MASK);
                }

                if (modifier.find("commandorcontrol") != -1) {
                  mask = (GdkModifierType)(mask | GDK_CONTROL_MASK);
                } else if (modifier.find("control") != -1) {
                  mask = (GdkModifierType)(mask | GDK_CONTROL_MASK);
                }

                if (modifier.find("alt") != -1) {
                  mask = (GdkModifierType)(mask | GDK_MOD1_MASK);
                }
              }

              if (isShift || modifier.find("shift") != -1) {
                mask = (GdkModifierType)(mask | GDK_SHIFT_MASK);
              }

              gtk_widget_add_accelerator(
                item,
                "activate",
                accelGroup,
                (guint) key[0],
                mask,
                GTK_ACCEL_VISIBLE
              );

              gtk_widget_show(item);
            }
          }

          if (isTrayMenu) {
            g_signal_connect_data(
              G_OBJECT(item),
              "activate",
              G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
                const auto* data = static_cast<WeakWindowCallbackData*>(arg);
                if (data == nullptr) {
                  return;
                }

                const auto window = data->weak.lock();
                if (window == nullptr) {
                  return;
                }

                auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
                auto parent = gtk_widget_get_name(t);

                window->eval(getResolveMenuSelectionJavaScript("0", title, parent, "tray"));
              }),
              new WeakWindowCallbackData { this->getWeakSelf() },
              +[](gpointer userData, GClosure* /*closure*/) {
                delete static_cast<WeakWindowCallbackData*>(userData);
              },
              GConnectFlags(0)
            );
          } else {
            g_signal_connect_data(
              G_OBJECT(item),
              "activate",
              G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
                const auto* data = static_cast<WeakWindowCallbackData*>(arg);
                if (data == nullptr) {
                  return;
                }

                const auto window = data->weak.lock();
                if (window == nullptr) {
                  return;
                }

                auto title = gtk_menu_item_get_label(GTK_MENU_ITEM(t));
                auto parent = gtk_widget_get_name(t);

                if (String(title).find("About") == 0) {
                  return window->about();
                }

                if (String(title).find("Quit") == 0) {
                  return window->exit(0);
                }

                window->eval(getResolveMenuSelectionJavaScript("0", title, parent, "system"));
              }),
              new WeakWindowCallbackData { this->getWeakSelf() },
              +[](gpointer userData, GClosure* /*closure*/) {
                delete static_cast<WeakWindowCallbackData*>(userData);
              },
              GConnectFlags(0)
            );
          }
        }

        gtk_widget_set_name(item, menuTitle.c_str());
        setWidgetBackgroundColor(menuItem, color);
        gtk_menu_shell_append(GTK_MENU_SHELL(ctx), item);
      }

      if (isTrayMenu) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menutray), menuItem);
      } else {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuItem), ctx);
        gtk_menu_shell_append(GTK_MENU_SHELL(menubar), menuItem);
      }
    }

    if (isTrayMenu) {
      auto& userConfig = this->bridge->userConfig;
      auto cwd = getcwd();
      auto trayIconPath = String("application_tray_icon");

      if (fs::exists(fs::path(cwd) / (trayIconPath + ".png"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".png")).string();
      } else if (fs::exists(fs::path(cwd) / (trayIconPath + ".jpg"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".jpg")).string();
      } else if (fs::exists(fs::path(cwd) / (trayIconPath + ".jpeg"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".jpeg")).string();
      } else if (fs::exists(fs::path(cwd) / (trayIconPath + ".ico"))) {
        trayIconPath = (fs::path(cwd) / (trayIconPath + ".ico")).string();
      } else {
        trayIconPath = "";
      }

      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      GtkStatusIcon* trayIcon;
      if (trayIconPath.size() > 0) {
        trayIcon = gtk_status_icon_new_from_file(trayIconPath.c_str());
      } else {
        trayIcon = gtk_status_icon_new_from_icon_name("utilities-terminal");
      }

      if (userConfig.count("tray_tooltip") > 0) {
        gtk_status_icon_set_tooltip_text(trayIcon, userConfig["tray_tooltip"].c_str());
      }

      g_signal_connect_data(
        trayIcon,
        "activate",
        G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
          const auto* data = static_cast<WeakWindowCallbackData*>(arg);
          if (data == nullptr) {
            return;
          }

          const auto window = data->weak.lock();
          if (window == nullptr || window->menutray == nullptr || window->bridge == nullptr) {
            return;
          }

          gtk_menu_popup_at_pointer(GTK_MENU(window->menutray), NULL);
          window->bridge->emit("tray", true);
        }),
        new WeakWindowCallbackData { this->getWeakSelf() },
        +[](gpointer userData, GClosure* /*closure*/) {
          delete static_cast<WeakWindowCallbackData*>(userData);
        },
        GConnectFlags(0)
      );
      G_GNUC_END_IGNORE_DEPRECATIONS
      gtk_widget_show_all(menutray);
    } else {
      if (gtk_widget_get_parent(menubar) == nullptr) {
        gtk_box_pack_start(GTK_BOX(this->vbox), menubar, false, false, 0);
      }
      gtk_widget_show_all(menubar);
    }
  }

  void Window::setSystemMenuItemEnabled (bool enabled, int barPos, int menuPos) {
    // @TODO(): provide impl
  }

  void Window::closeContextMenu () {
    if (this->contextMenuID > 0) {
      const auto seq = std::to_string(this->contextMenuID);
      this->contextMenuID = 0;
      closeContextMenu(seq);
    }
  }

  void Window::closeContextMenu (const String& seq) {
    if (contextMenu != nullptr) {
      auto ptr = contextMenu;
      contextMenu = nullptr;
      closeContextMenu(ptr, seq);
    }
  }

  void Window::closeContextMenu (
    GtkWidget* contextMenu,
    const String& seq
  ) {
    if (contextMenu != nullptr) {
      gtk_menu_popdown((GtkMenu* ) contextMenu);
      gtk_widget_destroy(contextMenu);
      this->eval(getResolveMenuSelectionJavaScript(seq, "", "contextMenu", "context"));
    }
  }

  void Window::setContextMenu (
    const String& seq,
    const String& menuSource
  ) {
    closeContextMenu();
    if (menuSource.empty()) return void(0);

    // members
    this->contextMenu = gtk_menu_new();

    try {
      this->contextMenuID = std::stoi(seq);
    } catch (...) {
      this->contextMenuID = 0;
    }

    auto menuItems = split(menuSource, '\n');

    for (auto itemData : menuItems) {
      if (trim(itemData).size() == 0) {
        continue;
      }

      if (itemData.find("---") != -1) {
        auto* item = gtk_separator_menu_item_new();
        gtk_widget_show(item);
        gtk_menu_shell_append(GTK_MENU_SHELL(this->contextMenu), item);
        continue;
      }

      auto pair = split(itemData, ':');
      auto meta = String(seq + ";" + itemData);
      auto* item = gtk_menu_item_new_with_label(pair[0].c_str());

      g_signal_connect_data(
        G_OBJECT(item),
        "activate",
        G_CALLBACK(+[](GtkWidget* t, gpointer arg) {
          const auto* data = static_cast<WeakWindowCallbackData*>(arg);
          if (data == nullptr) {
            return;
          }

          const auto window = data->weak.lock();
          if (window == nullptr) {
            return;
          }

          auto meta = gtk_widget_get_name(t);
          auto pair = split(meta, ';');
          auto seq = pair[0];
          auto items = split(pair[1], ":");

          if (items.size() != 2) return;
          window->eval(getResolveMenuSelectionJavaScript(seq, trim(items[0]), trim(items[1]), "context"));
        }),
        new WeakWindowCallbackData { this->getWeakSelf() },
        +[](gpointer userData, GClosure* /*closure*/) {
          delete static_cast<WeakWindowCallbackData*>(userData);
        },
        GConnectFlags(0)
      );

      gtk_widget_set_name(item, meta.c_str());
      gtk_widget_show(item);
      gtk_menu_shell_append(GTK_MENU_SHELL(this->contextMenu), item);
    }

    GdkRectangle rect;
    gint x, y;

    auto win = GDK_WINDOW(gtk_widget_get_window(window));
    auto seat = gdk_display_get_default_seat(gdk_display_get_default());
    auto event = gdk_event_new(GDK_BUTTON_PRESS);
    auto mouse_device = gdk_seat_get_pointer(seat);

    gdk_window_get_device_position(win, mouse_device, &x, &y, nullptr);
    gdk_event_set_device(event, mouse_device);

    event->button.send_event = 1;
    event->button.button = GDK_BUTTON_SECONDARY;
    event->button.window = GDK_WINDOW(g_object_ref(win));
    event->button.time = GDK_CURRENT_TIME;

    rect.height = 0;
    rect.width = 0;
    rect.x = x - 1;
    rect.y = y - 1;

    gtk_widget_add_events(contextMenu, GDK_ALL_EVENTS_MASK);
    gtk_widget_set_can_focus(contextMenu, true);
    gtk_widget_show_all(contextMenu);
    gtk_widget_grab_focus(contextMenu);

    gtk_menu_popup_at_rect(
      GTK_MENU(contextMenu),
      win,
      &rect,
      GDK_GRAVITY_SOUTH_WEST,
      GDK_GRAVITY_NORTH_WEST,
      event
    );
  }

  void Window::handleApplicationURL (const String& url) {
    JSON::Object json = JSON::Object::Entries {{
      "url", url
    }};

    auto options = this->options;
    if (this->index == 0 && this->window && this->webview) {
      if (options.userConfig["build_headless"] != "true") {
        gtk_widget_grab_focus(GTK_WIDGET(this->webview));
        gtk_widget_show_all(GTK_WIDGET(this->window));
        gtk_widget_grab_focus(GTK_WIDGET(this->window));
        gtk_window_activate_focus(GTK_WINDOW(this->window));
        gtk_window_present(GTK_WINDOW(this->window));
      }
    }

    this->bridge->emit("applicationurl", json.str());
  }
}
