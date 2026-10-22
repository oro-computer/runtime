package oro.runtime.app
import java.net.URI
import java.security.MessageDigest
fun main() {
  val digest = MessageDigest.getInstance("SHA-256").digest("public-certificate".toByteArray())
  val correct = java.util.Base64.getEncoder().encodeToString(digest)
  val wrong = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
  val pins = NativeTlsPolicy.parseTlsPins("example.test sha256/$correct\nexample.test:8443 sha256/$wrong\nrevoked.test:443\ninvalid.test sha256/not-a-digest")
  check(NativeTlsPolicy.isCertificateAllowedForHost(pins, "EXAMPLE.TEST.:443", digest))
  check(!NativeTlsPolicy.isCertificateAllowedForHost(pins, "example.test:8443", digest))
  check(!NativeTlsPolicy.isCertificateAllowedForHost(pins, "revoked.test:443", digest))
  check(!NativeTlsPolicy.isCertificateAllowedForHost(pins, "invalid.test:443", digest))
  check(!NativeTlsPolicy.hasTlsPinsForHost(pins, "foreign.test:443"))
  check(NativeHttps.endpoint(URI("https://EXAMPLE.test/public")) == "example.test:443")
  check(NativeHttps.endpoint(URI("https://[::1]:8443/public")) == "[::1]:8443")
  for (url in listOf("http://example.test", "https://user:secret@example.test", "https://example.test/#secret", "https://example.test:0", "https://example.test:65536", "https://foreign%2etest")) {
    try { NativeHttps.endpoint(URI(url)); error("invalid endpoint accepted") } catch (_: IllegalArgumentException) {}
  }
  val rotated = NativeTlsPolicy.parseTlsPins("example.test:443 sha256/$wrong sha256/$correct")
  check(NativeTlsPolicy.isCertificateAllowedForHost(rotated, "example.test:443", digest))
  println("native TLS authority, pin rotation, revocation and fail-closed parser checks passed")
}
