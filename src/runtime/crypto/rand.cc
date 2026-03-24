#include <random>
#include <limits>

#include "../crypto.hh"

namespace oro::runtime::crypto {
  // Use a process-global 64-bit RNG seeded from a non-deterministic source.
  static std::mt19937_64& get_rng () {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    return gen;
  }

  uint64_t rand64 () {
    static std::uniform_int_distribution<uint64_t> dist(
      0,
      std::numeric_limits<uint64_t>::max()
    );
    return dist(get_rng());
  }

	int randint (int a, int b) {
    if (a == 0 && b == 0) {
      return 0;
    }

    static std::random_device rd;  // non-deterministic random seed
    static std::mt19937 gen(rd()); // mersenne twister rng

    // Create a uniform distribution in the range of valid indices
    std::uniform_int_distribution<size_t> dist(a, b);

    // Generate and return a random index
    return dist(gen);
  }

	int randint (int a) {
    return randint(a, INT_MAX);
  }

	int randint () {
    return randint(0, INT_MAX);
  }
}
