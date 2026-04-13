#ifndef ORO_RUNTIME_CRYPTO_H
#define ORO_RUNTIME_CRYPTO_H

#include "platform.hh"

#define SHA1_DIGEST_SIZE 20

namespace oro::runtime::crypto {
  uint64_t rand64 ();
	int randint (int a, int b);
	int randint (int a);
	int randint ();

  struct SHA1Context {
    uint32_t state[5];
    uint64_t bitCount;
    unsigned char buffer[64];
  };

  void sha1_init (SHA1Context *ctx);
  void sha1_update (
    SHA1Context *ctx,
    const unsigned char *data,
    size_t size
  );

  void sha1_final (
    SHA1Context *ctx,
    unsigned char out[SHA1_DIGEST_SIZE]
  );

  void sha1 (
    const unsigned char *data,
    size_t size,
    unsigned char out[SHA1_DIGEST_SIZE]
  );

  class SHA1 {
    public:
      SHA1Context context;
      bool isFinalized = false;
      unsigned char output[SHA1_DIGEST_SIZE] = {0};

      SHA1 ();
      SHA1 (const types::String&);
      SHA1 (const types::SharedPointer<unsigned char[]>&, size_t);
      SHA1 (const unsigned char *, size_t);

      SHA1& update (const unsigned char *, size_t);
      SHA1& update (const types::String&);
      bool finalized () const;
      types::Vector<uint8_t> finalize ();
      types::String str ();
      inline size_t size () const {
        return SHA1_DIGEST_SIZE;
      }
  };

  const types::String sha1 (const types::String&);
  const types::String sha1 (const types::SharedPointer<unsigned char[]>&, size_t);
  const types::String sha1 (const unsigned char *, size_t);
}
#endif
