#ifndef ORO_RUNTIME_NATIVE_ANDROID_SERVICES_H
#define ORO_RUNTIME_NATIVE_ANDROID_SERVICES_H

#include "execution.hh"

namespace oro::runtime::native {
  struct NativeHttpsRequest {
    std::string url;
    std::string method = "GET";
    std::map<std::string, std::string> headers;
    std::optional<RecordBytes> body;
    int timeoutMs = 30000;
    int maximumResponseBytes = 1048576;
  };
  struct NativeHttpsResponse {
    int status = 0;
    RecordBytes body;
  };
  struct NativeV1RecordStatus : RecordStatus {
    std::string commitId;
  };
  /** Explicit access to the existing Android Kotlin v1 store, not an implicit conversion to C++ v2. */
  class AndroidV1Records {
   public:
    RecordSnapshot read () const;
    NativeV1RecordStatus status () const;
    RecordSnapshot apply (int64_t revision, const RecordChanges&, const std::string& commitId = "");
    RecordSnapshot migrate (const std::string& id, const std::string& sourceSha256, int64_t revision, const RecordValues&);
    std::optional<CoordinationState> get (const std::string& slot) const;
    CoordinationState prepare (const std::string& slot, const std::string& operationRef, int64_t revision);
    CoordinationState begin (const CoordinationTicket&);
    CoordinationState unknown (const CoordinationTicket&);
    CoordinationState abort (const CoordinationTicket&);
    CoordinationState commit (const CoordinationTicket&, const RecordChanges&, const RecordBytes& receipt = {});
    CoordinationState resolve (const CoordinationTicket&, bool applied, const RecordChanges& = {}, const RecordBytes& receipt = {});
    CoordinationState recover (const CoordinationTicket&);
    RecordSnapshot release (const CoordinationTicket&);

   private:
    friend class AndroidPlatform;
    using Request = std::function<std::string (const std::string&, const std::string&)>;
    explicit AndroidV1Records (Request request) : request (std::move (request)) {}
    Request request;
    CoordinationState transition (const std::string&, const CoordinationTicket&, const RecordChanges& = {},
                                  const RecordBytes& receipt = {}, bool applied = false);
  };
}  // namespace oro::runtime::native
#endif
