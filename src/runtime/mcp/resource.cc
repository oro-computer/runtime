#include "resource.hh"

#include "../bytes.hh"

namespace oro::runtime::mcp {
  JSON::Object ResourceDescriptor::toJSON() const {
    JSON::Object::Entries entries {
      {"uri", JSON::String(this->uri)},
      {"name", JSON::String(this->name)},
      {"mimeType", JSON::String(this->mimeType)},
      {"description", JSON::String(this->description)},
      {"subscribable", JSON::Boolean(this->subscribable)}
    };

    if (!this->metadata.isNull()) {
      entries.insert({"metadata", this->metadata});
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
      entries.insert({"metadata", descriptor.metadata});
    }

    switch (this->kind) {
      case ResourceContentKind::Text: {
        entries.insert({"type", JSON::String("text")});
        entries.insert({"text", JSON::String(this->text)});
        break;
      }
      case ResourceContentKind::Binary: {
        entries.insert({"type", JSON::String("blob")});
        entries.insert({"blob", JSON::String(bytes::base64::encode(this->bytes))});
        break;
      }
    }

    return JSON::Object(entries);
  }
}
