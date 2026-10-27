#include "src/runtime/native/desktop_credentials.hh"
#include <dlfcn.h>
#include <future>
#include <thread>
#include <cassert>
#include <iostream>

using namespace oro::runtime;
using namespace oro::runtime::native;

void seed (const char* application, const char* username, const char* password) {
  auto library = dlopen("libsecret-1.so.0", RTLD_NOW);
  assert(library);
  const auto make =
    reinterpret_cast<gpointer (*)(const gchar*, gssize, const gchar*)>(dlsym(library, "secret_value_new"));
  const auto release = reinterpret_cast<void (*)(gpointer)>(dlsym(library, "secret_value_unref"));
  const auto store =
    reinterpret_cast<gboolean (*)(gpointer, gpointer, GHashTable*, const gchar*, const gchar*, gpointer, GCancellable*,
                                  GError**)>(dlsym(library, "secret_service_store_sync"));
  assert(make && release && store);
  const auto attributes = g_hash_table_new(g_str_hash, g_str_equal);
  g_hash_table_insert(attributes, const_cast<char*>("application"), const_cast<char*>(application));
  g_hash_table_insert(attributes, const_cast<char*>("username"), const_cast<char*>(username));
  const auto value = make(password, -1, "text/plain");
  GError* error = nullptr;
  assert(store(nullptr, nullptr, attributes, nullptr, "Public runtime password test", value, nullptr, &error));
  assert(!error);
  release(value);
  g_hash_table_unref(attributes);
}

int main (int argc, char** argv) {
  assert(gtk_init_check(&argc, &argv));
  assert(DesktopCredentials::available());
  seed("org.oro.PasswordPublicTest", "public-a", "public-password-a");
  seed("org.oro.PasswordPublicTest", "public-b", "public-password-b");
  seed("org.oro.OtherPublicTest", "other-app", "never-selected");
  const auto parent = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_title(parent, "Public native password test");
  gtk_widget_show_all(GTK_WIDGET(parent));
  auto check = [&] (const std::string& username, int mode, const std::string& expectedError) {
    gtk_widget_show(GTK_WIDGET(parent));
    gtk_window_present(parent);
    gdk_window_focus(gtk_widget_get_window(GTK_WIDGET(parent)), GDK_CURRENT_TIME);
    const auto focusDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!gtk_window_is_active(parent) && std::chrono::steady_clock::now() < focusDeadline)
      g_main_context_iteration(nullptr, true);
    assert(gtk_window_is_active(parent));
    const auto signal = std::make_shared<ExecutionCancellation>(10000);
    auto result = std::async(std::launch::async, [&, signal] {
      try {
        auto value = DesktopCredentials::select("org.oro.PasswordPublicTest", username, parent, signal);
        assert(expectedError.empty());
        assert(value.username == "public-b");
        assert(value.password == "public-password-b");
      } catch (const StateError& error) {
        assert(error.code == expectedError);
      }
    });
    bool handled = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      assert(std::chrono::steady_clock::now() < deadline);
      while (g_main_context_pending(nullptr))
        g_main_context_iteration(nullptr, false);
      auto windows = gtk_window_list_toplevels();
      for (auto item = windows; item; item = item->next) {
        if (!GTK_IS_DIALOG(item->data) || !gtk_widget_get_visible(GTK_WIDGET(item->data)) || handled)
          continue;
        const auto dialog = GTK_DIALOG(item->data);
        auto children = gtk_container_get_children(GTK_CONTAINER(gtk_dialog_get_content_area(dialog)));
        GtkComboBox* combo = nullptr;
        for (auto child = children; child; child = child->next)
          if (GTK_IS_COMBO_BOX(child->data))
            combo = GTK_COMBO_BOX(child->data);
        assert(combo);
        const auto count = gtk_tree_model_iter_n_children(gtk_combo_box_get_model(combo), nullptr);
        if (count != (username.empty() ? 2 : 1))
          std::cerr << "Public selection mode=" << mode << " count=" << count << std::endl;
        assert(count == (username.empty() ? 2 : 1));
        g_list_free(children);
        assert(DesktopCredentials::ownsPrompt(GTK_WIDGET(parent)));
        handled = true;
        if (mode == 2) {
          signal->cancel();
          // Shutdown must release the worker even while the GTK thread cannot pump.
          assert(result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        } else if (mode == 3)
          gtk_widget_hide(GTK_WIDGET(parent));
        else {
          if (username.empty())
            gtk_combo_box_set_active(combo, 1);
          gtk_dialog_response(dialog, mode == 1 ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_ACCEPT);
          gdk_window_focus(gtk_widget_get_window(GTK_WIDGET(parent)), GDK_CURRENT_TIME);
        }
      }
      g_list_free(windows);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    result.get();
    const auto cleanup = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (DesktopCredentials::ownsPrompt(GTK_WIDGET(parent)) && std::chrono::steady_clock::now() < cleanup) {
      while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, false);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(!DesktopCredentials::ownsPrompt(GTK_WIDGET(parent)));
  };
  check("public-b", 0, "");
  check("", 1, "ABORT_ERR");
  check("public-a", 2, "ABORT_ERR");
  check("public-a", 3, "ABORT_ERR");
  check("missing", 0, "ENOENT");
  check("public-b", 0, "");
  gtk_widget_destroy(GTK_WIDGET(parent));
  std::cout << "linux-password-selection-filter-cancel-lifecycle-retry-passed" << std::endl;
}
