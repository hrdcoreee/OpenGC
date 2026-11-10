#!/usr/bin/env bash
set -euo pipefail

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${YELLOW}===================================================${NC}"
echo -e "${YELLOW}           OpenGC Linux Build Script               ${NC}"
echo -e "${YELLOW}===================================================${NC}"

# 1. Check required tools
REQUIRED_CMDS=("cmake" "git" "curl" "g++")
for cmd in "${REQUIRED_CMDS[@]}"; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo -e "${RED}[!] ERROR: '$cmd' is not installed.${NC}"
        echo -e "On Debian/Ubuntu run: sudo apt update && sudo apt install -y build-essential cmake ninja-build git curl pkg-config gcc-multilib g++-multilib"
        exit 1
    fi
done

# 2. Check or Setup vcpkg
if [ -z "${VCPKG_ROOT:-}" ]; then
    if [ -f "$SCRIPT_DIR/vcpkg/vcpkg" ]; then
        export VCPKG_ROOT="$SCRIPT_DIR/vcpkg"
    elif [ -f "$HOME/vcpkg/vcpkg" ]; then
        export VCPKG_ROOT="$HOME/vcpkg"
    fi
fi

if [ -z "${VCPKG_ROOT:-}" ]; then
    echo -e "${CYAN}[*] vcpkg not found. Cloning vcpkg into $SCRIPT_DIR/vcpkg...${NC}"
    git clone https://github.com/microsoft/vcpkg.git "$SCRIPT_DIR/vcpkg"
    echo -e "${CYAN}[*] Bootstrapping vcpkg...${NC}"
    "$SCRIPT_DIR/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
    export VCPKG_ROOT="$SCRIPT_DIR/vcpkg"
fi

echo -e "${GREEN}[*] Using vcpkg: $VCPKG_ROOT${NC}"

# 3. Determine Generator
CMAKE_GEN="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    CMAKE_GEN="Ninja"
fi
echo -e "${GREEN}[*] Using CMake Generator: $CMAKE_GEN${NC}"

mkdir -p "$SCRIPT_DIR/release"

# 4. Build Client (x64 Linux)
echo -e "\n${CYAN}[*] Configuring Client (x64 Linux)...${NC}"
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build" -G "$CMAKE_GEN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOUTDIR="$SCRIPT_DIR/release" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux

echo -e "\n${CYAN}[*] Compiling Client (csgo_gc, csgo)...${NC}"
cmake --build "$SCRIPT_DIR/build" --config Release --target csgo_gc csgo

# 5. Build Dedicated Server (x86 32-bit Linux) if 32-bit multilib is available
if dpkg -l | grep -q "gcc-multilib" || command -v i686-linux-gnu-gcc >/dev/null 2>&1; then
    echo -e "\n${CYAN}[*] Configuring Dedicated Server (x86 32-bit Linux)...${NC}"
    cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build_ds" -G "$CMAKE_GEN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DOUTDIR="$SCRIPT_DIR/release" \
        -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_TARGET_TRIPLET=x86-linux

    echo -e "\n${CYAN}[*] Compiling Dedicated Server (srcds, csgo_gc)...${NC}"
    cmake --build "$SCRIPT_DIR/build_ds" --config Release --target srcds csgo_gc
else
    echo -e "${YELLOW}[!] Skipping 32-bit DS build (install gcc-multilib and g++-multilib for 32-bit srcds support).${NC}"
fi

echo -e "\n${GREEN}===================================================${NC}"
echo -e "${GREEN}[OK] Linux build completed successfully!           ${NC}"
echo -e "${GREEN}Binaries available in: build/ and release/         ${NC}"
echo -e "${GREEN}===================================================${NC}\n"
