#ifndef ORO_RUNTIME_CORE_SERVICES_UPDATE_H
#define ORO_RUNTIME_CORE_SERVICES_UPDATE_H

#include "../../core.hh"
#include "../../bytes.hh"

namespace oro::runtime::core::services {
  class Update : public core::Service {
    public:
      struct ManifestCheckOptions {
        String manifestUrl;
        String signatureUrl;
        String expectedAppId;
        // Hex or base64(/url) encoded public keys.
        Vector<String> publicKeys;
        // Optional maximum manifest size in bytes; 0 means "no explicit limit".
        uint64_t maxManifestBytes = 0;
        // Optional additional HTTP headers (name/value).
        Vector<std::pair<String, String>> headers;
      };

      struct SelectionOptions {
        String channel;
        String currentVersion;
        String platform;
        String arch;
        String runtimeVersion;
      };

      struct DownloadOptions {
        // Optional maximum artifact size in bytes; 0 means "no explicit limit".
        uint64_t maxArtifactBytes = 0;
      };

      Update (const Options& options)
        : core::Service(options) {}

      // Fetches & verifies a manifest + signature pair and selects an update,
      // returning a JSON payload that mirrors the JS UpdateCheckResult shape
      // (minus artifact bytes). All heavy work is performed off the loop
      // thread; the callback is invoked on the loop thread.
      void check (
        const String& seq,
        const ManifestCheckOptions& manifest,
        const SelectionOptions& selection,
        const DownloadOptions& download,
        const Callback callback
      );

      // Downloads an artifact, enforcing size limits and hash verification,
      // and returns the bytes via QueuedResponse with
      // content-type=application/octet-stream.
      void download (
        const String& seq,
        const String& artifactUrl,
        const String& hashAlgorithm,
        const String& hash,
        uint64_t expectedLength,
        const DownloadOptions& options,
        const Callback callback
      );
  };
}

#endif

