#include "../crypto.hh"
#include "../uuid.hh"

#include <chrono>

namespace oro::runtime::uuid {
  void v7 (char* buffer) {
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()
    ).count();
    uint64_t timestamp = delta & ((1ull << 60) - 1);
    uint64_t value = 0;

    for (int i = 0; i < 8; ++i) {
      value = (value << 8) | (crypto::rand64() % 256);
    }
    value &= ((1ull << 62) - 1);

    uint64_t part1 = (timestamp << 4) | 0x7;
    uint64_t part2 = value & ~0xc000000000000000ull;
    part2 |= 0x8000000000000000ull;


    snprintf(
      buffer,
      37,
      "%08x-%04x-%04x-%02x%02x-%012llx",
      (uint32_t) (part1 >> 32),
      (uint16_t) (part1 >> 16),
      (uint16_t) (part1),
      (uint8_t) (part2 >> 56),
      (uint8_t) (part2 >> 48),
      static_cast<unsigned long long >(part2 & 0x0000ffffffffffffull)
    );
  }

  String v7 () {
    String output;
    output.resize(37);
    v7(output.data());
    // v7(char*) writes a 36-byte UUID string plus a trailing NUL. Avoid
    // persisting that NUL in the returned std::string to keep downstream
    // consumers (e.g., URLs) stable.
    if (!output.empty() && output.back() == '\0') {
      output.pop_back();
    } else {
      output.resize(36);
    }
    return output;
  }
}
