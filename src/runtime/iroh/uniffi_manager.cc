#include "uniffi_manager.hh"

#if ORO_RUNTIME_HAS_IROH_FFI
namespace oro::runtime::iroh::uniffi {

  Manager::Manager () = default;

  ManagerStatus Manager::status () const {
    Lock lock(mutex);
    ManagerStatus current;
    current.initialized = static_cast<bool>(currentNode);
    current.version = version;
    current.logLevel = currentLogLevel;
    return current;
  }

  Error Manager::init (const NodeOptions& options) {
    Lock lock(mutex);

    if (currentNode) {
      return Error{};
    }

    auto nodeResult = Node::create(options);
    if (!nodeResult) {
      return nodeResult.error;
    }

    auto node = std::make_unique<Node>(std::move(nodeResult.value));
    auto netResult = node->net();
    if (!netResult) {
      return netResult.error;
    }

    currentNet = std::make_unique<Net>(std::move(netResult.value));
    currentNode = std::move(node);
    currentLogLevel = LogLevel::Info;
    return Error{};
  }

  Error Manager::shutdown () {
    Lock lock(mutex);
    if (!currentNode) {
      return Error{};
    }

    currentNet.reset();
    currentNode.reset();
    currentLogLevel = LogLevel::Info;
    return Error{};
  }

  Error Manager::setLogLevel (LogLevel level) {
    auto error = ::oro::runtime::iroh::uniffi::setLogLevel(level);
    if (!error.ok()) {
      return error;
    }

    Lock lock(mutex);
    currentLogLevel = level;
    return error;
  }

  Result<ByteVector> Manager::pathToKey (
    const String& path,
    const std::optional<String>& prefix,
    const std::optional<String>& root
  ) const {
    return ::oro::runtime::iroh::uniffi::pathToKey(path, prefix, root);
  }

  Result<String> Manager::keyToPath (
    const ByteVector& key,
    const std::optional<String>& prefix,
    const std::optional<String>& root
  ) const {
    return ::oro::runtime::iroh::uniffi::keyToPath(key, prefix, root);
  }

  bool Manager::isInitialized () const {
    Lock lock(mutex);
    return static_cast<bool>(currentNode);
  }

  Node* Manager::node () const {
    Lock lock(mutex);
    return currentNode.get();
  }

  Net* Manager::net () const {
    Lock lock(mutex);
    return currentNet.get();
  }
} // namespace oro::runtime::iroh::uniffi
#endif
