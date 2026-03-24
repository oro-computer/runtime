{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "oro-runtime-dev";

  # Build tools and compilers
  nativeBuildInputs = with pkgs; [
    # C++ toolchain (latest clang for C++20 support)
    llvmPackages_18.clang
    llvmPackages_18.libcxx

    # Build systems
    cmake
    gnumake
    ninja

    # Build utilities
    pkg-config
    autoconf
    automake
    libtool

    # Version control
    git

    # Rust toolchain
    rustup

    # Node.js toolchain
    nodejs_20
    nodePackages.pnpm

    # Utilities
    curl
    wget
    which
    python3
  ];

  # Runtime libraries
  buildInputs = with pkgs; [
    # Core dependencies
    libuv
    libsodium
    mbedtls
    libusb1

    # GUI/WebView dependencies (required for desktop runtime)
    glib
    gtk3
    webkitgtk_4_1

    # System libraries
    openssl
    zlib

    # C++ standard library
    stdenv.cc.cc.lib
  ];

  # Environment configuration
  shellHook = ''
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║  Oro Runtime Development Environment (NixOS)                  ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo ""
    echo "Build Configuration:"
    echo "  • C++ Compiler: clang++ (C++20 enabled)"
    echo "  • Rust: $(rustc --version 2>/dev/null || echo 'run: rustup default stable')"
    echo "  • Node.js: $(node --version)"
    echo "  • pnpm: $(pnpm --version)"
    echo ""

    # Set environment variables for the build
    export NO_ANDROID=1
    export NO_IOS=1
    export PREFIX="$PWD/build/prefix"
    export CPU_CORES="$(nproc)"

    # Ensure Rust is available
    if ! command -v cargo &> /dev/null; then
      echo "⚠️  Rust toolchain not initialized. Run:"
      echo "    rustup default stable"
      echo ""
    fi

    # Setup pkg-config paths
    export PKG_CONFIG_PATH="${pkgs.libuv}/lib/pkgconfig:${pkgs.libsodium}/lib/pkgconfig:${pkgs.mbedtls}/lib/pkgconfig:${pkgs.libusb1}/lib/pkgconfig:${pkgs.glib}/lib/pkgconfig:${pkgs.gtk3}/lib/pkgconfig:${pkgs.webkitgtk_4_1}/lib/pkgconfig:$PKG_CONFIG_PATH"

    # Setup library paths
    export LD_LIBRARY_PATH="${pkgs.libuv}/lib:${pkgs.libsodium}/lib:${pkgs.mbedtls}/lib:${pkgs.libusb1}/lib:${pkgs.glib}/lib:${pkgs.gtk3}/lib:${pkgs.webkitgtk_4_1}/lib:${pkgs.stdenv.cc.cc.lib}/lib:$LD_LIBRARY_PATH"

    # Compiler configuration
    export CC="${pkgs.llvmPackages_18.clang}/bin/clang"
    export CXX="${pkgs.llvmPackages_18.clang}/bin/clang++"

    echo "Environment Variables:"
    echo "  • PREFIX=$PREFIX"
    echo "  • NO_ANDROID=$NO_ANDROID"
    echo "  • NO_IOS=$NO_IOS"
    echo "  • CPU_CORES=$CPU_CORES"
    echo ""
    echo "Build Commands:"
    echo "  • Install dependencies: ./bin/install.sh"
    echo "  • Build runtime:        make runtime"
    echo "  • Run tests:            npm test"
    echo "  • Clean build:          make clean"
    echo ""
    echo "📝 Note: Building on NixOS - all dependencies provided via Nix"
    echo "🔧 Android/iOS builds are disabled (desktop Linux only)"
    echo ""
  '';

  # Additional environment variables
  NO_ANDROID = "1";
  NO_IOS = "1";

  # Prevent the build from trying to install to /usr/local
  PREFIX = "$PWD/build/prefix";
}
