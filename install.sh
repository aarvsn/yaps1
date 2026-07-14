#!/usr/bin/env bash
# ============================================================================
# YAPS1 — Yet Another PlayStation 1 Emulator  |  Native Install Script
# ============================================================================
# Builds YAPS1 from source and installs the binary to a user-writable prefix.
# Supports: Ubuntu/Debian, Fedora, Arch Linux, macOS, Windows (MSYS2/Clang)
#
# Usage:
#   chmod +x install.sh
#   ./install.sh              # installs to ~/.local
#   ./install.sh /usr/local   # installs to /usr/local (needs sudo)
#   ./install.sh --uninstall  # removes installed files
# ============================================================================

set -euo pipefail

# ---- Defaults ----
PREFIX="${1:-$HOME/.local}"
UNINSTALL=false
BUILD_DIR="build"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# ---- Parse arguments ----
for arg in "$@"; do
    case "$arg" in
        --uninstall) UNINSTALL=true ;;
        --help|-h)
            echo "Usage: $0 [PREFIX] [--uninstall]"
            echo ""
            echo "  PREFIX       Install prefix (default: ~/.local)"
            echo "  --uninstall  Remove YAPS1 from the given prefix"
            echo "  --help       Show this help"
            exit 0
            ;;
        -*) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

# ---- Uninstall path ----
if [ "$UNINSTALL" = true ]; then
    echo "==> Uninstalling YAPS1 from $PREFIX ..."
    rm -f "$PREFIX/bin/YAPS1" "$PREFIX/bin/yaps1"
    echo "==> Done."
    exit 0
fi

echo "==> YAPS1 Install Script"
echo "    Prefix : $PREFIX"
echo "    Jobs   : $JOBS"
echo ""

# ---- Detect OS and install build dependencies ----
install_deps() {
    echo "==> Detecting platform and installing build dependencies..."
    if command -v apt-get &>/dev/null; then
        # Debian / Ubuntu
        sudo apt-get update -qq
        sudo apt-get install -y -qq cmake g++ git pkg-config \
            libgl-dev libvulkan-dev zlib1g-dev
    elif command -v dnf &>/dev/null; then
        # Fedora
        sudo dnf install -y cmake gcc-c++ git pkg-config \
            mesa-libGL-devel vulkan-loader-devel zlib-devel
    elif command -v pacman &>/dev/null; then
        # Arch
        sudo pacman -Sy --noconfirm cmake gcc git pkg-config \
            mesa vulkan-icd-loader zlib
    elif command -v brew &>/dev/null; then
        # macOS (Homebrew)
        brew install cmake git zlib
    else
        echo "WARNING: Could not detect package manager."
        echo "         Please install manually: cmake, C++23 compiler, git"
    fi
}

# ---- Build ----
build_yaps1() {
    echo "==> Configuring CMake ..."
    cmake -B "$BUILD_DIR" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release

    echo "==> Building YAPS1 ($JOBS jobs) ..."
    cmake --build "$BUILD_DIR" -j"$JOBS"

    echo "==> Installing to $PREFIX ..."
    cmake --install "$BUILD_DIR"

    echo ""
    echo "==> YAPS1 installed successfully!"
    echo "    Binary: $PREFIX/bin/YAPS1"
    echo ""
    echo "    Usage: YAPS1 <game.bin|game.cue|game.iso>"
    echo ""
    echo "    Place your PS1 BIOS (SCPH1001.BIN) in the same directory as the game,"
    echo "    or set bios_path in config.toml."
}

# ---- Main ----
install_deps
build_yaps1