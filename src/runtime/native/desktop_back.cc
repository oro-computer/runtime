#include "desktop_back.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include "records.hh"
#include "../runtime.hh"
#include "../window.hh"
#include "../bridge.hh"

namespace oro::runtime::native {
  namespace {
    struct Navigation {
      std::string document;
      std::string registration;
      std::string pending;
      guint timeout = 0;
      ~Navigation() {
        if (timeout)
          g_source_remove(timeout);
      }
    };

    Navigation& navigation (window::Window& window) {
      auto value = static_cast<Navigation*>(g_object_get_data(G_OBJECT(window.window), "oro-native-back"));
      if (!value)
        throw StateError("ENOTREADY");
      return *value;
    }

    std::string identifier () {
      auto value = g_uuid_string_random();
      const std::string result(value);
      g_free(value);
      return result;
    }

    std::string field (const JSON::Object& value, const std::string& name) {
      if (!value.has(name) || value.get(name).type != JSON::Type::String)
        throw StateError("EINVAL");
      const auto result = value.get(name).as<JSON::String>().value();
      if (result.empty() || result.size() > 80)
        throw StateError("EINVAL");
      return result;
    }

    void fallback (window::Window& window) {
      if (!gtk_widget_get_visible(window.window) || !gtk_window_is_active(GTK_WINDOW(window.window)))
        return;
      if (webkit_web_view_can_go_back(window.webview))
        webkit_web_view_go_back(window.webview);
      else if (window.index != 0)
        window.close();
      else
        window.minimize();
    }

    void pressed (window::Window& window) {
      const auto surface = gtk_widget_get_window(window.window);
      if (surface && (gdk_window_get_state(surface) & GDK_WINDOW_STATE_FULLSCREEN)) {
        window.eval("if (document.fullscreenElement) document.exitFullscreen().catch(() => {})");
        gtk_window_unfullscreen(GTK_WINDOW(window.window));
        return;
      }
      auto& state = navigation(window);
      if (state.registration.empty() || state.document != window.bridge->nativeDocumentToken) {
        fallback(window);
        return;
      }
      if (!state.pending.empty())
        return;
      state.pending = identifier();
      const auto detail = JSON::Object(
        JSON::Object::Entries{{"registration", state.registration}, {"id", state.pending}, {"deadlineMs", 750}});
      window.bridge->emit("backrequest", detail.str());
      auto weak = new std::weak_ptr<bridge::Bridge>(window.bridge);
      state.timeout = g_timeout_add_full(
        G_PRIORITY_DEFAULT, 750,
        +[] (gpointer pointer) -> gboolean {
          const auto bridge = static_cast<std::weak_ptr<bridge::Bridge>*>(pointer)->lock();
          if (!bridge)
            return G_SOURCE_REMOVE;
          const auto window = bridge->getRuntime()->windowManager.getWindowForBridge(bridge.get());
          if (!window || !window->window)
            return G_SOURCE_REMOVE;
          auto& state = navigation(*window);
          state.timeout = 0;
          if (state.pending.empty() || state.document != bridge->nativeDocumentToken)
            return G_SOURCE_REMOVE;
          state.pending.clear();
          fallback(*window);
          return G_SOURCE_REMOVE;
        },
        weak, +[] (gpointer pointer) { delete static_cast<std::weak_ptr<bridge::Bridge>*>(pointer); });
    }
  }  // namespace

  void DesktopBack::attach(window::Window& window) {
    g_object_set_data_full(
      G_OBJECT(window.window), "oro-native-back", new Navigation(),
      +[] (gpointer value) { delete static_cast<Navigation*>(value); });
    g_signal_connect(window.webview, "key-press-event",
                     G_CALLBACK(+[] (GtkWidget*, GdkEventKey* event, gpointer pointer) -> gboolean {
                       const auto window = static_cast<window::Window*>(pointer);
                       const auto modifiers = event->state & gtk_accelerator_get_default_mod_mask();
                       if ((event->keyval != GDK_KEY_Back || modifiers != 0) &&
                           (event->keyval != GDK_KEY_Left || modifiers != GDK_MOD1_MASK))
                         return FALSE;
                       pressed(*window);
                       return TRUE;
                     }),
                     &window);
  }

  void DesktopBack::reset(window::Window& window) {
    auto& state = navigation(window);
    cancelPending(window);
    state.document.clear();
    state.registration.clear();
  }

  void DesktopBack::cancelPending(window::Window& window) {
    auto& state = navigation(window);
    if (state.timeout)
      g_source_remove(state.timeout);
    state.timeout = 0;
    state.pending.clear();
  }

  JSON::Any DesktopBack::request(window::Window& window, const std::string& action, const JSON::Object& options) {
    auto& state = navigation(window);
    if (action == "enable") {
      if (options.size() != 0)
        throw StateError("EINVAL");
      reset(window);
      state.document = window.bridge->nativeDocumentToken;
      state.registration = identifier();
      return JSON::Object::Entries{{"registration", state.registration}};
    }
    if (action != "disable" && action != "respond")
      throw StateError("EINVAL");
    if (state.document != window.bridge->nativeDocumentToken || field(options, "registration") != state.registration)
      throw StateError("ABORT_ERR");
    if (action == "disable") {
      if (options.size() != 1)
        throw StateError("EINVAL");
      reset(window);
      return nullptr;
    }
    if (options.size() != 3 || !options.has("handled") || options.get("handled").type != JSON::Type::Boolean)
      throw StateError("EINVAL");
    if (field(options, "id") != state.pending)
      throw StateError("ABORT_ERR");
    if (state.timeout)
      g_source_remove(state.timeout);
    state.timeout = 0;
    state.pending.clear();
    if (!options.get("handled").as<JSON::Boolean>().value())
      fallback(window);
    return nullptr;
  }
}  // namespace oro::runtime::native
#endif
