#include "tests.hh"
#include "src/core/codec.hh"
#include "src/runtime/ipc.hh"
#include "src/runtime/javascript.hh"

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

    t.test("oro::runtime::ipc::Message preserves query value payloads", [](auto t) {
      using oro::runtime::ipc::Message;

      const auto encoded = Message(
        "ipc://test?seq=R1&value=%20line1%0Aline2%0A%20&empty=&raw=a=b%3Dc",
        true
      );

      t.equals(
        encoded.get("value"),
        " line1\nline2\n ",
        "percent-encoded multiline values preserve whitespace"
      );
      t.assert(encoded.has("empty"), "empty query values are retained");
      t.equals(encoded.get("empty"), "", "empty query values decode to empty strings");
      t.equals(encoded.get("raw"), "a=b=c", "raw equals signs are preserved in values");

      const auto raw = Message("ipc://test?value= line1\nline2\n ", true);
      t.equals(
        raw.get("value"),
        " line1\nline2\n ",
        "raw multiline values preserve leading and trailing whitespace"
      );
    });

    t.test("oro::runtime::javascript resolve payload is a safe string literal", [](auto t) {
      const auto source = oro::runtime::javascript::getResolveToRenderProcessJavaScript(
        "R1",
        "0",
        "{\"data\":\"line1\\nline2's\"}"
      );

      t.assert(
        source.find("const value = \"") != oro::runtime::String::npos,
        "resolve payload is emitted as a JSON string literal"
      );
      t.assert(
        source.find("const value = '") == oro::runtime::String::npos,
        "resolve payload is not emitted as an unescaped single-quoted literal"
      );
      t.assert(
        source.find("line1\\\\nline2's") != oro::runtime::String::npos,
        "resolve payload preserves escaped newlines and single quotes"
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
