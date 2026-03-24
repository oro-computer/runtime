#ifndef ORO_RUNTIME_IROH_UNIFFI_MANAGER_H
#define ORO_RUNTIME_IROH_UNIFFI_MANAGER_H

#include "../platform/types.hh"

#include "uniffi.hh"

#include <memory>
#include <optional>

#if !defined(ORO_RUNTIME_IROH_VERSION)
#  if defined(ORO_RUNTIME_IROH_VERSION)
#    define ORO_RUNTIME_IROH_VERSION ORO_RUNTIME_IROH_VERSION
#  else
#    define ORO_RUNTIME_IROH_VERSION ""
#  endif
#endif

#define ORO_RUNTIME_IROH_STRINGIFY_IMPL(value) #value
#define ORO_RUNTIME_IROH_STRINGIFY(value) ORO_RUNTIME_IROH_STRINGIFY_IMPL(value)

namespace oro::runtime::iroh::uniffi {
  using types::Lock;
  using types::Mutex;
  using types::String;
  using ByteVector = types::Vector<uint8_t>;

  namespace detail {
    inline String sanitizeVersionLiteral (String value) {
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
      }
      return value;
    }

    inline String defaultVersionLiteral () {
      static const String literal = sanitizeVersionLiteral(
        String(ORO_RUNTIME_IROH_STRINGIFY(ORO_RUNTIME_IROH_VERSION))
      );
      return literal;
    }
  } // namespace detail

  struct ManagerStatus {
    bool initialized = false;
    String version = detail::defaultVersionLiteral();
    LogLevel logLevel = LogLevel::Info;
  };

  class Manager {
    public:
      Manager ();

      ManagerStatus status () const;

      Error init (const NodeOptions& options = NodeOptions {});
      Error shutdown ();

      Error setLogLevel (LogLevel level);

      Result<ByteVector> pathToKey (
        const String& path,
        const std::optional<String>& prefix = std::nullopt,
        const std::optional<String>& root = std::nullopt
      ) const;

      Result<String> keyToPath (
        const ByteVector& key,
        const std::optional<String>& prefix = std::nullopt,
        const std::optional<String>& root = std::nullopt
      ) const;

      bool isInitialized () const;

      Node* node () const;
      Net* net () const;

    private:
      mutable Mutex mutex;
      std::unique_ptr<Node> currentNode;
      std::unique_ptr<Net> currentNet;
      LogLevel currentLogLevel = LogLevel::Info;
      String version = detail::defaultVersionLiteral();
  };
} // namespace oro::runtime::iroh::uniffi

#undef ORO_RUNTIME_IROH_STRINGIFY
#undef ORO_RUNTIME_IROH_STRINGIFY_IMPL

#endif // ORO_RUNTIME_IROH_UNIFFI_MANAGER_H
