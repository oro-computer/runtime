#ifndef ORO_RUNTIME_PLATFORM_ANDROID_TYPES_H
#define ORO_RUNTIME_PLATFORM_ANDROID_TYPES_H

#include "../types.hh"
#include "native.hh"

namespace oro::runtime::android {
  using namespace oro::runtime::types;

  /**
   * An Android AssetManager NDK type.
   */
  using AssetManager = ::AAssetManager;

  /**
   * An Android AssetManager Asset NDK type.
   */
  using Asset = ::AAsset;

  /**
   * An Android AssetManager AssetDirectory NDK type.
   */
  using AssetDirectory = ::AAssetDir;

  /**
   * An opaque `Activity` instance.
   */
  using Activity = ::jobject;

  /**
   * An opaque `Application` instance.
   */
  using Application = ::jobject;

  /**
   * A container that holds Android OS build information.
   */
  struct BuildInformation {
    String brand;
    String device;
    String fingerprint;
    String hardware;
    String model;
    String manufacturer;
    String product;
    int sdk = 0;
  };
}
#endif
