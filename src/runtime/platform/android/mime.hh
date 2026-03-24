#ifndef ORO_RUNTIME_PLATFORM_ANDROID_MIME_H
#define ORO_RUNTIME_PLATFORM_ANDROID_MIME_H

#include "../types.hh"
#include "environment.hh"

namespace oro::runtime::android {
  using MimeTypeMapRef = jobject;

  struct MimeTypeMap {
    MimeTypeMapRef ref;
    JVMEnvironment jvm;
    static const MimeTypeMap* sharedMimeTypeMap ();
    String getMimeTypeFromExtension (const String& extension) const;
  };

  void initializeMimeTypeMap (MimeTypeMapRef ref, JVMEnvironment jvm);
}

#endif
