#include "tests.hh"
#include "src/runtime/toml.hh"

namespace oro::Tests {
  void toml (Harness& t) {
    t.test("TOML basic values", [] (auto t) {
      const auto document = oro::runtime::TOML::parse(R"TOML(
        title = "TOML Example"
        enabled = true
        answer = 42
        ratio = 1.5
      )TOML");

      const auto& table = document.asTable();

      t.equals(table.at("title").asString(), "TOML Example", "title parsed");
      t.equals(table.at("enabled").asBool(), true, "boolean parsed");
      t.equals(table.at("answer").asInteger(), 42, "integer parsed");
      t.assert(table.at("ratio").asFloat() == 1.5, "float parsed");
    });

    t.test("TOML tables and arrays", [] (auto t) {
      const auto document = oro::runtime::TOML::parse(R"TOML(
        [database]
        server = "192.168.1.1"
        ports = [
          8001,
          8001, # repeated port
          8002
        ]

        trailing = [
          "one",
          "two",
        ]

        inline_trailing = [1, 2,]

        [[clients]]
        name = "alpha"
        [clients.preferences]
        color = "blue"

        [[clients]]
        name = "beta"
      )TOML");

      const auto& root = document.asTable();
      const auto& database = root.at("database").asTable();
      t.equals(database.at("server").asString(), "192.168.1.1", "server parsed");

      const auto& ports = database.at("ports").asArray();
      t.equals(static_cast<int64_t>(ports.size()), 3, "ports array size");
      t.equals(ports[2].asInteger(), 8002, "ports third entry");

      const auto& clients = root.at("clients").asArray();
      t.equals(static_cast<int64_t>(clients.size()), 2, "clients array size");

      const auto& firstClient = clients[0].asTable();
      t.equals(firstClient.at("name").asString(), "alpha", "first client name");
      const auto& preferences = firstClient.at("preferences").asTable();
      t.equals(preferences.at("color").asString(), "blue", "nested table inherits array context");

      const auto& secondClient = clients[1].asTable();
      t.equals(secondClient.at("name").asString(), "beta", "second client name");

      const auto& trailing = database.at("trailing").asArray();
      t.equals(static_cast<int64_t>(trailing.size()), 2, "trailing comma array size");
      t.equals(trailing[1].asString(), "two", "trailing comma retains value");

      const auto& inlineTrailing = database.at("inline_trailing").asArray();
      t.equals(static_cast<int64_t>(inlineTrailing.size()), 2, "inline trailing comma array size");
      t.equals(inlineTrailing[0].asInteger(), 1, "inline trailing first value");
      t.equals(inlineTrailing[1].asInteger(), 2, "inline trailing second value");
    });

    t.test("TOML date and time types", [] (auto t) {
      const auto document = oro::runtime::TOML::parse(R"TOML(
        local_date = 1979-05-27
        local_time = 07:32:00
        local_datetime = 1979-05-27T07:32:00
        offset_datetime = 1979-05-27T07:32:00+01:00
      )TOML");

      const auto& table = document.asTable();

      const auto& date = table.at("local_date").asDate();
      t.equals(static_cast<int64_t>(date.year), 1979, "date year");
      t.equals(static_cast<int64_t>(date.month), 5, "date month");
      t.equals(static_cast<int64_t>(date.day), 27, "date day");

      const auto& time = table.at("local_time").asTime();
      t.equals(static_cast<int64_t>(time.hour), 7, "time hour");
      t.equals(static_cast<int64_t>(time.minute), 32, "time minute");
      t.equals(static_cast<int64_t>(time.second), 0, "time second");

      const auto& localDateTime = table.at("local_datetime").asDateTime();
      t.equals(static_cast<int64_t>(localDateTime.date.year), 1979, "local datetime year");
      t.equals(localDateTime.hasOffset, false, "local datetime has no offset");

      const auto& offsetDateTime = table.at("offset_datetime").asDateTime();
      t.equals(static_cast<int64_t>(offsetDateTime.offsetMinutes), 60, "offset datetime offset");
      t.equals(offsetDateTime.hasOffset, true, "offset datetime has offset");
    });

    t.test("TOML multiline and inline tables", [] (auto t) {
      const auto document = oro::runtime::TOML::parse(R"TOML(
        poem = """Roses are red
Violets are blue"""
        inline = { nested = { value = 1 }, label = "test" }
      )TOML");

      const auto& table = document.asTable();
      t.equals(table.at("poem").asString(), "Roses are red\nViolets are blue", "multiline string preserves newlines");

      const auto& inlineTable = table.at("inline").asTable();
      t.equals(inlineTable.at("label").asString(), "test", "inline table string");
      const auto& nested = inlineTable.at("nested").asTable();
      t.equals(nested.at("value").asInteger(), 1, "inline nested value");
    });

    t.test("TOML inline table immutability", [] (auto t) {
      t.throws([] () {
        oro::runtime::TOML::parse(R"TOML(
          inline = { nested = 1 }
          inline.nested = 2
        )TOML");
      }, "inline tables cannot be extended with dotted keys");
    });

    t.test("TOML dotted keys conflict with later table headers", [] (auto t) {
      t.throws([] () {
        oro::runtime::TOML::parse(R"TOML(
          app.window.width = 800

          [app.window]
          height = 600
        )TOML");
      }, "implicit tables cannot be redefined by headers");
    });
  }
}
