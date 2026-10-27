#include "src/runtime/native/desktop_notifications.hh"

#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace oro::runtime::native;
constexpr auto destination = "org.freedesktop.Notifications";
constexpr auto path = "/org/freedesktop/Notifications";
constexpr auto xml = R"XML(<node><interface name="org.freedesktop.Notifications">
<method name="GetCapabilities"><arg type="as" direction="out"/></method>
<method name="Notify"><arg type="s" direction="in"/><arg type="u" direction="in"/>
<arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/>
<arg type="as" direction="in"/><arg type="a{sv}" direction="in"/><arg type="i" direction="in"/>
<arg type="u" direction="out"/></method>
<method name="CloseNotification"><arg type="u" direction="in"/></method>
<method name="TestSnapshot"><arg type="u" direction="out"/><arg type="b" direction="out"/><arg type="s" direction="out"/></method>
<method name="TestAction"><arg type="u" direction="in"/><arg type="s" direction="in"/></method>
<method name="TestStop"/>
<method name="TestNoActions"/>
<signal name="ActionInvoked"><arg type="u"/><arg type="s"/></signal>
<signal name="NotificationClosed"><arg type="u"/><arg type="u"/></signal>
</interface></node>)XML";

template <class F> void pump (F done) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!done()) {
    while (g_main_context_iteration(nullptr, false)) {}
    assert(std::chrono::steady_clock::now() < until);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

int main (int argc, char** argv) {
  assert(argc == 2);
  auto bus = std::make_shared<DesktopBus>();
  if (std::string(argv[1]) == "server") {
    auto loop = g_main_loop_new(nullptr, false);
    uint32_t last = 0;
    bool silent = false;
    bool actions = true;
    std::string body;
    bus->exportObject(path, xml, [&](const auto&, const auto& method, GVariant* parameters, GDBusMethodInvocation* reply) {
      if (method == "GetCapabilities") {
        const gchar* names[] = {"body", "body-markup", "sound", "persistence", actions ? "actions" : nullptr, nullptr};
        g_dbus_method_invocation_return_value(reply, g_variant_new("(^as)", names));
      } else if (method == "Notify") {
        const char* app = nullptr; const char* icon = nullptr; const char* title = nullptr; const char* text = nullptr;
        GVariant* buttons = nullptr; GVariant* hints = nullptr; int32_t expires = 0; uint32_t replace = 0;
        g_variant_get(parameters, "(&su&s&s&s@as@a{sv}i)", &app, &replace, &icon, &title, &text, &buttons, &hints, &expires);
        body = text;
        gboolean value = false;
        assert(g_variant_lookup(hints, "suppress-sound", "b", &value));
        const char* desktopEntry = nullptr;
        assert(g_variant_lookup(hints, "desktop-entry", "&s", &desktopEntry));
        assert(std::string(desktopEntry) == "public-native-test" || std::string(desktopEntry) == "public-runtime");
        silent = value;
        last = replace ? replace : last + 1;
        g_variant_unref(buttons); g_variant_unref(hints);
        g_dbus_method_invocation_return_value(reply, g_variant_new("(u)", last));
      } else if (method == "CloseNotification") {
        uint32_t id = 0; g_variant_get(parameters, "(u)", &id);
        bus->emitSignal(path, destination, "NotificationClosed", g_variant_new("(uu)", id, 3u));
        g_dbus_method_invocation_return_value(reply, g_variant_new("()"));
      } else if (method == "TestSnapshot") {
        g_dbus_method_invocation_return_value(reply, g_variant_new("(ubs)", last, silent, body.c_str()));
      } else if (method == "TestAction") {
        uint32_t id = 0; const char* action = nullptr; g_variant_get(parameters, "(u&s)", &id, &action);
        bus->emitSignal(path, destination, "ActionInvoked", g_variant_new("(us)", id, action));
        g_dbus_method_invocation_return_value(reply, g_variant_new("()"));
      } else if (method == "TestNoActions") {
        actions = false; g_dbus_method_invocation_return_value(reply, g_variant_new("()"));
      } else if (method == "TestStop") {
        g_dbus_method_invocation_return_value(reply, g_variant_new("()"));
        g_main_loop_quit(loop);
      }
    });
    bus->own(destination);
    std::cout << "notification-mock-ready" << std::endl;
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return 0;
  }
  int responses = 0;
  int lostControls = 0;
  DesktopNotifications notifications(bus, "public-native-test", "Public test", [&](const auto& id, const auto& tag, const auto& action) {
    if (id == "ongoing") {
      assert(tag == "scope" && action == "unavailable"); lostControls++; return;
    }
    assert(id == "public" && tag == "scope" && action == "stop"); responses++;
  });
  const auto invoke = [&](const char* method, GVariant* arguments = nullptr, const GVariantType* type = G_VARIANT_TYPE("()")) {
    return bus->call(destination, path, destination, method, arguments, type);
  };
  DesktopNotification value;
  value.id = "public"; value.tag = "scope"; value.title = "Public status"; value.body = "<public>&";
  value.actions = {{"stop", "Stop"}};
  const auto first = notifications.submit(value);
  auto inspect = [&] (bool expected) {
    const auto response = invoke("TestSnapshot", nullptr, G_VARIANT_TYPE("(ubs)"));
    uint32_t id = 0; gboolean silent = false; const char* body = nullptr;
    g_variant_get(response.get(), "(ub&s)", &id, &silent, &body);
    assert(id == first && bool(silent) == expected && std::string(body) == "&lt;public&gt;&amp;");
  };
  inspect(false);
  assert(notifications.submit(value) == first); inspect(true);
  value.renotify = true; assert(notifications.submit(value) == first); inspect(false);
  value.groupSummary = true; assert(notifications.submit(value) == first); inspect(true);
  invoke("TestAction", g_variant_new("(us)", first, "unauthorized"));
  while (g_main_context_iteration(nullptr, false)) {}
  assert(responses == 0);
  invoke("TestAction", g_variant_new("(us)", first, "stop"));
  pump([&] { return responses == 1; });
  auto attacker = std::make_shared<DesktopBus>();
  attacker->emitSignal(path, destination, "ActionInvoked", g_variant_new("(us)", first, "stop"));
  for (int i = 0; i < 10; i++) { while (g_main_context_iteration(nullptr, false)) {} std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
  assert(responses == 1);
  assert(notifications.list().size() == 1);
  notifications.close("public", "scope");
  assert(notifications.list().empty());
  DesktopNotification ongoing;
  ongoing.id = "ongoing"; ongoing.tag = "scope"; ongoing.title = "Public monitoring"; ongoing.ongoing = true;
  notifications.submit(ongoing);
  invoke("TestNoActions");
  try { notifications.submit(value); assert(false); } catch (const StateError& error) { assert(error.code == "ENOSYS"); }
  invoke("TestStop");
  pump([&] { return lostControls == 1; });
  assert(notifications.list().empty());
  std::cout << "desktop-notification-dbus-ownership-sound-and-actions-passed\n";
}
