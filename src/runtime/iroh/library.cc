#include "../iroh.hh"

namespace oro::runtime::iroh {
#if ORO_RUNTIME_HAS_IROH_FFI
  namespace {
    inline Result makeResult (const uniffi::Error& error) {
      Result result;
      result.code = error.code;
      result.message = error.message;
      return result;
    }

    inline uniffi::LogLevel toUniffiLogLevel (LogLevel level) {
      return static_cast<uniffi::LogLevel>(static_cast<int>(level));
    }
  } // namespace
#endif

  Library& Library::shared () {
    static Library instance;
    return instance;
  }

  Library::Library () = default;

  Library::~Library () {
    this->shutdown();
  }

  bool Library::init () {
#if ORO_RUNTIME_HAS_IROH_FFI
    Lock lock(this->mutex);
    if (this->initialized) {
      return true;
    }

    auto error = this->manager.init({});
    if (!error.ok()) {
      return false;
    }

    this->initialized = true;
    return true;
#else
    return false;
#endif
  }

  bool Library::shutdown () {
    Lock lock(this->mutex);
#if ORO_RUNTIME_HAS_IROH_FFI
    if (!this->initialized) {
      return true;
    }

    auto error = this->manager.shutdown();
    this->initialized = false;
    return error.ok();
#else
    this->initialized = false;
    return true;
#endif
  }

  bool Library::isInitialized () const {
    Lock lock(this->mutex);
    return this->initialized;
  }

  Result Library::setLogLevel (LogLevel level) {
#if ORO_RUNTIME_HAS_IROH_FFI
    auto error = this->manager.setLogLevel(toUniffiLogLevel(level));
    return makeResult(error);
#else
    return Result {1, "Iroh FFI unavailable"};
#endif
  }

  String Library::version () const {
#if ORO_RUNTIME_HAS_IROH_FFI
    const auto status = this->manager.status();
    return status.version;
#else
    return {};
#endif
  }

  Result Library::pathToKey (
    const String& path,
    const std::optional<String>& prefix,
    const std::optional<String>& root,
    Vector<uint8_t>& out
  ) {
#if ORO_RUNTIME_HAS_IROH_FFI
    auto result = this->manager.pathToKey(path, prefix, root);
    if (!result.ok()) {
      return makeResult(result.error);
    }

    out = result.value;
    return Result{};
#else
    return Result {1, "Iroh FFI unavailable"};
#endif
  }

  Result Library::keyToPath (
    const Vector<uint8_t>& key,
    const std::optional<String>& prefix,
    const std::optional<String>& root,
    String& out
  ) {
#if ORO_RUNTIME_HAS_IROH_FFI
    auto result = this->manager.keyToPath(key, prefix, root);
    if (!result.ok()) {
      return makeResult(result.error);
    }

    out = result.value;
    return Result{};
#else
    return Result {1, "Iroh FFI unavailable"};
#endif
  }
} // namespace oro::runtime::iroh
