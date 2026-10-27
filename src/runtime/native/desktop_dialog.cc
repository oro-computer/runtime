#include "desktop_dialog.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include <condition_variable>
#include <mutex>
#include <cstring>

namespace oro::runtime::native {
  bool ownsNativePrompt (GtkWidget* parent) {
    return g_object_get_data(G_OBJECT(parent), interactivePromptKey) != nullptr;
  }
  std::shared_ptr<GtkWindow> retainNativeWindow (GtkWindow* parent) {
    if (!parent)
      return nullptr;
    return std::shared_ptr<GtkWindow>(GTK_WINDOW(g_object_ref(parent)), [] (GtkWindow* value) {
      g_idle_add_full(
        G_PRIORITY_DEFAULT,
        +[] (gpointer pointer) -> gboolean {
          g_object_unref(pointer);
          return G_SOURCE_REMOVE;
        },
        value, nullptr);
    });
  }
  namespace {
    struct Prompt {
      std::shared_ptr<GtkWindow> parent;
      std::shared_ptr<ExecutionCancellation> signal;
      std::string title;
      std::mutex mutex;
      std::condition_variable changed;
      bool protectedPath = false;
      bool waiting = false;
      bool finished = false;
      std::string failure;
      RecordBytes pin;
      GtkWidget* dialog = nullptr;
      GtkWidget* entry = nullptr;
      std::chrono::steady_clock::time_point resumeDeadline;
      ~Prompt() {
        auto bytes = static_cast<volatile unsigned char*>(pin.data());
        for (size_t i = 0; i < pin.size(); i++)
          bytes[i] = 0;
      }
      void finish (const std::string& error) {
        std::lock_guard lock(mutex);
        failure = error;
        finished = true;
        changed.notify_all();
      }
    };
  }  // namespace
  RecordBytes promptNativePin (GtkWindow* parent, const std::string& title, bool protectedPath,
                               const std::shared_ptr<ExecutionCancellation>& signal) {
    if (!parent || !signal || title.empty() || title.size() > 128 || title.find('\0') != std::string::npos ||
        !g_utf8_validate(title.c_str(), title.size(), nullptr))
      throw StateError("EINVAL");
    auto prompt = std::make_shared<Prompt>();
    prompt->parent = retainNativeWindow(parent);
    prompt->signal = signal;
    prompt->title = title;
    prompt->protectedPath = protectedPath;
    g_idle_add_full(
      G_PRIORITY_DEFAULT,
      +[] (gpointer pointer) -> gboolean {
        auto prompt = *static_cast<std::shared_ptr<Prompt>*>(pointer);
        if (prompt->signal->cancelled() || !gtk_widget_get_visible(GTK_WIDGET(prompt->parent.get())) ||
            !gtk_window_is_active(prompt->parent.get())) {
          prompt->finish("ABORT_ERR");
          return G_SOURCE_REMOVE;
        }
        if (ownsNativePrompt(GTK_WIDGET(prompt->parent.get()))) {
          prompt->finish("EBUSY");
          return G_SOURCE_REMOVE;
        }
        g_object_set_data(G_OBJECT(prompt->parent.get()), interactivePromptKey, prompt.get());
        prompt->dialog =
          gtk_dialog_new_with_buttons(prompt->title.c_str(), prompt->parent.get(),
                                      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                      "Cancel", GTK_RESPONSE_CANCEL, "Continue", GTK_RESPONSE_ACCEPT, nullptr);
        g_object_ref_sink(prompt->dialog);
        const auto content = gtk_dialog_get_content_area(GTK_DIALOG(prompt->dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 16);
        const auto label =
          gtk_label_new(prompt->protectedPath ? "Use your device authentication controls after continuing."
                                              : "Enter the key provider PIN for this cryptographic operation.");
        gtk_label_set_line_wrap(GTK_LABEL(label), true);
        gtk_box_pack_start(GTK_BOX(content), label, true, true, 8);
        if (!prompt->protectedPath) {
          prompt->entry = gtk_entry_new();
          g_object_ref_sink(prompt->entry);
          gtk_widget_set_name(prompt->entry, "oro-protected-key-pin");
          gtk_entry_set_visibility(GTK_ENTRY(prompt->entry), false);
          gtk_entry_set_input_purpose(GTK_ENTRY(prompt->entry), GTK_INPUT_PURPOSE_PASSWORD);
          gtk_entry_set_max_length(GTK_ENTRY(prompt->entry), 256);
          gtk_entry_set_activates_default(GTK_ENTRY(prompt->entry), true);
          gtk_box_pack_start(GTK_BOX(content), prompt->entry, false, false, 8);
        }
        gtk_dialog_set_default_response(GTK_DIALOG(prompt->dialog), GTK_RESPONSE_ACCEPT);
        g_signal_connect(prompt->dialog, "response",
                         G_CALLBACK(+[] (GtkDialog* dialog, gint response, gpointer pointer) {
                           auto prompt = static_cast<Prompt*>(pointer);
                           if (response != GTK_RESPONSE_ACCEPT)
                             prompt->failure = "ABORT_ERR";
                           if (response == GTK_RESPONSE_ACCEPT && prompt->entry) {
                             const auto value = gtk_entry_get_text(GTK_ENTRY(prompt->entry));
                             const auto size = strlen(value);
                             if (!size || size > 256)
                               prompt->failure = "EACCES";
                             else
                               prompt->pin.assign(value, value + size);
                             gtk_entry_set_text(GTK_ENTRY(prompt->entry), "");
                           }
                           prompt->waiting = true;
                           prompt->resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                           gtk_widget_hide(GTK_WIDGET(dialog));
                           gtk_window_present(prompt->parent.get());
                         }),
                         prompt.get());
        g_signal_connect(prompt->dialog, "focus-out-event",
                         G_CALLBACK(+[] (GtkWidget*, GdkEventFocus*, gpointer pointer) -> gboolean {
                           auto prompt = static_cast<Prompt*>(pointer);
                           if (!prompt->waiting)
                             prompt->signal->cancel();
                           return FALSE;
                         }),
                         prompt.get());
        gtk_widget_show_all(prompt->dialog);
        if (prompt->entry)
          gtk_widget_grab_focus(prompt->entry);
        g_timeout_add_full(
          G_PRIORITY_DEFAULT, 25,
          +[] (gpointer pointer) -> gboolean {
            auto prompt = *static_cast<std::shared_ptr<Prompt>*>(pointer);
            const bool cancelled =
              prompt->signal->cancelled() || !gtk_widget_get_visible(GTK_WIDGET(prompt->parent.get()));
            const bool resumed = prompt->waiting && gtk_window_is_active(prompt->parent.get());
            const bool expired = prompt->waiting && std::chrono::steady_clock::now() >= prompt->resumeDeadline;
            if (!cancelled && !resumed && !expired)
              return G_SOURCE_CONTINUE;
            if (g_object_get_data(G_OBJECT(prompt->parent.get()), interactivePromptKey) == prompt.get())
              g_object_set_data(G_OBJECT(prompt->parent.get()), interactivePromptKey, nullptr);
            if (prompt->entry)
              gtk_entry_set_text(GTK_ENTRY(prompt->entry), "");
            gtk_widget_destroy(prompt->dialog);
            g_object_unref(prompt->dialog);
            if (prompt->entry)
              g_object_unref(prompt->entry);
            prompt->finish(cancelled || expired ? "ABORT_ERR" : prompt->failure);
            return G_SOURCE_REMOVE;
          },
          new std::shared_ptr<Prompt>(prompt),
          +[] (gpointer pointer) { delete static_cast<std::shared_ptr<Prompt>*>(pointer); });
        return G_SOURCE_REMOVE;
      },
      new std::shared_ptr<Prompt>(prompt),
      +[] (gpointer pointer) { delete static_cast<std::shared_ptr<Prompt>*>(pointer); });
    std::unique_lock lock(prompt->mutex);
    while (!prompt->finished) {
      if (signal->cancelled())
        throw StateError("ABORT_ERR");
      prompt->changed.wait_for(lock, std::chrono::milliseconds(25));
    }
    if (!prompt->failure.empty())
      throw StateError(prompt->failure);
    return std::move(prompt->pin);
  }
}  // namespace oro::runtime::native
#endif
