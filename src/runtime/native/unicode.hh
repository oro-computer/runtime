#ifndef ORO_RUNTIME_NATIVE_UNICODE_H
#define ORO_RUNTIME_NATIVE_UNICODE_H
#include "records.hh"
#include <string_view>
namespace oro::runtime::native {
  /** Strict conversion, preserving U+0000 and supplementary characters; rejects malformed sequences. */
  std::u16string nativeUtf16 (std::string_view);
  std::string nativeUtf8 (std::u16string_view);
}  // namespace oro::runtime::native
#endif
