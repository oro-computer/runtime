#ifndef ORO_RUNTIME_NATIVE_WEB_PUSH_H
#define ORO_RUNTIME_NATIVE_WEB_PUSH_H

#include "records.hh"
#include <span>

namespace oro::runtime::native {
  /** Native subscription material; privateKey never crosses the JavaScript bridge. */
  struct WebPushKeys {
    std::array<unsigned char, 32> privateKey;
    std::array<unsigned char, 65> publicKey;
    std::array<unsigned char, 16> authentication;
    ~WebPushKeys();
    static WebPushKeys generate ();
  };

  bool webPushAvailable ();
  std::string webPushBase64(std::span<const unsigned char>);
  RecordBytes webPushDecode (const std::string&);
  void validateWebPushPublicKey(std::span<const unsigned char>);
  /** RFC 8291 single-record aes128gcm, 103..4096 encrypted bytes. */
  RecordBytes decryptWebPush (const WebPushKeys&, std::span<const unsigned char>);
}  // namespace oro::runtime::native
#endif
