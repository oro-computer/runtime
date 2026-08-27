#ifndef ORO_RUNTIME_MCP_RESOURCE_H
#define ORO_RUNTIME_MCP_RESOURCE_H

#include "protocol.hh"

#include <optional>

namespace oro::runtime::mcp {
  enum class ResourceContentKind {
    Text,
    Binary
  };

  struct ResourceDescriptor {
    String uri;
    String name;
    String title;
    String description;
    String mimeType;
    bool subscribable = false;
    std::optional<uint64_t> size;
    JSON::Any icons = JSON::Null();
    JSON::Any annotations = JSON::Null();
    JSON::Any metadata = JSON::Null();

    JSON::Object toJSON() const;
  };

  struct ResourceContent {
    ResourceContentKind kind = ResourceContentKind::Text;
    String text;
    Vector<uint8_t> bytes;

    static ResourceContent textContent(const String& value);
    static ResourceContent binaryContent(const Vector<uint8_t>& value);

    JSON::Object toJSON(const ResourceDescriptor& descriptor) const;
  };
}

#endif
