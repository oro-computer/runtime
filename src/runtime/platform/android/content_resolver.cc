#include "../../filesystem.hh"
#include "../../string.hh"
#include "../../url.hh"

#include "content_resolver.hh"
#include <cerrno>
#include <fcntl.h>
#include <limits>

using oro::runtime::string::split;

namespace oro::runtime::android {
  namespace {
    struct LocalReferences {
      JNIEnv* env;
      bool active;
      explicit LocalReferences (JNIEnv* env) : env(env), active(env->PushLocalFrame(32) == JNI_OK) {}
      ~LocalReferences () {
        if (active) env->PopLocalFrame(nullptr);
      }
    };

    int takeProviderError (JNIEnv* env) {
      if (!env->ExceptionCheck()) return 0;
      const auto exception = env->ExceptionOccurred();
      env->ExceptionClear();
      int code = EIO;
      for (const auto& entry : {
        std::pair { "java/lang/SecurityException", EACCES },
        std::pair { "java/io/FileNotFoundException", ENOENT },
        std::pair { "java/lang/IllegalArgumentException", EINVAL },
        std::pair { "java/lang/UnsupportedOperationException", ENOTSUP }
      }) {
        const auto type = env->FindClass(entry.first);
        if (type) {
          const bool matches = env->IsInstanceOf(exception, type);
          env->DeleteLocalRef(type);
          if (matches) {
            code = entry.second;
            break;
          }
        } else {
          env->ExceptionClear();
        }
      }
      env->DeleteLocalRef(exception);
      return code;
    }
  }

  int ContentResolver::getOpenMode (int flags, String& mode) {
    mode.clear();
    const int allowed = O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC | O_LARGEFILE;
    if (flags & ~allowed) return ENOTSUP;
    const auto access = flags & O_ACCMODE;
    if (access == O_RDONLY) {
      if (flags & (O_TRUNC | O_APPEND)) return EINVAL;
      mode = "r";
    } else if (access == O_WRONLY || access == O_RDWR) {
      if (flags & O_TRUNC) {
        mode = access == O_RDWR ? "rwt" : "wt";
      } else if (access == O_WRONLY && (flags & O_APPEND)) {
        mode = "wa";
      } else {
        mode = "rw";
      }
    } else {
      return EINVAL;
    }
    return 0;
  }

  int ContentResolver::getReadRange (
    size_t offset, size_t size, off_t start, off_t length, int64_t& position, size_t& count
  ) {
    count = size;
    position = -1;
    if (start < 0 || length < -1) return EINVAL;
    if (offset == std::numeric_limits<size_t>::max()) {
      // A bounded asset section needs an explicit virtual position. Whole-file
      // descriptors retain ordinary sequential reads, including provider pipes.
      return start == 0 && length == -1 ? 0 : ENOTSUP;
    }
    if (offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() - start)) return EOVERFLOW;
    position = static_cast<int64_t>(offset) + start;
    if (length >= 0) {
      count = offset >= static_cast<uint64_t>(length) ? 0 :
        std::min<size_t>(size, static_cast<uint64_t>(length) - offset);
    }
    return 0;
  }

  ContentResolver::ContentResolver (const ContentResolver& resolver) {
    this->application = resolver.application;
    this->jvm = resolver.jvm;
  }

  ContentResolver::ContentResolver (ContentResolver&& resolver) {
    this->application = resolver.application;
    this->jvm = resolver.jvm;
    resolver.jvm = nullptr;
    resolver.application = nullptr;
  }

  ContentResolver& ContentResolver::operator = (const ContentResolver& resolver) {
    this->application = resolver.application;
    this->jvm = resolver.jvm;
    return *this;
  }

  ContentResolver& ContentResolver::operator = (ContentResolver&& resolver) {
    this->application = resolver.application;
    this->jvm = resolver.jvm;
    resolver.jvm = nullptr;
    resolver.application = nullptr;
    return *this;
  }

  bool ContentResolver::isDocumentURI (const String& uri) {
    if (this->jvm && this->application) {
      const auto attachment = JNIEnvironmentAttachment(this->jvm);
      const auto platform = (jobject) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        this->application,
        "getAppPlatform",
        "()Loro/runtime/app/AppPlatform;"
      );

      return CallClassMethodFromAndroidEnvironment(
        attachment.env,
        Boolean,
        platform,
        "isDocumentURI",
        "(Ljava/lang/String;)Z",
        StringWrap(attachment.env, uri).j_str()
      );
    }

    return false;
  }

  bool ContentResolver::isContentURI (const String& uri) {
    const auto url = URL(uri);
    return url.scheme == "content" || url.scheme == "android.resource";
  }

  bool ContentResolver::isExternalStorageDocumentURI (const String& uri) {
    const auto url = URL(uri);
    return url.hostname == "com.android.externalstorage.documents";
  }

  bool ContentResolver::isDownloadsDocumentURI (const String& uri) {
    const auto url = URL(uri);
    return url.hostname == "com.android.providers.downloads.documents";
  }

  bool ContentResolver::isMediaDocumentURI (const String& uri) {
    const auto url = URL(uri);
    return url.hostname == "com.android.providers.media.documents";
  }

  bool ContentResolver::isPhotosURI (const String& uri) {
    const auto url = URL(uri);
    return url.hostname == "com.android.providers.media.documents";
  }

  String ContentResolver::getContentMimeType (const String& uri) {
    if (!this->jvm || !this->application) return "";
    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    const auto env = attachment.env;
    if (!env) return "";
    LocalReferences references(env);
    if (!references.active) {
      env->ExceptionClear();
      return "";
    }
    const auto platform = CallObjectClassMethodFromAndroidEnvironment(
      env, this->application, "getAppPlatform", "()Loro/runtime/app/AppPlatform;"
    );
    if (takeProviderError(env) || !platform) return "";
    const auto value = static_cast<jstring>(CallObjectClassMethodFromAndroidEnvironment(
      env, platform, "getContentMimeType", "(Ljava/lang/String;)Ljava/lang/String;", StringWrap(env, uri).j_str()
    ));
    if (takeProviderError(env) || !value) return "";
    return StringWrap(env, value).str();
  }

  String ContentResolver::getPathnameFromURI (const String& uri) {
    const auto url = URL(uri);

    if (url.scheme == "file") {
      return url.pathname;
    }

    if (this->jvm && this->isDocumentURI(uri)) {
      const auto attachment = JNIEnvironmentAttachment(this->jvm);
      const auto platform = (jobject) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        this->application,
        "getAppPlatform",
        "()Loro/runtime/app/AppPlatform;"
      );

      const auto documentIDString = (jstring) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        platform,
        "getDocumentID",
        "(Ljava/lang/String;)Ljava/lang/String;",
        StringWrap(attachment.env, uri).j_str()
      );

      const auto documentID = StringWrap(attachment.env, documentIDString).str();

      if (this->isExternalStorageDocumentURI(uri)) {
        const auto externalStorage = filesystem::Resource::getExternalAndroidStorageDirectory();
        const auto parts = split(documentID, ":");
        const auto type = parts[0];

        if (type == "primary" && parts.size() > 1) {
          return externalStorage.string() + "/" + parts[1];
        }
      }

      if (this->isDownloadsDocumentURI(uri)) {
        const auto contentURIString = (jstring) CallObjectClassMethodFromAndroidEnvironment(
          attachment.env,
          platform,
          "getContentURI",
          "(Ljava/lang/String;J)Ljava/lang/String;",
          attachment.env->NewStringUTF("content://downloads/public_downloads"),
          std::stol(documentID)
        );

        const auto contentURI = StringWrap(attachment.env, contentURIString).str();

        return this->getPathnameFromContentURI(contentURI);
      }

      if (this->isMediaDocumentURI(uri)) {
        const auto parts = split(documentID, ":");
        const auto type = parts[0];

        if (parts.size() > 1) {
          const auto id = parts[1];
          const auto contentURI = this->getExternalContentURIForType(type);

          if (contentURI.size() > 0) {
            return this->getPathnameFromContentURI(contentURI, id);
          }
        }
      }
    }

    if (url.scheme == "content") {
      if (this->isPhotosURI(uri)) {
        const auto parts = split(url.pathname, '/');
        if (parts.size() > 0) {
          return parts[parts.size() - 1];
        }
      }

      return this->getPathnameFromContentURI(uri);
    }


    return "";
  }

  String ContentResolver::getPathnameFromContentURI (
    const String& uri,
    const String& id
  ) {
    if (this->jvm && this->application) {
      const auto attachment = JNIEnvironmentAttachment(this->jvm);
      const auto platform = (jobject) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        this->application,
        "getAppPlatform",
        "()Loro/runtime/app/AppPlatform;"
      );

      const auto result = (jstring) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        platform,
        "getPathnameFromContentURI",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        StringWrap(attachment.env, uri).j_str(),
        android::StringWrap(attachment.env, id).j_str()
      );

      return StringWrap(attachment.env, result).str();
    }

    return "";
  }

  String ContentResolver::getExternalContentURIForType (const String& type) {
    if (this->jvm && this->application) {
      const auto attachment = JNIEnvironmentAttachment(this->jvm);
      const auto platform = (jobject) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        this->application,
        "getAppPlatform",
        "()Loro/runtime/app/AppPlatform;"
      );

      const auto result = (jstring) CallObjectClassMethodFromAndroidEnvironment(
        attachment.env,
        platform,
        "getExternalContentURIForType",
        "(Ljava/lang/String;)Ljava/lang/String;",
        android::StringWrap(attachment.env, type).j_str()
      );

      return StringWrap(attachment.env, result).str();
    }

    return "";
  }

  Vector<String> ContentResolver::getPathnameEntriesFromContentURI (
    const String& uri
  ) {
    Vector<String> entries;

    if (!this->jvm || !this->application) {
      return entries;
    }

    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    const auto platform = (jobject) CallObjectClassMethodFromAndroidEnvironment(
      attachment.env,
      this->application,
      "getAppPlatform",
      "()Loro/runtime/app/AppPlatform;"
    );

    const auto results = (jobjectArray) CallObjectClassMethodFromAndroidEnvironment(
      attachment.env,
      platform,
      "getPathnameEntriesFromContentURI",
      "(Ljava/lang/String;)[Ljava/lang/String;",
      StringWrap(attachment.env, uri).j_str()
    );

    const auto length = attachment.env->GetArrayLength(results);

    for (int i = 0; i < length; ++i) {
      const auto result = (jstring) attachment.env->GetObjectArrayElement(results, i);
      const auto pathname = attachment.env->GetStringUTFChars(result, nullptr);
      if (pathname != nullptr) {
        entries.push_back(pathname);
        attachment.env->ReleaseStringUTFChars(result, pathname);
      }
    }

    return entries;
  }

  bool ContentResolver::hasAccess (const String& uri, int* error) {
    const auto descriptor = this->openFileDescriptor(uri, nullptr, nullptr, "r", error);
    if (!descriptor) return false;
    const bool closed = this->closeFileDescriptor(descriptor);
    if (!closed && error) *error = EIO;
    return closed;
  }

  ContentResolver::FileDescriptor ContentResolver::openFileDescriptor (
    const String& uri,
    off_t* offset,
    off_t* length,
    const String& mode,
    int* error
  ) {
    if (error) *error = 0;
    if (offset) *offset = 0;
    if (length) *length = -1;
    if (!this->jvm || !this->application ||
        (uri.find('\0') != String::npos || (!uri.starts_with("content:") && !uri.starts_with("android.resource:")))) {
      if (error) *error = EINVAL;
      return nullptr;
    }
    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    const auto env = attachment.env;
    if (!env) {
      if (error) *error = EIO;
      return nullptr;
    }
    LocalReferences references(env);
    if (!references.active) {
      env->ExceptionClear();
      if (error) *error = ENOMEM;
      return nullptr;
    }
    const auto platform = CallObjectClassMethodFromAndroidEnvironment(
      env, this->application, "getAppPlatform", "()Loro/runtime/app/AppPlatform;"
    );
    auto code = takeProviderError(env);
    if (!platform || code) {
      if (error) *error = code ? code : EIO;
      return nullptr;
    }
    const auto uriValue = StringWrap(env, uri).j_str();
    if (takeProviderError(env) || !uriValue) {
      if (error) *error = ENOMEM;
      return nullptr;
    }
    const auto modeValue = android::StringWrap(env, mode).j_str();
    if (takeProviderError(env) || !modeValue) {
      if (error) *error = ENOMEM;
      return nullptr;
    }
    const auto fileDescriptor = CallObjectClassMethodFromAndroidEnvironment(
      env, platform, "openContentResolverFileDescriptor",
      "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;", uriValue, modeValue
    );
    code = takeProviderError(env);
    if (!fileDescriptor || code) {
      if (error) *error = code ? code : EIO;
      return nullptr;
    }
    const auto start = CallClassMethodFromAndroidEnvironment(env, Long, fileDescriptor, "getStartOffset", "()J");
    code = takeProviderError(env);
    jlong declared = -1;
    if (!code) {
      declared = CallClassMethodFromAndroidEnvironment(env, Long, fileDescriptor, "getDeclaredLength", "()J");
      code = takeProviderError(env);
    }
    if (code || start < 0 || declared < -1 || (declared == -1 && start != 0)) {
      CallVoidClassMethodFromAndroidEnvironment(env, fileDescriptor, "close", "()V");
      takeProviderError(env);
      if (error) *error = code ? code : EINVAL;
      return nullptr;
    }
    const auto owned = env->NewGlobalRef(fileDescriptor);
    if (takeProviderError(env) || !owned) {
      CallVoidClassMethodFromAndroidEnvironment(env, fileDescriptor, "close", "()V");
      takeProviderError(env);
      if (error) *error = ENOMEM;
      return nullptr;
    }
    if (offset) *offset = static_cast<off_t>(start);
    if (length) *length = static_cast<off_t>(declared);
    return owned;
  }

  bool ContentResolver::closeFileDescriptor (ContentResolver::FileDescriptor fileDescriptor) {
    if (!this->jvm || !fileDescriptor) return false;
    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    if (!attachment.env) return false;
    CallVoidClassMethodFromAndroidEnvironment(attachment.env, fileDescriptor, "close", "()V");
    const bool closed = takeProviderError(attachment.env) == 0;
    attachment.env->DeleteGlobalRef(fileDescriptor);
    return closed;
  }

  off_t ContentResolver::getFileDescriptorLength (FileDescriptor fileDescriptor) {
    if (!this->jvm || !fileDescriptor) return -1;
    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    if (!attachment.env) return -1;
    const auto length = CallClassMethodFromAndroidEnvironment(
      attachment.env, Long, fileDescriptor, "getLength", "()J"
    );
    return takeProviderError(attachment.env) ? -1 : static_cast<off_t>(length);
  }

  off_t ContentResolver::getFileDescriptorOffset (FileDescriptor fileDescriptor) {
    if (!this->jvm || !fileDescriptor) return -1;
    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    if (!attachment.env) return -1;
    const auto offset = CallClassMethodFromAndroidEnvironment(
      attachment.env, Long, fileDescriptor, "getStartOffset", "()J"
    );
    return takeProviderError(attachment.env) ? -1 : static_cast<off_t>(offset);
  }

  int ContentResolver::getFileDescriptorFD (FileDescriptor fileDescriptor) {
    if (!this->jvm || !fileDescriptor) return -1;
    const auto attachment = JNIEnvironmentAttachment(this->jvm);
    if (!attachment.env) return -1;
    LocalReferences references(attachment.env);
    if (!references.active) {
      attachment.env->ExceptionClear();
      return -1;
    }
    const auto parcel = CallObjectClassMethodFromAndroidEnvironment(
      attachment.env, fileDescriptor, "getParcelFileDescriptor", "()Landroid/os/ParcelFileDescriptor;"
    );
    if (takeProviderError(attachment.env) || !parcel) return -1;
    const auto fd = CallClassMethodFromAndroidEnvironment(attachment.env, Int, parcel, "getFd", "()I");
    return takeProviderError(attachment.env) ? -1 : fd;
  }
}
