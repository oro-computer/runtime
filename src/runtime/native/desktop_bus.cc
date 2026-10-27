#include "desktop_bus.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include <algorithm>

namespace oro::runtime::native {
  namespace {
    [[noreturn]] void failure (GError* error) {
      std::string code = "EIO";
      if (error) {
        if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED) ||
            g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_AUTH_FAILED))
          code = "EACCES";
        else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
                 g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER) ||
                 g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED))
          code = "ENOTREADY";
        else if (g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD) ||
                 g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_INTERFACE))
          code = "ENOSYS";
        else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
                 g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY))
          code = "ETIMEDOUT";
        g_error_free(error);
      }
      throw StateError(code);
    }
  }  // namespace

  DesktopBus::DesktopBus() {
    GError* error = nullptr;
    auto address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!address)
      failure(error);
    this->connection = g_dbus_connection_new_for_address_sync(
      address,
      static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                        G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      nullptr, nullptr, &error);
    g_free(address);
    if (!this->connection)
      failure(error);
    g_dbus_connection_set_exit_on_close(this->connection, false);
  }

  DesktopBus::~DesktopBus() {
    for (const auto id : this->subscriptions)
      g_dbus_connection_signal_unsubscribe(this->connection, id);
    for (const auto id : this->objects)
      g_dbus_connection_unregister_object(this->connection, id);
    g_dbus_connection_close_sync(this->connection, nullptr, nullptr);
    g_object_unref(this->connection);
  }

  gulong DesktopBus::onClosed (std::function<void()> callback) {
    auto handler = new std::function<void()>(std::move(callback));
    return g_signal_connect_data(this->connection, "closed", G_CALLBACK(+[](
      GDBusConnection*, gboolean, GError*, gpointer pointer) {
      try { (*static_cast<std::function<void()>*>(pointer))(); } catch (...) {}
    }), handler, +[](gpointer pointer, GClosure*) { delete static_cast<std::function<void()>*>(pointer); },
      static_cast<GConnectFlags>(0));
  }

  void DesktopBus::removeClosedHandler (gulong handler) {
    if (handler) g_signal_handler_disconnect(this->connection, handler);
  }

  DesktopBus::Variant DesktopBus::call(const std::string& destination, const std::string& path,
                                       const std::string& interface, const std::string& method, GVariant* parameters,
                                       const GVariantType* resultType, int timeoutMs) {
    if (timeoutMs < 1 || timeoutMs > 95000)
      throw StateError("EINVAL");
    GError* error = nullptr;
    const auto result = g_dbus_connection_call_sync(this->connection, destination.c_str(), path.c_str(),
                                                    interface.c_str(), method.c_str(), parameters, resultType,
                                                    G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr, &error);
    if (!result)
      failure(error);
    return Variant(result, g_variant_unref);
  }

  void DesktopBus::callAsync (const std::string& destination, const std::string& path,
                              const std::string& interface, const std::string& method, GVariant* parameters,
                              const GVariantType* resultType, Completion completion, int timeoutMs) {
    if (timeoutMs < 1 || timeoutMs > 10000 || !completion) throw StateError("EINVAL");
    auto callback = new Completion(std::move(completion));
    g_dbus_connection_call(this->connection, destination.c_str(), path.c_str(), interface.c_str(), method.c_str(),
      parameters, resultType, G_DBUS_CALL_FLAGS_NONE, timeoutMs, nullptr,
      +[](GObject* connection, GAsyncResult* result, gpointer pointer) {
        std::unique_ptr<Completion> callback(static_cast<Completion*>(pointer));
        GError* error = nullptr;
        Variant value(g_dbus_connection_call_finish(G_DBUS_CONNECTION(connection), result, &error), g_variant_unref);
        std::string code;
        if (error) {
          try { failure(error); } catch (const StateError& failure) { code = failure.code; }
        }
        try { (*callback)(std::move(value), code); } catch (...) {}
      }, callback);
  }

  std::string DesktopBus::owner(const std::string& name) {
    try {
      const auto result = this->call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                     "GetNameOwner", g_variant_new("(s)", name.c_str()), G_VARIANT_TYPE("(s)"));
      const char* owner = nullptr;
      g_variant_get(result.get(), "(&s)", &owner);
      return owner;
    } catch (const StateError& error) {
      if (error.code == "ENOTREADY")
        return "";
      throw;
    }
  }

  std::vector<std::string> DesktopBus::names() {
    std::set<std::string> names;
    for (const auto method : {"ListNames", "ListActivatableNames"}) {
      const auto result = this->call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", method,
                                     nullptr, G_VARIANT_TYPE("(as)"));
      GVariantIter* iterator = nullptr;
      g_variant_get(result.get(), "(as)", &iterator);
      const char* value = nullptr;
      while (g_variant_iter_next(iterator, "&s", &value)) {
        if (names.size() >= 4096) {
          g_variant_iter_free(iterator);
          throw StateError("E2BIG");
        }
        names.insert(value);
      }
      g_variant_iter_free(iterator);
    }
    return {names.begin(), names.end()};
  }

  void DesktopBus::own(const std::string& name) {
    if (!g_dbus_is_name(name.c_str()) || name.front() == ':')
      throw StateError("EINVAL");
    const auto result = this->call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                   "RequestName", g_variant_new("(su)", name.c_str(), 4u), G_VARIANT_TYPE("(u)"));
    guint32 status = 0;
    g_variant_get(result.get(), "(u)", &status);
    if (status != 1 && status != 4)
      throw StateError("EBUSY");
  }

  guint DesktopBus::subscribe(const std::string& sender, const std::string& path, const std::string& interface,
                              const std::string& member, Signal handler) {
    auto callback = new Signal(std::move(handler));
    const auto id = g_dbus_connection_signal_subscribe(
      this->connection, sender.c_str(), interface.c_str(), member.empty() ? nullptr : member.c_str(), path.c_str(),
      nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      [] (GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* member, GVariant* value,
          gpointer data) {
        try {
          (*static_cast<Signal*>(data))(member, value);
        } catch (...) {
        }
      },
      callback,
      [] (gpointer data) {
        delete static_cast<Signal*>(data);
      });
    std::lock_guard lock(this->mutex);
    this->subscriptions.insert(id);
    return id;
  }

  void DesktopBus::unsubscribe(guint id) {
    std::lock_guard lock(this->mutex);
    if (this->subscriptions.erase(id))
      g_dbus_connection_signal_unsubscribe(this->connection, id);
  }

  guint DesktopBus::exportObject(const std::string& path, const std::string& xml, Method handler) {
    GError* error = nullptr;
    auto description = g_dbus_node_info_new_for_xml(xml.c_str(), &error);
    if (!description)
      failure(error);
    static const GDBusInterfaceVTable table = {
      [] (GDBusConnection*, const gchar* sender, const gchar*, const gchar*, const gchar* method, GVariant* parameters,
          GDBusMethodInvocation* invocation, gpointer data) {
        try {
          (*static_cast<Method*>(data))(sender, method, parameters, invocation);
        } catch (...) {
          g_dbus_method_invocation_return_dbus_error(invocation, "org.oro.Error.Failed", "Native operation failed");
        }
      },
      nullptr,
      nullptr,
      {0}};
    auto callback = new Method(std::move(handler));
    const auto id = g_dbus_connection_register_object(
      this->connection, path.c_str(), description->interfaces[0], &table, callback,
      [] (gpointer data) {
        delete static_cast<Method*>(data);
      },
      &error);
    g_dbus_node_info_unref(description);
    if (!id) {
      delete callback;
      failure(error);
    }
    std::lock_guard lock(this->mutex);
    this->objects.insert(id);
    return id;
  }

  void DesktopBus::unexport(guint id) {
    std::lock_guard lock(this->mutex);
    if (this->objects.erase(id))
      g_dbus_connection_unregister_object(this->connection, id);
  }

  const char* DesktopBus::uniqueName() const {
    return g_dbus_connection_get_unique_name(this->connection);
  }

  void DesktopBus::emitSignal(const std::string& path, const std::string& interface, const std::string& member,
                              GVariant* parameters) {
    GError* error = nullptr;
    if (!g_dbus_connection_emit_signal(this->connection, nullptr, path.c_str(), interface.c_str(), member.c_str(),
                                       parameters, &error))
      failure(error);
  }

  void DesktopBus::callNoReply(const std::string& destination, const std::string& path, const std::string& interface,
                               const std::string& member, GVariant* parameters) {
    auto message = g_dbus_message_new_method_call(destination.c_str(), path.c_str(), interface.c_str(), member.c_str());
    if (!message)
      throw StateError("EINVAL");
    g_dbus_message_set_body(message, parameters);
    g_dbus_message_set_flags(message, G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    GError* error = nullptr;
    const auto sent =
      g_dbus_connection_send_message(this->connection, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, nullptr, &error);
    g_object_unref(message);
    if (!sent)
      failure(error);
  }
}  // namespace oro::runtime::native
#endif
