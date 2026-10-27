#include "src/runtime/app.hh"
#include "src/runtime/filesystem.hh"
#include "src/runtime/native/linux_platform.hh"
#include <iostream>
#include <atomic>
#include <thread>

extern "C" {
  const unsigned char* oro_runtime_init_get_user_config_bytes () { return reinterpret_cast<const unsigned char*>(""); }
  unsigned int oro_runtime_init_get_user_config_bytes_size () { return 0; }
  bool oro_runtime_init_is_debug_enabled () { return false; }
  const char* oro_runtime_init_get_dev_host () { return "localhost"; }
  int oro_runtime_init_get_dev_port () { return 0; }
  int oro_runtime_init_get_user_config_format () { return 0; }
}
using namespace oro::runtime;
int main (int argc, char** argv) {
  if (argc != 2 || !gtk_init_check(&argc, &argv)) return 1;
  g_object_set(gtk_settings_get_default(), "gtk-xft-dpi", 144 * 1024, nullptr);
  filesystem::Resource::setResourcesPath(std::filesystem::absolute(argv[1]));
  app::App::Options options;
  options.userConfig = {{"meta_bundle_identifier", "org.oro.PublicRuntime"}, {"meta_title", "Public runtime UI"},
    {"build_name", "public_runtime_ui"}, {"permissions_allow_usb", "false"}, {"permissions_allow_bluetooth", "false"},
    {"permissions_hardware_acceleration_disabled", "true"}};
  options.features.useAI = false;
  options.features.useUSB = false;
  options.features.useBluetooth = false;
  options.features.useHID = false;
  options.features.useHCI = false;
  options.features.useIroh = false;
  options.features.useIPFS = false;
  options.features.useGeolocation = false;
  options.features.useMediaDevices = false;
  options.features.useMCP = false;
  auto app = new app::App(options);
  const auto backend = app->runtime.services.platform.nativeLinux();
  std::atomic<int> reads = 0;
  std::atomic<bool> waiting = false;
  std::atomic<int> cancelled = 0;
  backend->registerExtension("public.metadata", [&](const auto& action, const auto&, const auto& signal) -> JSON::Any {
    if (action == "wait") {
      waiting = true;
      while (!signal.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      cancelled++;
      waiting = false;
      return JSON::Object::Entries {{"private", "public-test-result-must-not-arrive"}};
    }
    reads++;
    return JSON::Object::Entries {{"public", true}};
  });
  std::shared_ptr<window::Manager::ManagedWindow> view;
  auto manager = window::ManagerOptions();
  manager.userConfig = options.userConfig;
  manager.defaultWidth = "640"; manager.defaultHeight = "480";
  int status = 1;
  manager.onMessage = [&](const String& message) {
    if (message.starts_with("PUBLIC_UI_STAGE:")) std::cout << message << std::endl;
    if (message == "PUBLIC_UI_SCALE_RESET") {
      g_object_set(gtk_settings_get_default(), "gtk-xft-dpi", 96 * 1024, nullptr);
    }
    if (message == "PUBLIC_UI_BACK_PRESS") {
      GdkEventKey event {};
      event.type = GDK_KEY_PRESS;
      event.keyval = GDK_KEY_Left;
      event.state = GDK_MOD1_MASK;
      gboolean handled = false;
      g_signal_emit_by_name(view->webview, "key-press-event", &event, &handled);
      if (!handled) { std::cerr << "Public native Back key was not intercepted\n"; gtk_main_quit(); }
    }
    if (message == "PUBLIC_UI_RELOAD") {
      if (!waiting) return;
      webkit_web_view_reload(view->webview);
    } else if (message == "PUBLIC_UI_CRASH") {
      if (!waiting) return;
      webkit_web_view_terminate_web_process(view->webview);
      g_timeout_add(500, [](gpointer pointer) -> gboolean {
        const auto view = static_cast<window::Window*>(pointer);
        const auto panel = GTK_WIDGET(g_object_get_data(G_OBJECT(view->window), "oro-recovery-panel"));
        const auto button = GTK_WIDGET(g_object_get_data(G_OBJECT(view->window), "oro-recovery-button"));
        if (!panel || !button || !gtk_widget_get_visible(panel) || gtk_widget_get_visible(GTK_WIDGET(view->webview))) {
          std::cerr << "Public renderer recovery was not held for user retry\n";
          gtk_main_quit();
        } else { gtk_button_clicked(GTK_BUTTON(button)); }
        return G_SOURCE_REMOVE;
      }, view.get());
    } else if (message == "PUBLIC_UI_PASSED") {
      if (reads != 3 || cancelled != 3) { std::cerr << "Public UI native counts did not match\n"; }
      else { status = 0; std::cout << "linux-webkit-isolated-native-ipc-cancellation-reload-passed\n";
        std::cout << "linux-webkit-process-loss-user-recovery-passed\n"; }
      gtk_main_quit();
    } else if (message.starts_with("PUBLIC_UI_FAILED:")) {
      std::cerr << message << std::endl;
      gtk_main_quit();
    }
  };
  app->runtime.windowManager.configure(manager);
  view = app->runtime.windowManager.createDefaultWindow(window::Window::Options {
    .shouldExitApplicationOnClose = false, .height = 480, .width = 640
  });
  view->show();
  view->navigate(url::URL("oro://org.oro.PublicRuntime/index.html"));
  g_timeout_add(100, [](gpointer pointer) -> gboolean {
    const auto window = static_cast<window::Window*>(pointer);
    if (window->window && gtk_widget_get_window(window->window)) gdk_window_focus(gtk_widget_get_window(window->window), GDK_CURRENT_TIME);
    return G_SOURCE_CONTINUE;
  }, view.get());
  g_timeout_add(45000, [](gpointer) -> gboolean { std::cerr << "Public UI timed out\n"; gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);
  gtk_main();
  backend->shutdown();
  return status;
}
