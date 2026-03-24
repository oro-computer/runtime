#include "tests.hh"

namespace oro::Tests {
  void string (Harness& t) {
    t.test("oro::replace()", [](auto t) {
      t.comment("TODO");
    });

    t.test("oro::tmpl()", [](auto t) {
      t.comment("TODO");
    });

    t.test("oro::trim()", [](auto t) {
      t.comment("TODO");
    });

    t.test("oro::convertStringToWString()", [](auto t) {
      t.comment("TODO");
    });

    t.test("oro::convertWStringToString()", [](auto t) {
      t.comment("TODO");
    });

    t.test("oro::split(const String&, const String&)", [](auto t) {
      const auto items = split("a && b && c && d && e", " && ");

      t.equals(items[0], "a", "items[0] == a");
      t.equals(items[1], "b", "items[1] == b");
      t.equals(items[2], "c", "items[2] == c");
      t.equals(items[3], "d", "items[3] == d");
      t.equals(items[4], "e", "items[4] == e");
    });

    t.test("oro::split(const String&, char)", [](auto t) {
      const auto items = split("a|b|c|d|e", '|');

      t.equals(items[0], "a", "items[0] == a");
      t.equals(items[1], "b", "items[1] == b");
      t.equals(items[2], "c", "items[2] == c");
      t.equals(items[3], "d", "items[3] == d");
      t.equals(items[4], "e", "items[4] == e");
    });

    t.test("oro::join()", [](auto t) {
      const auto joined = join(split("a|b|c|d|e", '|'), '|');
      t.equals(joined, "a|b|c|d|e", "joins vector");
    });

    t.test("oro::parseStringList()", [](auto t) {
      t.comment("TODO");
    });
  }
}
