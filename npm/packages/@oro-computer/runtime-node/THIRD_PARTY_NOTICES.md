# Third-Party Notices

Oro Runtime is licensed under Apache-2.0. Portions of the source tree and
release artifacts include third-party software under their own licenses.

Bundled source with checked-in license text:

- `api/external/libsodium`: ISC license in `api/external/libsodium/LICENSE`.
- `api/fetch`: MIT license in `api/fetch/LICENSE`.
- `api/navigation`: MIT license in `api/navigation/LICENSE.md`.
- `api/test`: MIT license in `api/test/LICENSE`.
- `api/url/url`: MIT license in `api/url/url/LICENSE.txt`.
- `api/url/urlpattern`: BSD-3-Clause license in `api/url/urlpattern/LICENSE`.
- `include/cranium`: MIT license in `include/cranium/LICENSE`.
- `include/cpp-httplib`: MIT license in `include/cpp-httplib/LICENSE`.
- `include/nlohmann`: MIT license in `include/nlohmann/LICENSE.MIT`.

The build also obtains native dependencies, including asn1c, cr-sqlite, Iroh,
jsoncons, libipfs, libsodium, libusb, libuv, llama.cpp, mbedTLS, SQLite,
whisper.cpp, and zlib. Network Git sources are verified against the immutable revisions in
`bin/install.sh`; downloaded archives are checksum-verified. SQLite's
amalgamation is dedicated to the public domain by its authors. Release
distributions collect the available upstream license files under
`share/licenses/oro-runtime/`, including license texts and a dependency inventory
for crates linked into the `oro-iroh` static library. Releases also include an
SPDX SBOM of the staged output. Those components remain governed by their
upstream licenses.

Maintainers must update this inventory when adding bundled code or build-time
dependencies. This file is attribution information and does not change any
third-party license.
