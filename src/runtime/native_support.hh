#ifndef ORO_RUNTIME_NATIVE_SUPPORT_H
#define ORO_RUNTIME_NATIVE_SUPPORT_H

#include <oro/platform.h>

#include <array>
#include <string>
#include <string_view>

namespace oro::runtime::native {
  enum class Platform { Android, IOS, MacOS, Linux, Windows, Unknown };
  enum class Implementation { Implemented, Missing, Restricted };

  struct FeatureSupport {
    std::string_view feature;
    Implementation implementation;
    std::string_view backend;
    std::string_view reason;
  };

  inline constexpr std::array<std::string_view, 16> features = {
      "authentication", "credentials",    "foreground",    "foreground.continuous",
      "deferred",       "push",           "launcher",      "extension",
      "back",           "gestures",       "notifications", "records",
      "coordination",   "secure-storage", "background", "workflows"};

  inline constexpr Platform currentPlatform () {
#if ORO_RUNTIME_PLATFORM_ANDROID
    return Platform::Android;
#elif ORO_RUNTIME_PLATFORM_IOS
    return Platform::IOS;
#elif ORO_RUNTIME_PLATFORM_MACOS
    return Platform::MacOS;
#elif ORO_RUNTIME_PLATFORM_LINUX
    return Platform::Linux;
#elif ORO_RUNTIME_PLATFORM_WINDOWS
    return Platform::Windows;
#else
    return Platform::Unknown;
#endif
  }

  inline constexpr std::string_view platformName (Platform platform) {
    switch (platform) {
    case Platform::Android:
      return "android";
    case Platform::IOS:
      return "ios";
    case Platform::MacOS:
      return "macos";
    case Platform::Linux:
      return "linux";
    case Platform::Windows:
      return "windows";
    default:
      return "unknown";
    }
  }

  inline constexpr bool knownFeature (std::string_view feature) {
    for (const auto candidate : features) {
      if (candidate == feature) {
        return true;
      }
    }
    return false;
  }

  /** Source/backend inventory only. Permission, hardware, configuration and OS admission are separate probes. */
  inline constexpr FeatureSupport describe (Platform platform, std::string_view feature) {
    if (!knownFeature(feature)) {
      return {feature, Implementation::Missing, "", "unknown-feature"};
    }
    if (platform == Platform::Unknown) {
      return {feature, Implementation::Missing, "", "unknown-platform"};
    }
    if (feature == "secure-storage") {
      switch (platform) {
      case Platform::Android:
        return {feature, Implementation::Implemented, "android-keystore", ""};
      case Platform::IOS:
      case Platform::MacOS:
        return {feature, Implementation::Implemented, "apple-keychain", ""};
      case Platform::Linux:
        return {feature, Implementation::Implemented, "secret-service", ""};
      case Platform::Windows:
        return {feature, Implementation::Implemented, "credential-manager", ""};
      default:
        break;
      }
    }
    if (feature == "workflows") {
      if (platform == Platform::Android)
        return {feature, Implementation::Implemented, "bundled-read-notify", ""};
      return {feature, Implementation::Missing, "", "workflow-backend-unimplemented"};
    }
    if (platform == Platform::Android) {
      if (feature == "push") {
        return {feature, Implementation::Implemented, "unifiedpush", ""};
      }
      if (feature == "authentication") {
        return {feature, Implementation::Implemented, "android-keystore", ""};
      }
      if (feature == "credentials") {
        return {feature, Implementation::Implemented, "android-credential-manager", ""};
      }
      if (feature == "foreground.continuous") {
        return {feature, Implementation::Implemented, "android-special-use", ""};
      }
      if (feature == "foreground") {
        return {feature, Implementation::Implemented, "android-data-sync", ""};
      }
      if (feature == "deferred" || feature == "background") {
        return {feature, Implementation::Implemented, "android-job-scheduler", ""};
      }
      return {feature, Implementation::Implemented, "android-native", ""};
    }
    if (platform == Platform::Linux) {
      if (feature == "authentication") return {feature, Implementation::Implemented, "pkcs11-protected-keys", ""};
      if (feature == "background") return {feature, Implementation::Restricted, "", "android-share-provider-cleanup"};
      if (feature == "credentials") return {feature, Implementation::Implemented, "secret-service-selection", ""};
      if (feature == "back") return {feature, Implementation::Implemented, "gtk-back-navigation", ""};
      if (feature == "foreground" || feature == "foreground.continuous") {
        return {feature, Implementation::Implemented, "linux-native-execution", ""};
      }
      if (feature == "notifications") return {feature, Implementation::Implemented, "freedesktop-notifications", ""};
      if (feature == "extension") return {feature, Implementation::Implemented, "compiled-native-registry", ""};
      if (feature == "push") return {feature, Implementation::Implemented, "unifiedpush-dbus", ""};
      if (feature == "deferred") return {feature, Implementation::Implemented, "native-durable-scheduler", ""};
      if (feature == "launcher") return {feature, Implementation::Implemented, "freedesktop-desktop-actions", ""};
    }
    if (feature == "records" || feature == "coordination") {
      return {feature, Implementation::Implemented, "sqlite-os-secret-store-v2", ""};
    }
    if (platform == Platform::IOS && feature == "foreground.continuous") {
      return {feature, Implementation::Restricted, "", "no-unbounded-background-execution"};
    }
    if (feature == "gestures") {
      return {feature, Implementation::Restricted, "", "android-system-gesture-api"};
    }
    return {feature, Implementation::Missing, "", "backend-not-implemented"};
  }
} // namespace oro::runtime::native
#endif
