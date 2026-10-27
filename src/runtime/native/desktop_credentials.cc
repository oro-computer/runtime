#include "desktop_credentials.hh"
#include "desktop_dialog.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include <dlfcn.h>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace oro::runtime::native {
  namespace {
    // Keep the library mapped: its GTypes and asynchronous finalizers outlive individual requests.
    struct Secrets {
      void* library = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
      gpointer (*get)(int, GCancellable*, GError**) = nullptr;
      GList* (*search)(gpointer, gpointer, GHashTable*, int, GCancellable*, GError**) = nullptr;
      GHashTable* (*attributes)(gpointer) = nullptr;
      int (*unlock)(gpointer, GList*, GCancellable*, GList**, GError**) = nullptr;
      gboolean (*load)(gpointer, GCancellable*, GError**) = nullptr;
      gpointer (*secret)(gpointer) = nullptr;
      const gchar* (*valueText)(gpointer) = nullptr;
      void (*valueUnref)(gpointer) = nullptr;

      Secrets() {
        if (!library)
          return;
#define ORO_SECRET_SYMBOL(field, symbol) field = reinterpret_cast<decltype(field)>(dlsym(library, symbol))
        ORO_SECRET_SYMBOL(get, "secret_service_get_sync");
        ORO_SECRET_SYMBOL(search, "secret_service_search_sync");
        ORO_SECRET_SYMBOL(attributes, "secret_item_get_attributes");
        ORO_SECRET_SYMBOL(unlock, "secret_service_unlock_sync");
        ORO_SECRET_SYMBOL(load, "secret_item_load_secret_sync");
        ORO_SECRET_SYMBOL(secret, "secret_item_get_secret");
        ORO_SECRET_SYMBOL(valueText, "secret_value_get_text");
        ORO_SECRET_SYMBOL(valueUnref, "secret_value_unref");
#undef ORO_SECRET_SYMBOL
      }
      bool ready () const {
        return get && search && attributes && unlock && load && secret && valueText && valueUnref;
      }
    };

    Secrets& secrets () {
      static Secrets instance;
      return instance;
    }

    void checked (GError* error, const std::shared_ptr<ExecutionCancellation>& signal) {
      if (error) {
        const bool cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        const bool unavailable = g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
          g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
          g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED);
        const bool expired = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
          g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY);
        g_error_free(error);
        throw StateError(cancelled || signal->cancelled() ? "ABORT_ERR" : unavailable ? "ENOTREADY" : expired ? "ETIMEDOUT" : "EACCES");
      }
      if (signal->cancelled())
        throw StateError("ABORT_ERR");
    }

    bool publicText (const char* value, size_t limit, bool empty = false) {
      if (!value || (!empty && !*value) || strlen(value) > limit || !g_utf8_validate(value, -1, nullptr))
        return false;
      for (const auto* ch = reinterpret_cast<const unsigned char*>(value); *ch; ch++)
        if (*ch < 32 || *ch == 127)
          return false;
      return true;
    }

    struct Prompt {
      GtkWindow* parent;
      std::shared_ptr<GtkWindow> parentOwner;
      std::shared_ptr<ExecutionCancellation> signal;
      std::vector<std::string> usernames;
      std::mutex mutex;
      std::condition_variable changed;
      bool finished = false;
      int selection = -1;
      GtkWidget* dialog = nullptr;
      GtkWidget* list = nullptr;
      std::chrono::steady_clock::time_point resumeDeadline;
      bool waitingForParent = false;

      void finish (int result) {
        std::lock_guard lock(mutex);
        selection = result;
        finished = true;
        changed.notify_all();
      }
    };

    int choose (GtkWindow* parent, std::vector<std::string> usernames,
                const std::shared_ptr<ExecutionCancellation>& signal) {
      auto prompt = std::make_shared<Prompt>();
      prompt->parent = parent;
      prompt->parentOwner = std::shared_ptr<GtkWindow>(GTK_WINDOW(g_object_ref(parent)), [](GtkWindow* value) {
        g_idle_add_full(G_PRIORITY_DEFAULT, +[](gpointer pointer) -> gboolean {
          g_object_unref(pointer);
          return G_SOURCE_REMOVE;
        }, value, nullptr);
      });
      prompt->signal = signal;
      prompt->usernames = std::move(usernames);
      auto owned = new std::shared_ptr<Prompt>(prompt);
      g_idle_add_full(
        G_PRIORITY_DEFAULT,
        +[] (gpointer pointer) -> gboolean {
          auto prompt = *static_cast<std::shared_ptr<Prompt>*>(pointer);
          delete static_cast<std::shared_ptr<Prompt>*>(pointer);
          if (prompt->signal->cancelled() || !gtk_widget_get_visible(GTK_WIDGET(prompt->parent)) ||
              !gtk_window_is_active(prompt->parent)) {
            prompt->finish(-1);
            return G_SOURCE_REMOVE;
          }
          if (ownsNativePrompt(GTK_WIDGET(prompt->parent))) {
            prompt->finish(-2);
            return G_SOURCE_REMOVE;
          }
          g_object_set_data(G_OBJECT(prompt->parent), interactivePromptKey, prompt.get());
          prompt->dialog =
            gtk_dialog_new_with_buttons("Choose saved password", prompt->parent,
                                        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                                        "Cancel", GTK_RESPONSE_CANCEL, "Choose", GTK_RESPONSE_ACCEPT, nullptr);
          g_object_ref_sink(prompt->dialog);
          gtk_window_set_default_size(GTK_WINDOW(prompt->dialog), 400, 180);
          prompt->list = gtk_combo_box_text_new();
          g_object_ref_sink(prompt->list);
          gtk_widget_set_name(prompt->list, "oro-password-accounts");
          for (const auto& username : prompt->usernames)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(prompt->list), username.c_str());
          gtk_combo_box_set_active(GTK_COMBO_BOX(prompt->list), 0);
          gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(prompt->dialog))), 16);
          gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(prompt->dialog))), prompt->list, true, true,
                             0);
          g_signal_connect(
            prompt->dialog, "response", G_CALLBACK(+[] (GtkDialog* dialog, gint response, gpointer pointer) {
              auto prompt = static_cast<Prompt*>(pointer);
              prompt->selection =
                response == GTK_RESPONSE_ACCEPT ? gtk_combo_box_get_active(GTK_COMBO_BOX(prompt->list)) : -1;
              prompt->waitingForParent = true;
              prompt->resumeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
              gtk_widget_hide(GTK_WIDGET(dialog));
              gtk_window_present(prompt->parent);
            }),
            prompt.get());
          g_signal_connect(prompt->dialog, "focus-out-event",
                           G_CALLBACK(+[] (GtkWidget*, GdkEventFocus*, gpointer pointer) -> gboolean {
                             auto prompt = static_cast<Prompt*>(pointer);
                             if (!prompt->waitingForParent)
                               prompt->signal->cancel();
                             return FALSE;
                           }),
                           prompt.get());
          gtk_widget_show_all(prompt->dialog);
          g_timeout_add_full(
            G_PRIORITY_DEFAULT, 25,
            +[] (gpointer pointer) -> gboolean {
              auto prompt = *static_cast<std::shared_ptr<Prompt>*>(pointer);
              const bool cancelled = prompt->signal->cancelled() || !gtk_widget_get_visible(GTK_WIDGET(prompt->parent));
              const bool resumed = prompt->waitingForParent && gtk_window_is_active(prompt->parent);
              const bool expired =
                prompt->waitingForParent && std::chrono::steady_clock::now() >= prompt->resumeDeadline;
              if (!cancelled && !resumed && !expired)
                return G_SOURCE_CONTINUE;
              if (g_object_get_data(G_OBJECT(prompt->parent), interactivePromptKey) == prompt.get())
                g_object_set_data(G_OBJECT(prompt->parent), interactivePromptKey, nullptr);
              if (prompt->dialog) {
                gtk_widget_destroy(prompt->dialog);
                g_object_unref(prompt->dialog);
                g_object_unref(prompt->list);
              }
              prompt->finish(cancelled || expired ? -1 : prompt->selection);
              return G_SOURCE_REMOVE;
            },
            new std::shared_ptr<Prompt>(prompt),
            +[] (gpointer pointer) { delete static_cast<std::shared_ptr<Prompt>*>(pointer); });
          return G_SOURCE_REMOVE;
        },
        owned, nullptr);
      std::unique_lock lock(prompt->mutex);
      while (!prompt->finished) {
        if (signal->cancelled()) return -1;
        prompt->changed.wait_for(lock, std::chrono::milliseconds(25));
      }
      return prompt->selection;
    }
  }  // namespace

  bool DesktopCredentials::available() {
    return secrets().ready();
  }

  bool DesktopCredentials::ownsPrompt(GtkWidget* parent) {
    return ownsNativePrompt(parent);
  }

  DesktopPassword DesktopCredentials::select(const std::string& application, const std::string& username,
                                             GtkWindow* parent, const std::shared_ptr<ExecutionCancellation>& signal) {
    auto& api = secrets();
    if (!api.ready())
      throw StateError("ENOSYS");
    if (!parent || !publicText(application.c_str(), 255) || !publicText(username.c_str(), 320, true))
      throw StateError("EINVAL");
    if (signal->cancelled())
      throw StateError("ABORT_ERR");
    auto cancellable = std::shared_ptr<GCancellable>(g_cancellable_new(), g_object_unref);
    // GCancellable interrupts provider I/O even while this worker is inside libsecret.
    std::jthread monitor([signal, cancellable](std::stop_token stop) {
      while (!stop.stop_requested()) {
        if (signal->cancelled()) {
          g_cancellable_cancel(cancellable.get());
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
    GError* error = nullptr;
    auto service = std::shared_ptr<void>(api.get(0, cancellable.get(), &error), [](gpointer value) {
      if (value) g_object_unref(value);
    });
    checked(error, signal);
    if (!service)
      throw StateError("ENOTREADY");
    auto attributes = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(attributes, const_cast<char*>("application"), const_cast<char*>(application.c_str()));
    if (!username.empty())
      g_hash_table_insert(attributes, const_cast<char*>("username"), const_cast<char*>(username.c_str()));
    // Search metadata only. Unlocking and secret retrieval follow explicit selection.
    constexpr int searchAllMetadata = 2;
    auto items = api.search(service.get(), nullptr, attributes, searchAllMetadata, cancellable.get(), &error);
    g_hash_table_unref(attributes);
    struct Items {
      GList* value;
      ~Items() {
        g_list_free_full(value, g_object_unref);
      }
    } itemsOwner{items};
    checked(error, signal);
    std::vector<gpointer> candidates;
    std::vector<std::string> usernames;
    for (auto item = items; item; item = item->next) {
      auto values = api.attributes(item->data);
      const auto app = static_cast<const char*>(g_hash_table_lookup(values, "application"));
      const auto name = static_cast<const char*>(g_hash_table_lookup(values, "username"));
      const bool eligible =
        app && application == app && publicText(name, 320) && (username.empty() || username == name);
      if (eligible) {
        candidates.push_back(item->data);
        usernames.emplace_back(name);
      }
      g_hash_table_unref(values);
      if (candidates.size() > 64)
        throw StateError("E2BIG");
    }
    if (candidates.empty())
      throw StateError("ENOENT");
    const auto selected = choose(parent, usernames, signal);
    if (selected == -2) throw StateError("EBUSY");
    if (selected < 0 || static_cast<size_t>(selected) >= candidates.size())
      throw StateError("ABORT_ERR");
    checked(nullptr, signal);
    GList one{candidates[selected], nullptr, nullptr};
    GList* unlocked = nullptr;
    api.unlock(service.get(), &one, cancellable.get(), &unlocked, &error);
    g_list_free_full(unlocked, g_object_unref);
    checked(error, signal);
    const bool loaded = api.load(candidates[selected], cancellable.get(), &error);
    checked(error, signal);
    if (!loaded)
      throw StateError("EACCES");
    const auto value = api.secret(candidates[selected]);
    if (!value)
      throw StateError("ENOENT");
    auto secret = std::shared_ptr<void>(value, api.valueUnref);
    const auto password = api.valueText(secret.get());
    if (!password || strlen(password) > 65536 || !g_utf8_validate(password, -1, nullptr))
      throw StateError("EBADMSG");
    checked(nullptr, signal);
    return {usernames[selected], std::string(password)};
  }
}  // namespace oro::runtime::native
#endif
