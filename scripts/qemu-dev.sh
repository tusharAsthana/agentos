#!/bin/bash
#
# AgentOS QEMU Development Environment
# Launches a fully emulated iPhone boot stack for testing without hardware.
#
# Usage: ./scripts/qemu-dev.sh [--gdb] [--debug] [--no-monitor]
#   --gdb       Start GDB server on localhost:1234
#   --debug     Enable QEMU debug output to stderr
#   --no-monitor  Disable QEMU monitor on stdio (use -nographic)
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse arguments
GDB_SERVER=0
DEBUG_OUTPUT=0
USE_MONITOR=1

while [[ $# -gt 0 ]]; do
  case $1 in
    --gdb)
      GDB_SERVER=1
      shift
      ;;
    --debug)
      DEBUG_OUTPUT=1
      shift
      ;;
    --no-monitor)
      USE_MONITOR=0
      shift
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

# Check prerequisites
if ! command -v qemu-system-aarch64 &> /dev/null; then
  echo -e "${RED}ERROR: qemu-system-aarch64 not found. Install with: brew install qemu${NC}"
  exit 1
fi

# Check build artifacts
if [ ! -f "$BUILD_DIR/hac.elf" ]; then
  echo -e "${YELLOW}WARNING: HAC ELF not found at $BUILD_DIR/hac.elf${NC}"
  echo "Building HAC firmware first..."
  make -C "$PROJECT_ROOT" hac
fi

if [ ! -f "$BUILD_DIR/kernel.image" ]; then
  echo -e "${YELLOW}WARNING: Kernel image not found at $BUILD_DIR/kernel.image${NC}"
  echo "Please run: make -C $PROJECT_ROOT kernel"
  exit 1
fi

echo -e "${GREEN}[QEMU] Starting AgentOS boot stack...${NC}"
echo "[QEMU] HAC EL2:   $BUILD_DIR/hac.elf"
echo "[QEMU] Kernel:    $BUILD_DIR/kernel.image"
echo ""

# Construct QEMU command line
QEMU_ARGS=(
  "qemu-system-aarch64"
  "-m" "3G"                                    # 3 GB RAM (matches iPhone 8/X)
  "-M" "virt"                                  # ARM virt machine (closest to iPhone A11)
  "-cpu" "cortex-a72"                          # A72 is ARM v8-A like A11
  "-smp" "6"                                   # 6 cores (2x A73 + 4x A55 like A11)
  "-nographic"                                 # No graphics
  "-serial" "stdio"                            # Serial output to terminal
  "-device" "virtio-rng-pci"                   # RNG for entropy
  "-device" "virtio-net-pci,netdev=net0"       # Network (optional, for future use)
  "-netdev" "user,id=net0"
)

# HAC loading: for QEMU, we'd need a custom loader; for now, print instructions
if [ "$GDB_SERVER" -eq 1 ]; then
  QEMU_ARGS+=("-gdb" "tcp::1234" "-S")        # GDB server, start halted
  echo -e "${YELLOW}[GDB] Server running on localhost:1234${NC}"
  echo "      Connect with: gdb $BUILD_DIR/hac.elf -ex 'target remote :1234' -ex 'continue'"
fi

if [ "$USE_MONITOR" -eq 0 ]; then
  QEMU_ARGS+=("-monitor" "none")
else
  QEMU_ARGS+=("-monitor" "stdio")
fi

if [ "$DEBUG_OUTPUT" -eq 1 ]; then
  QEMU_ARGS+=("-d" "guest_errors,unimp")
fi

# For now, QEMU virt doesn't natively understand raw HAC EL2 binaries.
# A production setup would require:
#  1. A custom QEMU MMIO wrapper for HAC hypervisor emulation
#  2. Or, HAC linked as a loadable ROM at 0x820000000
#
# For MVP testing, we boot a simple kernel that can be tested with:

echo -e "${YELLOW}[QEMU] Note: Full EL2 HAC emulation requires custom QEMU patches.${NC}"
echo "[QEMU] For basic testing, this QEMU session will boot the Linux kernel image."
echo "[QEMU] HAC HVC calls in the kernel will fail (not emulated)."
echo ""
echo -e "${GREEN}[QEMU] Launching QEMU...${NC}"
echo "[QEMU] QEMU command:"
echo "      ${QEMU_ARGS[@]}"
echo ""
echo "[QEMU] Serial output will appear below. Press Ctrl+A then X to quit QEMU."
echo "========================================================================"

# Launch QEMU
exec "${QEMU_ARGS[@]}" \
  -kernel "$BUILD_DIR/kernel.image" \
  -append "console=ttyAMA0 root=/dev/vda rw"
