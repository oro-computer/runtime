#include "resource.hh"

#include "../bytes.hh"

namespace oro::runtime::mcp {
  JSON::Object ResourceDescriptor::toJSON() const {
    JSON::Object::Entries entries {
      {"uri", JSON::String(this->uri)},
      {"name", JSON::String(this->name)},
      {"mimeType", JSON::String(this->mimeType)},
      {"description", JSON::String(this->description)}
    };

    if (!this->title.empty()) {
      entries.insert({"title", JSON::String(this->title)});
    }

    if (this->size.has_value()) {
      entries.insert({"size", JSON::Number(*this->size)});
    }

    if (!this->icons.isNull()) {
      entries.insert({"icons", this->icons});
    }

    if (!this->annotations.isNull()) {
      entries.insert({"annotations", this->annotations});
    }

    if (!this->metadata.isNull()) {
      entries.insert({"_meta", this->metadata});
    }

    return JSON::Object(entries);
  }

  ResourceContent ResourceContent::textContent(const String& value) {
    ResourceContent content;
    content.kind = ResourceContentKind::Text;
    content.text = value;
    return content;
  }

  ResourceContent ResourceContent::binaryContent(const Vector<uint8_t>& value) {
    ResourceContent content;
    content.kind = ResourceContentKind::Binary;
    content.bytes = value;
    return content;
  }

  JSON::Object ResourceContent::toJSON(const ResourceDescriptor& descriptor) const {
    JSON::Object::Entries entries {
      {"uri", JSON::String(descriptor.uri)},
      {"mimeType", JSON::String(descriptor.mimeType)}
    };

    if (!descriptor.metadata.isNull()) {
      entries.insert({"_meta", descriptor.metadata});
    }

    switch (this->kind) {
      case ResourceContentKind::Text: {
        entries.insert({"text", JSON::String(this->text)});
        break;
      }
      case ResourceContentKind::Binary: {
        entries.insert({"blob", JSON::String(bytes::base64::encode(this->bytes))});
        break;
      }
    }

    return JSON::Object(entries);
  }
}
