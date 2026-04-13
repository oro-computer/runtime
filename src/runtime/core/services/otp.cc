#include "otp.hh"

#include "../../app.hh"
#include "../../crypto.hh"
#include "../../debug.hh"
#include "../../runtime.hh"
#include "../../string.hh"
#include "../../url.hh"
#if ORO_RUNTIME_PLATFORM_IOS
#include "../../otp/ios.hh"
#endif

#include <cctype>
#include <limits>

#if ORO_RUNTIME_PLATFORM_ANDROID
#include "../../platform/android/environment.hh"
#endif

using oro::runtime::app::App;
using oro::runtime::string::split;
using oro::runtime::string::trim;
using oro::runtime::string::toLowerCase;

namespace oro::runtime::core::services {
  namespace {
    constexpr uint64_t DEFAULT_TIMEOUT_MS = 60000;
    constexpr const char* kOTPTransportSMS = "sms";

    inline String normalizeHost (const url::URL& url) {
      auto host = toLowerCase(url.hostname);
      if (host.size() == 0) {
        host = toLowerCase(url.origin);
      }

      if (url.port.size() > 0) {
        host += ":";
        host += url.port;
      }

      return host;
    }

    inline bool equalsInsensitive (
      const String& haystack,
      size_t offset,
      const String& needle
    ) {
      if (offset + needle.size() > haystack.size()) {
        return false;
      }

    for (size_t index = 0; index < needle.size(); ++index) {
      const auto hay = static_cast<unsigned char>(haystack[offset + index]);
      const auto ned = static_cast<unsigned char>(needle[index]);

      if (std::tolower(static_cast<int>(hay)) != std::tolower(static_cast<int>(ned))) {
        return false;
      }
    }

      return true;
    }

    String normalizeErrorType (const String& type) {
      const auto lowered = toLowerCase(trim(type));

      struct Mapping { const char* key; const char* value; };
      static constexpr Mapping mappings[] = {
        { "aborterror", "AbortError" },
        { "dataerror", "DataError" },
        { "invalidstateerror", "InvalidStateError" },
        { "notallowederror", "NotAllowedError" },
        { "notsupportederror", "NotSupportedError" },
        { "securityerror", "SecurityError" },
        { "timeouterror", "TimeoutError" }
      };

      for (const auto& mapping : mappings) {
        if (lowered == mapping.key) {
          return mapping.value;
        }
      }

      return "UnknownError";
    }
  }

  OTP::OTP (const Options& options)
    : core::Service(options)
  {}

  OTP::~OTP () {
    Vector<Request> pending;

    {
      Lock lock(this->mutex);
      for (auto& entry : this->requests) {
        pending.push_back(entry.second);
      }
      this->requests.clear();
    }

    for (auto& request : pending) {
      if (request.timerId != 0) {
        this->services.timers.clearTimeout(request.timerId);
      }
      this->stopPlatformRequest(request);
    }
  }

  bool OTP::start () {
    return core::Service::start();
  }

  bool OTP::stop () {
    Vector<Request> pending;

    {
      Lock lock(this->mutex);
      for (auto& entry : this->requests) {
        pending.push_back(entry.second);
      }
      this->requests.clear();
    }

    for (auto& request : pending) {
      if (request.timerId != 0) {
        this->services.timers.clearTimeout(request.timerId);
      }

      this->stopPlatformRequest(request);

      if (request.callback) {
        JSON::Object json = JSON::Object::Entries {
          {"err", JSON::Object::Entries {
            {"type", "AbortError"},
            {"message", "Web OTP request cancelled"}
          }}
        };
        request.callback(request.seq, json, QueuedResponse {});
      }
    }

    return core::Service::stop();
  }

  void OTP::get (
    const String& seq,
    const Map<String, String>& options,
    const Callback callback
  ) {
    if (!this->enabled.load()) {
      JSON::Object json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "NotSupportedError"},
          {"message", "Web OTP is not enabled on this platform"}
        }}
      };

      if (callback) {
        callback(seq, json, QueuedResponse {});
      }

      return;
    }

    auto runtime = this->context.getRuntime();
    if (!runtime || !runtime->hasPermission("otp")) {
      JSON::Object json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "NotAllowedError"},
          {"message", "Web OTP is disabled by runtime configuration"}
        }}
      };

      if (callback) {
        callback(seq, json, QueuedResponse {});
      }

      return;
    }

    auto originIt = options.find("origin");
    if (originIt == options.end() || trim(originIt->second).empty()) {
      JSON::Object json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "InvalidStateError"},
          {"message", "An origin must be provided"}
        }}
      };

      if (callback) {
        callback(seq, json, QueuedResponse {});
      }

      return;
    }

    Request request;
    request.id = crypto::rand64();
    request.seq = seq;
    request.callback = callback;
    request.origin = trim(originIt->second);

    // transports
    auto transportIt = options.find("transport");
    if (transportIt != options.end()) {
      const auto parts = split(transportIt->second, ',');
      for (auto part : parts) {
        part = trim(part);
        if (part.size() == 0) {
          continue;
        }
        request.transports.push_back(toLowerCase(part));
      }
    }

    if (!this->hasTransportSMS(request.transports)) {
      JSON::Object json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "NotSupportedError"},
          {"message", "Only the 'sms' transport is supported"}
        }}
      };

      if (callback) {
        callback(seq, json, QueuedResponse {});
      }

      return;
    }

    if (request.transports.empty()) {
      request.transports.push_back(kOTPTransportSMS);
    }

    // timeout
    request.timeoutMs = DEFAULT_TIMEOUT_MS;
    auto timeoutIt = options.find("timeout");
    if (timeoutIt != options.end()) {
      try {
        const auto value = std::stoll(trim(timeoutIt->second));
        if (value > 0) {
          request.timeoutMs = static_cast<uint64_t>(value);
        }
      } catch (...) {}
    }

    auto indexIt = options.find("index");
    if (indexIt != options.end()) {
      try {
        request.index = std::stoi(indexIt->second);
      } catch (...) {}
    }

    auto hintIt = options.find("hint");
    if (hintIt != options.end()) {
      request.hint = trim(hintIt->second);
    }

    url::URL parsed(request.origin, true);

    if (parsed.hostname.size() == 0 && parsed.origin.size() == 0) {
      JSON::Object json = JSON::Object::Entries {
        {"err", JSON::Object::Entries {
          {"type", "TypeError"},
          {"message", "Failed to parse origin"}
        }}
      };

      if (callback) {
        callback(seq, json, QueuedResponse {});
      }

      return;
    }

    request.host = normalizeHost(parsed);
    request.anchor = String("@") + request.host;

    Request* stored = nullptr;

    {
      Lock lock(this->mutex);
      const auto [it, inserted] = this->requests.emplace(request.id, std::move(request));
      stored = &(it->second);
      if (!inserted) {
        stored->callback = callback;
      }
    }

    if (!this->startPlatformRequest(*stored)) {
      this->reject(stored->id, "NotSupportedError", "OTP retrieval is unavailable on this platform");
      return;
    }

    if (stored->timeoutMs > 0) {
      const auto timeoutId = this->services.timers.setTimeout(
        stored->timeoutMs,
        [this, id = stored->id]() {
          this->handleTimeout(id);
        }
      );

      {
        Lock lock(this->mutex);
        auto it = this->requests.find(stored->id);
        if (it != this->requests.end()) {
          it->second.timerId = timeoutId;
        } else {
          this->services.timers.clearTimeout(timeoutId);
        }
      }
    }
  }

  void OTP::handleMessage (uint64_t id, const String& message) {
    Request request;
    {
      Lock lock(this->mutex);
      auto it = this->requests.find(id);
      if (it == this->requests.end()) {
        return;
      }
      request = it->second;
    }

    String code;
    if (!this->parseMessageForCode(request, message, code)) {
      this->reject(id, "InvalidStateError", "Failed to parse one-time password from SMS");
      return;
    }

    this->resolve(id, code);
  }

  void OTP::handleCode (uint64_t id, const String& code) {
    if (trim(code).empty()) {
      this->reject(id, "InvalidStateError", "Received empty OTP code");
      return;
    }

    this->resolve(id, trim(code));
  }

  void OTP::handleTimeout (uint64_t id) {
    this->reject(id, "TimeoutError", "Timed out waiting for the one-time password");
  }

  void OTP::handleError (
    uint64_t id,
    const String& type,
    const String& message
  ) {
    const auto errorType = type.size() > 0 ? type : String("AbortError");
    const auto errorMessage = message.size() > 0
      ? message
      : String("Web OTP request failed");

    this->reject(id, errorType, errorMessage);
  }

  bool OTP::resolve (uint64_t id, const String& code) {
    Request request;
    if (!this->popRequest(id, request)) {
      return false;
    }

    if (request.timerId != 0) {
      this->services.timers.clearTimeout(request.timerId);
    }

    this->stopPlatformRequest(request);

    if (!request.callback) {
      return false;
    }

    JSON::Array transportsJson;
    for (const auto& transport : request.transports) {
      transportsJson.push(transport);
    }

    JSON::Object data = JSON::Object::Entries {
      {"type", "otp"},
      {"code", code}
    };

    if (transportsJson.size() > 0) {
      data.set("transports", transportsJson);
    }

    if (request.origin.size() > 0) {
      data.set("origin", request.origin);
    }

    JSON::Object json = JSON::Object::Entries {
      {"data", data}
    };

    request.callback(request.seq, json, QueuedResponse {});
    return true;
  }

  bool OTP::reject (
    uint64_t id,
    const String& type,
    const String& message
  ) {
    Request request;
    if (!this->popRequest(id, request)) {
      return false;
    }

    if (request.timerId != 0) {
      this->services.timers.clearTimeout(request.timerId);
    }

    this->stopPlatformRequest(request);

    if (!request.callback) {
      return false;
    }

    const auto normalizedType = normalizeErrorType(type);
    const auto normalizedMessage = message.size() > 0
      ? message
      : String("Web OTP request failed");

    JSON::Object json = JSON::Object::Entries {
      {"err", JSON::Object::Entries {
        {"type", normalizedType},
        {"message", normalizedMessage}
      }}
    };

    request.callback(request.seq, json, QueuedResponse {});
    return true;
  }

  bool OTP::popRequest (uint64_t id, Request& request) {
    Lock lock(this->mutex);
    auto it = this->requests.find(id);
    if (it == this->requests.end()) {
      return false;
    }

    request = std::move(it->second);
    this->requests.erase(it);
    return true;
  }

  bool OTP::parseMessageForCode (
    const Request& request,
    const String& message,
    String& code
  ) const {
    if (request.host.empty()) {
      return false;
    }

    const auto& anchor = request.anchor;
    size_t anchorPosition = message.find('@');

    while (anchorPosition != String::npos) {
      if (equalsInsensitive(message, anchorPosition, anchor)) {
        break;
      }
      anchorPosition = message.find('@', anchorPosition + 1);
    }

    if (anchorPosition == String::npos) {
      return false;
    }

    const auto hashPosition = message.find('#', anchorPosition + anchor.size());
    if (hashPosition == String::npos) {
      return false;
    }

    size_t start = hashPosition + 1;
    while (start < message.size() && std::isspace(static_cast<unsigned char>(message[start]))) {
      start++;
    }

    size_t end = start;
    while (end < message.size() && !std::isspace(static_cast<unsigned char>(message[end]))) {
      end++;
    }

    code = trim(message.substr(start, end - start));
    return code.size() > 0;
  }

  bool OTP::hasTransportSMS (const Vector<String>& transports) const {
    if (transports.empty()) {
      return true;
    }

    for (const auto& transport : transports) {
      if (toLowerCase(trim(transport)) == kOTPTransportSMS) {
        return true;
      }
    }

    return false;
  }

  bool OTP::startPlatformRequest (Request& request) {
  #if ORO_RUNTIME_PLATFORM_ANDROID
    return this->startAndroidRequest(request);
  #elif ORO_RUNTIME_PLATFORM_APPLE
    return this->startAppleRequest(request);
  #else
    (void) request;
    return false;
  #endif
  }

  void OTP::stopPlatformRequest (const Request& request) {
  #if ORO_RUNTIME_PLATFORM_ANDROID
    this->stopAndroidRequest(request);
  #elif ORO_RUNTIME_PLATFORM_APPLE
    this->stopAppleRequest(request);
  #else
    (void) request;
  #endif
  }

#if ORO_RUNTIME_PLATFORM_ANDROID
  bool OTP::startAndroidRequest (Request& request) {
    auto app = App::sharedApplication();
    if (!app) {
      return false;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      return false;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      return false;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/otp/OTPManager");
    if (!cls) {
      attachment.printException();
      return false;
    }

    jmethodID startMethod = attachment.env->GetStaticMethodID(
      cls,
      "start",
      "(Landroid/content/Context;JLjava/lang/String;I)Z"
    );

    if (!startMethod) {
      attachment.env->DeleteLocalRef(cls);
      attachment.printException();
      return false;
    }

    const auto hostString = attachment.env->NewStringUTF(request.host.c_str());
    const jint timeout = request.timeoutMs > std::numeric_limits<jint>::max()
      ? std::numeric_limits<jint>::max()
      : static_cast<jint>(request.timeoutMs);

    const jboolean started = attachment.env->CallStaticBooleanMethod(
      cls,
      startMethod,
      android.activity,
      static_cast<jlong>(request.id),
      hostString,
      timeout
    );

    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
    }

    attachment.env->DeleteLocalRef(hostString);
    attachment.env->DeleteLocalRef(cls);

    return started == JNI_TRUE;
  }

  void OTP::stopAndroidRequest (const Request& request) {
    auto app = App::sharedApplication();
    if (!app) {
      return;
    }

    auto& android = app->runtime.android;
    if (!android.jvm || !android.activity) {
      return;
    }

    android::JNIEnvironmentAttachment attachment(android.jvm);
    if (!attachment.env) {
      return;
    }

    jclass cls = attachment.env->FindClass("oro/runtime/otp/OTPManager");
    if (!cls) {
      attachment.printException();
      return;
    }

    jmethodID finishMethod = attachment.env->GetStaticMethodID(
      cls,
      "finish",
      "(Landroid/content/Context;J)V"
    );

    if (!finishMethod) {
      attachment.env->DeleteLocalRef(cls);
      attachment.printException();
      return;
    }

    attachment.env->CallStaticVoidMethod(
      cls,
      finishMethod,
      android.activity,
      static_cast<jlong>(request.id)
    );

    if (attachment.hasException()) {
      attachment.printException();
      attachment.env->ExceptionClear();
    }

    attachment.env->DeleteLocalRef(cls);
  }
#endif

#if ORO_RUNTIME_PLATFORM_APPLE
  bool OTP::startAppleRequest (Request& request) {
  #if ORO_RUNTIME_PLATFORM_IOS
    return startIOSOTPRequest(request.id, *this, request.timeoutMs, request.host);
  #else
    (void) request;
    return false;
  #endif
  }

  void OTP::stopAppleRequest (const Request& request) {
  #if ORO_RUNTIME_PLATFORM_IOS
    stopIOSOTPRequest(request.id);
  #else
    (void) request;
  #endif
  }
#endif
}
