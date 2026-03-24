#ifndef ORO_RUNTIME_CORE_SERVICES_SECURE_STORAGE_H
#define ORO_RUNTIME_CORE_SERVICES_SECURE_STORAGE_H

#include "../../core.hh"
#include "../../bytes.hh"

namespace oro::runtime::core::services {
  class SecureStorage : public core::Service {
    public:
      enum class Encoding {
        UTF8,
        BASE64,
        HEX
      };

      static bool parseEncoding(const String& value, Encoding& encoding);
      static String encodingToString(Encoding encoding);
      static bool decodeValue(
        const String& value,
        Encoding encoding,
        bytes::Buffer& output,
        String& error
      );
      static bool encodeValue(
        const bytes::Buffer& value,
        Encoding encoding,
        String& output,
        String& error
      );

      SecureStorage (const Options& options);
      ~SecureStorage ();

      bool start () override;
      bool stop () override;

      void set (
        const String& seq,
        String scope,
        String key,
        bytes::Buffer value,
        const Callback callback
      );

      void get (
        const String& seq,
        String scope,
        String key,
        Encoding encoding,
        const Callback callback
      );

      void remove (
        const String& seq,
        String scope,
        String key,
        const Callback callback
      );

      void clear (
        const String& seq,
        String scope,
        const Callback callback
      );

      void keys (
        const String& seq,
        String scope,
        const Callback callback
      );

    private:
      String computeDefaultScope () const;
      bool normalizeScope (
        const String& requested,
        String& normalized,
        String& error
      ) const;
      String indexScopeFor (const String& scope) const;
      bool registerKey (const String& scope, const String& key);
      bool unregisterKey (const String& scope, const String& key);
      bool clearRegisteredKeys (const String& scope);
      Vector<String> registeredKeys (const String& scope) const;
      JSON::Object makeError (
        const char* source,
        const String& message,
        const String& type = "Error"
      ) const;
      JSON::Object makeDisabledError (const char* source) const;

      bool platformSet (
        const String& scope,
        const String& key,
        const bytes::Buffer& value,
        String& error
      );

      bool platformGet (
        const String& scope,
        const String& key,
        bytes::Buffer& value,
        bool& found,
        String& error
      );

      bool platformRemove (
        const String& scope,
        const String& key,
        String& error
      );

      bool platformClear (
        const String& scope,
        String& error
      );

      bool platformListKeys (
        const String& scope,
        Vector<String>& keys,
        String& error
      );
  };
}

#endif
