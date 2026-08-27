#include "tests.hh"
#include "src/runtime/ini.hh"

namespace oro::Tests {
  void ini (Harness& t) {
    t.test("oro::INI::parse", [] (auto t) {
      auto simple = runtime::INI::parse(R"INI(
        key = "value"
      )INI");

      t.equals(simple["key"], "value", "simple[key] == value");

      auto sections = runtime::INI::parse(R"INI(
        [section-1]
        key = "value"

        [section-2]
        key = "value"

        [section-3]
        key = "value"
      )INI");

      t.equals(sections["section-1_key"], "value", "sections[section-1_key] == value");
      t.equals(sections["section-2_key"], "value", "sections[section-2_key] == value");
      t.equals(sections["section-3_key"], "value", "sections[section-3_key] == value");

      auto subsections = runtime::INI::parse(R"INI(
        [section-1]
        key = "value"
        [.subsection]
        key = "value"

        [section-2]
        key = "value"
        [.subsection]
        key = "value"

        [section-3]
        key = "value"
        [.subsection]
        key = "value"
      )INI");

      t.equals(subsections["section-1_key"], "value", "subsections[section-1_key] == value");
      t.equals(subsections["section-2_key"], "value", "subsections[section-2_key] == value");
      t.equals(subsections["section-3_key"], "value", "subsections[section-3_key] == value");

      t.equals(subsections["section-1_subsection_key"], "value", "subsections[section-1_subsection_key] == value");
      t.equals(subsections["section-2_subsection_key"], "value", "subsections[section-2_subsection_key] == value");
      t.equals(subsections["section-3_subsection_key"], "value", "subsections[section-3_subsection_key] == value");

      auto arrays = runtime::INI::parse(R"INI(
        [numbers]
        array[] = 1
        array[] = 2
        array[] = 3

        [strings]
        array[] = "hello"
        array[] = world
      )INI");

      t.equals(arrays["numbers_array"], "1 2 3", "arrays[numbers_array] == 1 2 3");
      t.equals(arrays["strings_array"], "hello world", "arrays[strings_array] == hello world");

      auto dotsyntax = runtime::INI::parse(R"INI(
        [a.b.c.d.e.f]
        g = "value"

        [a.b.c.d.e]
        [.f.g.h]
        i = "value"
      )INI", ".");

      t.equals(dotsyntax["a.b.c.d.e.f.g"], "value", "dotsyntax[a.b.c.d.e.f.g] == value");
      t.equals(dotsyntax["a.b.c.d.e.f.g.h.i"], "value", "dotsyntax[a.b.c.d.e.f.g.h.i] == value");

      auto escapedSection = runtime::INI::parse(R"INI(
        [section\]]
        key = value
      )INI");

      t.equals(escapedSection["section]_key"], "value", "escapedSection[section]_key] == value");

      auto singleQuotes = runtime::INI::parse(R"INI(
        message = 'It\'s fine'
        path = 'C:\\path\\'
      )INI");

      t.equals(singleQuotes["message"], "It's fine", "singleQuotes[message] == It's fine");
      t.equals(singleQuotes["path"], "C:\\path\\", "singleQuotes[path] preserves escapes");
    });
  }
}
