#include "src/runtime/native/web_push.hh"

#include <cassert>
#include <algorithm>
#include <iostream>

using namespace oro::runtime::native;
template <class F> void rejects (F run) {
  try { run(); assert(false); } catch (const StateError&) {}
}
int main () {
  // Public interoperability vector from RFC 8291 section 5.
  WebPushKeys keys;
  const auto privateKey = webPushDecode("q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94");
  const auto publicKey = webPushDecode("BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4");
  const auto auth = webPushDecode("BTBZMqHH6r4Tts7J_aSIgg");
  std::copy(privateKey.begin(), privateKey.end(), keys.privateKey.begin());
  std::copy(publicKey.begin(), publicKey.end(), keys.publicKey.begin());
  std::copy(auth.begin(), auth.end(), keys.authentication.begin());
  auto encrypted = webPushDecode("DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgSxsj_Qulcy4a-fN");
  const auto decoded = decryptWebPush(keys, encrypted);
  assert(std::string(decoded.begin(), decoded.end()) == "When I grow up, I want to be a watermelon");
  validateWebPushPublicKey(keys.publicKey);
  const auto fresh = WebPushKeys::generate();
  validateWebPushPublicKey(fresh.publicKey);
  rejects([&] { decryptWebPush(fresh, encrypted); });
  encrypted.back() ^= 1;
  rejects([&] { decryptWebPush(keys, encrypted); });
  encrypted.back() ^= 1;
  encrypted[21] = 0;
  rejects([&] { decryptWebPush(keys, encrypted); });
  encrypted[21] = 4;
  encrypted[20] = 64;
  rejects([&] { decryptWebPush(keys, encrypted); });
  rejects([&] { decryptWebPush(keys, {}); });
  rejects([&] { webPushDecode("a+="); });
  assert(webPushBase64(auth) == "BTBZMqHH6r4Tts7J_aSIgg");
  std::cout << "rfc8291-vector-and-authentication-rejection-passed\n";
}
