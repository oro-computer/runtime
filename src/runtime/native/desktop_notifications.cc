#include "desktop_notifications.hh"

#if ORO_RUNTIME_PLATFORM_LINUX
#include <algorithm>
#include <cstring>

namespace oro::runtime::native {
  namespace {
    constexpr auto destination = "org.freedesktop.Notifications";
    constexpr auto path = "/org/freedesktop/Notifications";

    bool text (const std::string& value, size_t maximum) {
      return value.size() <= maximum && value.find('\0') == std::string::npos &&
             g_utf8_validate(value.c_str(), value.size(), nullptr);
    }

    std::string identity (const std::string& id, const std::string& tag) {
      if (id.empty() || !text(id, 256) || !text(tag, 256))
        throw StateError("EINVAL");
      return std::to_string(id.size()) + ":" + id + tag;
    }

    std::string escaped (const std::string& value) {
      auto escaped = g_markup_escape_text(value.c_str(), value.size());
      const std::string result(escaped);
      g_free(escaped);
      return result;
    }
  }  // namespace

  struct DesktopNotifications::State {
    mutable std::mutex mutex;
    std::mutex control;
    std::string applicationId;
    std::string applicationName;
    std::string owner;
    Response response;
    struct Entry {
      uint32_t serverId;
      std::string id;
      std::string tag;
      std::vector<std::string> actions;
      bool ongoing;
    };
    std::map<std::string, Entry> entries;
    bool closed = false;
    gulong closedHandler = 0;

    void changeOwner (const std::string& next) {
      Response callback;
      std::vector<Entry> lost;
      {
        std::lock_guard lock(this->mutex);
        if (this->closed || this->owner == next) return;
        this->owner = next;
        for (const auto& [key, entry] : this->entries) if (entry.ongoing) lost.push_back(entry);
        this->entries.clear();
        callback = this->response;
      }
      if (callback) for (const auto& entry : lost) callback(entry.id, entry.tag, "unavailable");
    }

    void signal (const std::string& name, GVariant* parameters) {
      if (name == "NameOwnerChanged") {
        const char* service = nullptr;
        const char* previous = nullptr;
        const char* next = nullptr;
        g_variant_get(parameters, "(&s&s&s)", &service, &previous, &next);
        if (service != std::string(destination))
          return;
        this->changeOwner(next);
        return;
      }
      uint32_t id = 0;
      uint32_t closedReason = 0;
      std::string action;
      if (name == "ActionInvoked" && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(us)"))) {
        const char* value = nullptr;
        g_variant_get(parameters, "(u&s)", &id, &value);
        action = value;
      } else if (name == "NotificationClosed" && g_variant_is_of_type(parameters, G_VARIANT_TYPE("(uu)"))) {
        g_variant_get(parameters, "(uu)", &id, &closedReason);
        action = "dismiss";
      } else
        return;
      std::optional<Entry> found;
      Response callback;
      {
        std::lock_guard lock(this->mutex);
        if (this->closed)
          return;
        for (auto entry = this->entries.begin(); entry != this->entries.end(); ++entry) {
          if (entry->second.serverId != id)
            continue;
          if (name == "ActionInvoked" && std::find(entry->second.actions.begin(), entry->second.actions.end(),
                                                   action) == entry->second.actions.end())
            return;
          found = entry->second;
          if (name == "NotificationClosed")
            this->entries.erase(entry);
          callback = this->response;
          break;
        }
      }
      if (found && callback) {
        if (name == "NotificationClosed") {
          if (closedReason == 3) return;
          if (closedReason != 2) {
            if (!found->ongoing) return;
            action = "unavailable";
          }
        }
        callback(found->id, found->tag, action);
      }
    }
  };

  DesktopNotifications::DesktopNotifications(std::shared_ptr<DesktopBus> bus, const std::string& applicationId,
                                             const std::string& name, Response callback)
      : state(std::make_shared<State>()), bus(std::move(bus)) {
    if (!this->bus || applicationId.empty() || !text(applicationId, 200) ||
        applicationId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") != std::string::npos ||
        !text(name, 256))
      throw StateError("EINVAL");
    this->state->applicationId = applicationId;
    this->state->applicationName = name;
    this->state->response = std::move(callback);
    const std::weak_ptr<State> weak = this->state;
    this->state->closedHandler = this->bus->onClosed([weak] {
      if (const auto state = weak.lock()) state->changeOwner("");
    });
    const auto receive = [weak] (const std::string& name, GVariant* value) {
      if (auto state = weak.lock())
        state->signal(name, value);
    };
    this->subscriptions.push_back(this->bus->subscribe(destination, path, destination, "", receive));
    this->subscriptions.push_back(this->bus->subscribe("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                       "org.freedesktop.DBus", "NameOwnerChanged", receive));
  }

  DesktopNotifications::~DesktopNotifications() {
    this->bus->removeClosedHandler(this->state->closedHandler);
    {
      std::lock_guard lock(this->state->mutex);
      this->state->closed = true;
      this->state->response = nullptr;
    }
    for (const auto id : this->subscriptions)
      this->bus->unsubscribe(id);
  }

  std::vector<std::string> DesktopNotifications::capabilities() {
    const auto result =
      this->bus->call(destination, path, destination, "GetCapabilities", nullptr, G_VARIANT_TYPE("(as)"));
    GVariantIter* iterator = nullptr;
    g_variant_get(result.get(), "(as)", &iterator);
    std::vector<std::string> values;
    const char* value = nullptr;
    while (g_variant_iter_next(iterator, "&s", &value)) {
      if (values.size() >= 128 || std::strlen(value) > 128) {
        g_variant_iter_free(iterator);
        throw StateError("E2BIG");
      }
      values.emplace_back(value);
    }
    g_variant_iter_free(iterator);
    return values;
  }

  uint32_t DesktopNotifications::submit(const DesktopNotification& notification) {
    std::lock_guard transaction(this->state->control);
    const auto key = identity(notification.id, notification.tag);
    if (!text(notification.title, 256) || !text(notification.body, 4096) || !text(notification.category, 128) ||
        !text(notification.group, 128) || notification.actions.size() > 4 || notification.urgency > 2 ||
        !text(notification.icon, 128) ||
        notification.icon.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
          std::string::npos)
      throw StateError("EINVAL");
    const auto capabilities = this->capabilities();
    const auto supports = [&] (const std::string& value) {
      return std::find(capabilities.begin(), capabilities.end(), value) != capabilities.end();
    };
    if (!notification.actions.empty() && !supports("actions"))
      throw StateError("ENOSYS");
    const auto owner = this->bus->owner(destination);
    if (owner.empty())
      throw StateError("ENOTREADY");
    this->state->changeOwner(owner);
    uint32_t replacement = 0;
    {
      std::lock_guard lock(this->state->mutex);
      if (this->state->closed)
        throw StateError("ESHUTDOWN");
      if (const auto found = this->state->entries.find(key); found != this->state->entries.end())
        replacement = found->second.serverId;
      if (!replacement && this->state->entries.size() >= 128)
        throw StateError("ENOSPC");
    }
    GVariantBuilder actions;
    GVariantBuilder hints;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    std::vector<std::string> permitted;
    for (const auto& [id, label] : notification.actions) {
      if (id.empty() || !text(id, 128) || label.empty() || !text(label, 128) ||
          std::find(permitted.begin(), permitted.end(), id) != permitted.end()) {
        g_variant_builder_clear(&actions);
        g_variant_builder_clear(&hints);
        throw StateError("EINVAL");
      }
      permitted.push_back(id);
      g_variant_builder_add(&actions, "s", id.c_str());
      g_variant_builder_add(&actions, "s", label.c_str());
    }
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(this->state->applicationId.c_str()));
    g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(notification.urgency));
    const bool silent = notification.silent || notification.groupSummary || (replacement && !notification.renotify);
    g_variant_builder_add(&hints, "{sv}", "suppress-sound", g_variant_new_boolean(silent));
    if (!silent && supports("sound"))
      g_variant_builder_add(&hints, "{sv}", "sound-name", g_variant_new_string("message-new-instant"));
    if (notification.ongoing)
      g_variant_builder_add(&hints, "{sv}", "resident", g_variant_new_boolean(true));
    if (!notification.category.empty())
      g_variant_builder_add(&hints, "{sv}", "category", g_variant_new_string(notification.category.c_str()));
    const auto body = supports("body-markup") ? escaped(notification.body) : notification.body;
    const auto result = this->bus->call(
      owner, path, destination, "Notify",
      g_variant_new("(susssasa{sv}i)", this->state->applicationName.c_str(), replacement, notification.icon.c_str(),
                    notification.title.c_str(), body.c_str(), &actions, &hints, notification.ongoing ? 0 : -1),
      G_VARIANT_TYPE("(u)"));
    uint32_t id = 0;
    g_variant_get(result.get(), "(u)", &id);
    if (!id || this->bus->owner(destination) != owner)
      throw StateError("EUNKNOWN");
    {
      std::lock_guard lock(this->state->mutex);
      if (this->state->closed || this->state->owner != owner)
        throw StateError("EUNKNOWN");
      this->state->entries[key] = {id, notification.id, notification.tag, permitted, notification.ongoing};
    }
    return id;
  }

  void DesktopNotifications::close(const std::string& id, const std::string& tag) {
    std::lock_guard transaction(this->state->control);
    const auto key = identity(id, tag);
    uint32_t value = 0;
    std::string owner;
    {
      std::lock_guard lock(this->state->mutex);
      const auto found = this->state->entries.find(key);
      if (found == this->state->entries.end())
        return;
      value = found->second.serverId;
      owner = this->state->owner;
    }
    const auto currentOwner = this->bus->owner(destination);
    if (currentOwner != owner) {
      this->state->changeOwner(currentOwner);
      return;
    }
    this->bus->call(owner, path, destination, "CloseNotification", g_variant_new("(u)", value), G_VARIANT_TYPE("()"));
    std::lock_guard lock(this->state->mutex);
    const auto found = this->state->entries.find(key);
    if (found != this->state->entries.end() && found->second.serverId == value)
      this->state->entries.erase(found);
  }

  std::vector<std::pair<std::string, std::string>> DesktopNotifications::list() const {
    std::lock_guard lock(this->state->mutex);
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& [key, entry] : this->state->entries)
      result.emplace_back(entry.id, entry.tag);
    return result;
  }
}  // namespace oro::runtime::native
#endif
