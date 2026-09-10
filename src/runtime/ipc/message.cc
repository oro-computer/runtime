#include "../bytes.hh"
#include "../url.hh"

#include "../ipc.hh"

using oro::runtime::url::decodeURIComponent;

namespace oro::runtime::ipc {
  namespace {
    inline bool isHexDigit (char c) {
      return (c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F');
    }

    inline uint8_t hexValue (char c) {
      if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
      if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
      return static_cast<uint8_t>(c - 'A' + 10);
    }

    bool containsPercentEscapes (const String& value) {
      if (value.size() < 3) {
        return false;
      }

      for (size_t i = 0; i + 2 < value.size(); ++i) {
        if (
          value[i] == '%' &&
          isHexDigit(value[i + 1]) &&
          isHexDigit(value[i + 2])
        ) {
          return true;
        }
      }

      return false;
    }

    // Decode percent escapes without treating '+' as a space. This is used as a
    // compatibility pass for double-encoded query parameters.
    String decodePercentEscapes (const String& input) {
      if (input.empty() || input.find('%') == String::npos) {
        return input;
      }

      String out;
      out.reserve(input.size());

      for (size_t i = 0; i < input.size(); ++i) {
        if (
          input[i] == '%' &&
          i + 2 < input.size() &&
          isHexDigit(input[i + 1]) &&
          isHexDigit(input[i + 2])
        ) {
          const auto high = hexValue(input[i + 1]);
          const auto low = hexValue(input[i + 2]);
          out.push_back(static_cast<char>((high << 4) | low));
          i += 2;
        } else {
          out.push_back(input[i]);
        }
      }

      return out;
    }
  }

  Message::Message (const String& source, bool decodeValues)
    : uri(source, decodeValues) {
    this->seq = this->get("seq");
    this->name = this->uri.hostname;
    this->value = this->get("value");
    this->href = this->uri.href();

    if (this->uri.searchParams.contains("index")) {
      try {
        this->index = this->uri.searchParams
          .get("index")
          .as<JSON::Number>()
          .value();
      } catch (const Exception& e) {
        debug(
          "oro::runtime::ipc::Message: Warning: received non-integer index: %s: %s",
          this->uri.str().c_str(),
          e.what()
        );
      }
    }
  }

  Message::Message (const String& source)
    : Message(source, false) {
    this->href = this->uri.href();
  }

  Message::Message (const Message& message)
    : value(message.value),
      index(message.index),
      name(message.name),
      seq(message.seq),
      uri(message.uri),
      isHTTP(message.isHTTP),
      cancel(message.cancel),
      buffer(message.buffer),
      client(message.client) {
    this->href = this->uri.href();
  }

  Message::Message (Message&& msg) {
    this->buffer = std::move(msg.buffer);
    this->client = std::move(msg.client);
    this->index = msg.index;
    this->value = std::move(msg.value);
    this->name = std::move(msg.name);
    this->uri = std::move(msg.uri);
    this->seq = std::move(msg.seq);
    this->isHTTP = msg.isHTTP;
    this->cancel = std::move(msg.cancel);
    this->href = this->uri.href();

    msg.name = "";
    msg.index = -1;
    msg.value = "";
    msg.uri = URL();
    msg.seq = "";
    msg.isHTTP = false;
    msg.buffer.reset();
    msg.cancel = nullptr;
  }

  Message& Message::operator = (const Message& msg) {
    this->decodedValues.clear();
    this->buffer = msg.buffer;
    this->client = msg.client;
    this->index = msg.index;
    this->value = msg.value;
    this->name = msg.name;
    this->uri = msg.uri;
    this->seq = msg.seq;
    this->isHTTP = msg.isHTTP;
    this->cancel = msg.cancel;
    this->href = this->uri.href();
    return *this;
  }

  Message& Message::operator = (Message&& msg) {
    this->decodedValues.clear();
    this->buffer = std::move(msg.buffer);
    this->client = std::move(msg.client);
    this->index = msg.index;
    this->value = std::move(msg.value);
    this->name = std::move(msg.name);
    this->uri = std::move(msg.uri);
    this->seq = std::move(msg.seq);
    this->isHTTP = msg.isHTTP;
    this->cancel = std::move(msg.cancel);
    this->href = this->uri.href();

    msg.name = "";
    msg.index = -1;
    msg.value = "";
    msg.uri = URL();
    msg.seq = "";
    msg.isHTTP = false;
    msg.buffer.reset();
    msg.cancel = nullptr;
    return *this;
  }

  bool Message::has (const String& key) const {
    return this->uri.searchParams.contains(key);
  }

  bool Message::contains (const String& key) const {
    return this->uri.searchParams.contains(key);
  }

  const String& Message::at (const String& key) const {
    const auto& value = this->uri.searchParams.at(key).data;
    const auto entry = this->decodedValues.find(key);
    if (entry != this->decodedValues.end()) {
      return entry->second;
    }

    // C extension accessors return pointers owned by the message. Decode once
    // and retain each value so another lookup cannot invalidate those pointers.
    return this->decodedValues.emplace(key, decodeURIComponent(value)).first->second;
  }

  const String Message::get (const String& key) const {
    return this->get(key, "");
  }

  const String Message::get (const String& key, const String &fallback) const {
    if (key == "value" && this->value.size() > 0) {
      return this->value;
    }

    if (!this->contains(key)) {
      return fallback;
    }

    auto decoded = decodeURIComponent(this->uri.searchParams.get(key).data);
    if (containsPercentEscapes(decoded)) {
      decoded = decodePercentEscapes(decoded);
    }

    return decoded;
  }

  const Map<String, String> Message::dump () const {
    return this->map();
  }

  const String Message::str () const {
    return this->uri.str();
  }

  const Map<String, String> Message::map () const {
    return this->uri.searchParams.map();
  }

  const JSON::Object Message::json () const {
    return JSON::Object::Entries {
      {"name", this->name},
      {"value", this->value},
      {"index", this->index},
      {"href", this->href},
      {"seq", this->seq},
      {"data", this->map()}
    };
  }
}
