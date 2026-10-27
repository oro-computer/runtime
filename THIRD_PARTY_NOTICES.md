# Third-Party Notices

Oro Runtime is licensed under Apache-2.0. Portions of the source tree and
release artifacts include third-party software under their own licenses.

Linux protected-key integration uses the PKCS #11 definitions from
[p11-kit 0.25.5](https://github.com/p11-glue/p11-kit/blob/0.25.5/common/pkcs11.h).
The fetched header preserves its authors' notice and permission for unrestricted
copying and distribution with that notice intact. Hardware modules and token
drivers remain separately installed operating-system dependencies.

Bundled source with checked-in license text:

- `api/buffer.js`: MIT-licensed Buffer implementation with MIT-licensed
  `base64-js` code and BSD-3-Clause `ieee754` code; license texts are in
  `api/buffer/`.
- `api/external/libsodium`: ISC license in `api/external/libsodium/LICENSE`.
- `api/fetch`: MIT license in `api/fetch/LICENSE`.
- `api/internal/streams/web.js`: MIT license in `api/internal/streams/LICENSE`.
- `api/navigation`: MIT license in `api/navigation/LICENSE.md`.
- `api/querystring.js`: MIT license included in the source header.
- `api/stream.js`: MIT license included in the source header.
- `api/test`: MIT license in `api/test/LICENSE`.
- `api/url/url`: MIT license in `api/url/url/LICENSE.txt`.
- `api/url/urlpattern`: BSD-3-Clause license in `api/url/urlpattern/LICENSE`.
- `include/cranium`: MIT license in `include/cranium/LICENSE`.
- `include/cpp-httplib`: MIT license in `include/cpp-httplib/LICENSE`.
- `include/nlohmann`: MIT license in `include/nlohmann/LICENSE.MIT`.

The build also obtains native dependencies, including asn1c, cr-sqlite, Iroh,
jsoncons, libipfs, libsodium, libusb, libuv, llama.cpp, mbedTLS, Microsoft
WebView2, SQLite, whisper.cpp, and zlib. Network Git sources are verified against
the immutable revisions in `bin/install.sh`; downloaded archives are
checksum-verified. SQLite's amalgamation is dedicated to the public domain by
its authors. Release distributions collect the available upstream license files
and notices under `share/licenses/oro-runtime/`, including Microsoft WebView2's
license and third-party notice plus a dependency inventory for crates linked
into the `oro-iroh` static library. Releases also include an SPDX SBOM of the
staged output. Those components remain governed by their upstream licenses.

The optional `oroc push server` development command downloads the official ntfy
2.33.1 release on first use and verifies a pinned SHA-256 before execution.
ntfy is an external executable, not linked into the runtime. Its upstream
license and notices remain in the downloaded archive in the user's tools cache.
See [ntfy's source and licensing](https://github.com/binwiederhier/ntfy/tree/v2.33.1).

Maintainers must update this inventory when adding bundled code or build-time
dependencies. This file is attribution information and does not change any
third-party license.
