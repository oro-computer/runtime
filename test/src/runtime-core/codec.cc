#include "tests.hh"
#include "src/core/codec.hh"

namespace oro::Tests {
  void codec (Harness& t) {
    t.test("oro::encodeURIComponent", [](auto t) {
      const auto encoded = oro::encodeURIComponent(
        "a % encoded string with foo@bar.com, $100, & #tag"
      );

      t.assert(encodeURIComponent("").size() == 0, "Empty input returns empty string");
      t.assert(encoded.size() != 0, "encoded has size");
      t.equals(
        encoded,
        "a%20%25%20encoded%20string%20with%20foo%40bar%2Ecom%2C%20%24100%2C%20%26%20%23tag",
        "encoded value is correct"
      );
    });

    t.test("oro::decodeURIComponent", [](auto t) {
      const auto decoded = oro::decodeURIComponent(
        "a%20%25%20encoded%20string%20with%20foo%40bar%2Ecom%2C%20%24100%2C%20%26%20%23tag"
      );

      t.assert(decodeURIComponent("").size() == 0, "Empty input returns empty string");
      t.assert(decoded.size() != 0, "decoded has size");
      t.equals(
        decoded,
        "a % encoded string with foo@bar.com, $100, & #tag",
        "decoded value is correct"
      );
    });

    t.test("oro::encodeHexString", [](auto t) {
      t.equals(
        oro::encodeHexString("hello world"),
        "68656C6C6F20776F726C64",
        "encodes 'hello world'"
      );

      t.equals(
        oro::encodeHexString("#F"),
        "2346",
        "encodes '\u0023\u0046'"
      );

      t.equals(
        oro::encodeHexString("{\"foo\":\"bar\",\"biz\":{\"baz\":\"boop\"}}"),
        "7B22666F6F223A22626172222C2262697A223A7B2262617A223A22626F6F70227D7D",
        "encodes '{\"foo\":\"bar\",\"biz\":{\"baz\":\"boop\"}}'"
      );
    });

    t.test("oro::decodeHexString", [](auto t) {
      t.equals(
        oro::decodeHexString("68656C6C6F20776F726C64"),
        "hello world",
        "decodes '68656C6C6F20776F726C64'"
      );

      t.equals(
        oro::decodeHexString("2346"),
        "#F",
        "decodes '2346'"
      );

      t.equals(
        oro::decodeHexString("7B22666F6F223A22626172222C2262697A223A7B2262617A223A22626F6F70227D7D"),
        "{\"foo\":\"bar\",\"biz\":{\"baz\":\"boop\"}}",
        "decodes '7B22666F6F223A22626172222C2262697A223A7B2262617A223A22626F6F70227D7D'"
      );
    });

    t.test("oro::decodeUTF8", [](auto t) {
      t.comment("skip: TODO(@jwerle)");
    });

    t.test("oro::toBytes", [](auto t) {
      t.comment("skip: TODO(@jwerle)");
    });
  }
}
