#include "../string.hh"

#include "../json.hh"

using oro::runtime::string::replace;

namespace oro::runtime::JSON {
  Type String::valueType = Type::String;

  String::String (const Number& number) {
    this->data = number.str();
  }

  String::String (const String& data) {
    this->data = runtime::String(data.value());
  }

  String::String (const runtime::String& data) {
    this->data = data;
  }

  String::String (const char data) {
    this->data = runtime::String(1, data);
  }

  String::String (const char *data) {
    this->data = data != nullptr ? runtime::String(data) : runtime::String("");
  }

  String::String (const Any& any) {
    this->data = any.str();
  }

  String::String (const Boolean& boolean) {
    this->data = boolean.str();
  }

  String::String (const Error& error) {
    this->data = error.str();
  }

  const runtime::String String::str () const {
    runtime::String out;
    out.reserve(this->data.size() + 2);
    out.push_back('"');

    static const char hex[] = "0123456789ABCDEF";

    for (runtime::String::size_type i = 0; i < this->data.size(); ++i) {
      const unsigned char ch = (unsigned char) this->data[i];
      switch (ch) {
        case '\"':
          out += "\\\"";
          break;
        case '\\':
          out += "\\\\";
          break;
        case '\b':
          out += "\\b";
          break;
        case '\f':
          out += "\\f";
          break;
        case '\n':
          out += "\\n";
          break;
        case '\r':
          out += "\\r";
          break;
        case '\t':
          out += "\\t";
          break;
        default:
          if (ch < 0x20) {
            out += "\\u00";
            out.push_back(hex[(ch >> 4) & 0x0F]);
            out.push_back(hex[ch & 0x0F]);
          } else {
            out.push_back((char) ch);
          }
          break;
      }
    }

    out.push_back('"');
    return out;
  }

  const runtime::String String::value () const {
    return this->data;
  }

  runtime::String::size_type String::size () const {
    return this->data.size();
  }
}
