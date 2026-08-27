#include <random>
#include <limits>
#include <mutex>

#include "../crypto.hh"

namespace oro::runtime::crypto {
  // Use a process-global 64-bit RNG seeded from a non-deterministic source.
  static std::mt19937_64& get_rng () {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    return gen;
  }

  static std::mutex& get_rng_mutex () {
    static std::mutex mutex;
    return mutex;
  }

  uint64_t rand64 () {
    static std::uniform_int_distribution<uint64_t> dist(
      0,
      std::numeric_limits<uint64_t>::max()
    );
    const std::lock_guard lock(get_rng_mutex());
    return dist(get_rng());
  }

  int randint (int a, int b) {
    if (a == 0 && b == 0) {
      return 0;
    }

    if (a > b) {
      std::swap(a, b);
    }
    std::uniform_int_distribution<int> dist(a, b);
    const std::lock_guard lock(get_rng_mutex());
    return dist(get_rng());
  }

  int randint (int a) {
    return randint(a, INT_MAX);
  }

  int randint () {
    return randint(0, INT_MAX);
  }
}
