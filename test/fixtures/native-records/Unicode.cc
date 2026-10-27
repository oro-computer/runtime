#include "src/runtime/native/unicode.hh"
#include <cassert>
#include <iostream>

using namespace oro::runtime::native;
template <class F>
void fails (const std::string& code, F work) {
  try {
    work ();
    assert (false);
  } catch (const StateError& error) {
    assert (error.code == code);
  }
}
int main () {
  const std::string nul ("public\0data", 11);
  assert (nativeUtf8 (nativeUtf16 (nul)) == nul);
  assert (nativeUtf16 ("\xf0\x9f\x8e\x88") == u"\U0001f388");
  // Every Unicode scalar, including embedded zero and supplementary characters.
  for (uint32_t value = 0; value <= 0x10ffff; value++) {
    if (value >= 0xd800 && value <= 0xdfff)
      continue;
    std::u16string input;
    if (value < 0x10000) {
      input.push_back (static_cast<char16_t> (value));
    } else {
      input.push_back (static_cast<char16_t> (0xd800 + ((value - 0x10000) >> 10)));
      input.push_back (static_cast<char16_t> (0xdc00 + ((value - 0x10000) & 1023)));
    }
    assert (nativeUtf16 (nativeUtf8 (input)) == input);
  }
  for (const std::string bytes : {"\x80", "\xc0\x80", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x90\x80"})
    fails ("EINVAL", [&] { nativeUtf16 (bytes); });
  fails ("EINVAL", [] { nativeUtf8 (std::u16string (1, static_cast<char16_t> (0xd800))); });
  fails ("EINVAL", [] { nativeUtf8 (std::u16string (1, static_cast<char16_t> (0xdc00))); });
  fails ("E2BIG", [] { nativeUtf16 (std::string (2097153, 'x')); });
  std::cout << "native-utf-all-scalars-nul-supplementary-malformed-and-bounds-passed\n";
}
