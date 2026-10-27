#include "src/runtime/native/desktop_dialog.hh"
#include <cassert>
#include <future>
#include <thread>
#include <iostream>

using namespace oro::runtime::native;
GtkWidget* entry (GtkWidget* widget) {
  if (GTK_IS_ENTRY (widget))
    return widget;
  if (!GTK_IS_CONTAINER (widget))
    return nullptr;
  auto children = gtk_container_get_children (GTK_CONTAINER (widget));
  GtkWidget* found = nullptr;
  for (auto item = children; item && !found; item = item->next)
    found = entry (GTK_WIDGET (item->data));
  g_list_free (children);
  return found;
}
int main (int argc, char** argv) {
  gtk_init (&argc, &argv);
  auto parent = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
  auto check = [&] (int mode, bool protectedPath, const std::string& expected) {
    gtk_widget_show_all (GTK_WIDGET (parent));
    gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (parent)), GDK_CURRENT_TIME);
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    while (!gtk_window_is_active (parent) && std::chrono::steady_clock::now () < deadline) {
      g_main_context_iteration (nullptr, false);
      std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    assert (gtk_window_is_active (parent));
    auto signal = std::make_shared<ExecutionCancellation> (10000);
    auto result = std::async (std::launch::async, [&] {
      try {
        const auto pin = promptNativePin (parent, "Public provider PIN test", protectedPath, signal);
        assert (expected.empty ());
        assert (protectedPath ? pin.empty () : std::string (pin.begin (), pin.end ()) == "public-test-pin");
      } catch (const StateError& error) {
        assert (!expected.empty () && error.code == expected);
      }
    });
    while (result.wait_for (std::chrono::milliseconds (0)) != std::future_status::ready) {
      assert (std::chrono::steady_clock::now () < deadline);
      while (g_main_context_pending (nullptr))
        g_main_context_iteration (nullptr, false);
      auto windows = gtk_window_list_toplevels ();
      for (auto item = windows; item; item = item->next) {
        auto widget = GTK_WIDGET (item->data);
        if (!GTK_IS_DIALOG (widget) || !gtk_widget_get_visible (widget))
          continue;
        const auto input = entry (widget);
        assert (protectedPath ? !input : input && !gtk_entry_get_visibility (GTK_ENTRY (input)));
        if (mode == 2) {
          signal->cancel ();
          assert (result.wait_for (std::chrono::seconds (1)) == std::future_status::ready);
        } else if (mode == 3) {
          gtk_widget_hide (GTK_WIDGET (parent));
        } else {
          if (input)
            gtk_entry_set_text (GTK_ENTRY (input), "public-test-pin");
          gtk_dialog_response (GTK_DIALOG (widget), mode == 1 ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_ACCEPT);
          gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (parent)), GDK_CURRENT_TIME);
        }
      }
      g_list_free (windows);
      std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    result.get ();
    while (ownsNativePrompt (GTK_WIDGET (parent)) && std::chrono::steady_clock::now () < deadline) {
      g_main_context_iteration (nullptr, false);
      std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    assert (!ownsNativePrompt (GTK_WIDGET (parent)));
  };
  check (0, false, "");
  check (1, false, "ABORT_ERR");
  check (2, false, "ABORT_ERR");
  check (3, false, "ABORT_ERR");
  check (0, true, "");
  check (0, false, "");
  gtk_widget_destroy (GTK_WIDGET (parent));
  std::cout << "linux-native-pin-mask-cancel-focus-protected-path-retry-passed\n";
}
