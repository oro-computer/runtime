#ifndef ORO_RUNTIME_PLATFORM_ANDROID_CONTENT_RESOLVER_H
#define ORO_RUNTIME_PLATFORM_ANDROID_CONTENT_RESOLVER_H

#include "environment.hh"
#include "types.hh"

namespace oro::runtime::android {
  class ContentResolver {
    public:
      using FileDescriptor = jobject;

      Application application = nullptr;
      JVMEnvironment jvm;
      ContentResolver () = default;
      ContentResolver (const ContentResolver&);
      ContentResolver (ContentResolver&&);

      ContentResolver& operator = (const ContentResolver&);
      ContentResolver& operator = (ContentResolver&&);

      bool isDocumentURI (const String& uri);
      bool isContentURI (const String& uri);
      bool isExternalStorageDocumentURI (const String& uri);
      bool isDownloadsDocumentURI (const String& uri);
      bool isMediaDocumentURI (const String& uri);
      bool isPhotosURI (const String& uri);

      String getContentMimeType (const String& url);
      String getExternalContentURIForType (const String& type);
      String getPathnameFromURI (const String& uri);
      String getPathnameFromContentURI (
        const String& uri,
        const String& id = ""
      );

      Vector<String> getPathnameEntriesFromContentURI (const String& uri);

      bool hasAccess (const String& uri, int* error = nullptr);
      FileDescriptor openFileDescriptor (
        const String& uri,
        off_t* offset = nullptr,
        off_t* length = nullptr,
        const String& mode = "r",
        int* error = nullptr
      );

      // Reject provider modes whose semantics cannot preserve the requested flags.
      static int getOpenMode (int flags, String& mode);
      static int getReadRange (size_t offset, size_t size, off_t start, off_t length, int64_t& position, size_t& count);

      bool closeFileDescriptor (FileDescriptor fileDescriptor);
      off_t getFileDescriptorLength (FileDescriptor fileDescriptor);
      off_t getFileDescriptorOffset (FileDescriptor fileDescriptor);
      int getFileDescriptorFD (FileDescriptor fileDescriptor);
  };
}
#endif
