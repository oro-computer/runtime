#include "unicode.hh"

namespace oro::runtime::native {
  std::u16string nativeUtf16 (std::string_view input) {
    if (input.size () > 2097152)
      throw StateError ("E2BIG");
    std::u16string output;
    for (size_t index = 0; index < input.size ();) {
      const auto first = static_cast<unsigned char> (input[index++]);
      uint32_t value = first;
      size_t count = 0;
      uint32_t minimum = 0;
      if (first >= 0xc2 && first <= 0xdf) {
        value &= 31;
        count = 1;
        minimum = 0x80;
      } else if (first >= 0xe0 && first <= 0xef) {
        value &= 15;
        count = 2;
        minimum = 0x800;
      } else if (first >= 0xf0 && first <= 0xf4) {
        value &= 7;
        count = 3;
        minimum = 0x10000;
      } else if (first >= 0x80) {
        throw StateError ("EINVAL");
      }
      if (count > input.size () - index)
        throw StateError ("EINVAL");
      while (count--) {
        const auto byte = static_cast<unsigned char> (input[index++]);
        if ((byte & 0xc0) != 0x80)
          throw StateError ("EINVAL");
        value = (value << 6) | (byte & 63);
      }
      if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        throw StateError ("EINVAL");
      if (value >= 0x10000) {
        value -= 0x10000;
        output.push_back (static_cast<char16_t> (0xd800 + (value >> 10)));
        output.push_back (static_cast<char16_t> (0xdc00 + (value & 1023)));
      } else {
        output.push_back (static_cast<char16_t> (value));
      }
    }
    return output;
  }
  std::string nativeUtf8 (std::u16string_view input) {
    if (input.size () > 2097152)
      throw StateError ("E2BIG");
    std::string output;
    for (size_t index = 0; index < input.size (); index++) {
      uint32_t value = input[index];
      if (value >= 0xd800 && value <= 0xdbff) {
        if (++index >= input.size () || input[index] < 0xdc00 || input[index] > 0xdfff)
          throw StateError ("EINVAL");
        value = 0x10000 + ((value - 0xd800) << 10) + (input[index] - 0xdc00);
      } else if (value >= 0xdc00 && value <= 0xdfff) {
        throw StateError ("EINVAL");
      }
      if (value < 0x80) {
        output.push_back (static_cast<char> (value));
      } else {
        if (value >= 0x10000) {
          output.push_back (static_cast<char> (0xf0 | (value >> 18)));
          output.push_back (static_cast<char> (0x80 | ((value >> 12) & 63)));
        } else if (value >= 0x800) {
          output.push_back (static_cast<char> (0xe0 | (value >> 12)));
        } else {
          output.push_back (static_cast<char> (0xc0 | (value >> 6)));
        }
        if (value >= 0x800)
          output.push_back (static_cast<char> (0x80 | ((value >> 6) & 63)));
        output.push_back (static_cast<char> (0x80 | (value & 63)));
      }
    }
    if (output.size () > 2097152)
      throw StateError ("E2BIG");
    return output;
  }
}  // namespace oro::runtime::native
