#include "../iroh.hh"

namespace oro::runtime::iroh {
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

  Library& Library::shared () {
    static Library instance;
    return instance;
  }

  Library::Library () = default;

  Library::~Library () {
    this->shutdown();
  }

  bool Library::init () {
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
  }

  bool Library::shutdown () {
    Lock lock(this->mutex);
    if (!this->initialized) {
      return true;
    }

    auto error = this->manager.shutdown();
    this->initialized = false;
    return error.ok();
  }

  bool Library::isInitialized () const {
    Lock lock(this->mutex);
    return this->initialized;
  }

  Result Library::setLogLevel (LogLevel level) {
    auto error = this->manager.setLogLevel(toUniffiLogLevel(level));
    return makeResult(error);
  }

  String Library::version () const {
    const auto status = this->manager.status();
    return status.version;
  }

  Result Library::pathToKey (
    const String& path,
    const std::optional<String>& prefix,
    const std::optional<String>& root,
    Vector<uint8_t>& out
  ) {
    auto result = this->manager.pathToKey(path, prefix, root);
    if (!result.ok()) {
      return makeResult(result.error);
    }

    out = result.value;
    return Result{};
  }

  Result Library::keyToPath (
    const Vector<uint8_t>& key,
    const std::optional<String>& prefix,
    const std::optional<String>& root,
    String& out
  ) {
    auto result = this->manager.keyToPath(key, prefix, root);
    if (!result.ok()) {
      return makeResult(result.error);
    }

    out = result.value;
    return Result{};
  }
} // namespace oro::runtime::iroh
