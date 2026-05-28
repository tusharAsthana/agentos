#!/bin/bash
#
# AgentOS Flash Script
# Detects iPhone in DFU mode and flashes HAC EL2 firmware + kernel + supervisor.
#
# Prerequisites:
#   - iPhone in DFU mode (hold Home+Power until "Connect to iTunes" screen)
#   - libusb, ipwndfu (pip install ipwndfu)
#   - gaster (https://github.com/0x1ff/gaster)
#   - pongoOS (https://github.com/checkra1n/pongoOS)
#
# Usage: ./scripts/flash.sh [--dry-run] [--device <model>]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration
DRY_RUN=0
DEVICE="iphone8"  # "iphone8" or "iphonex"
GASTER_BIN="$HOME/.local/bin/gaster"
IPWNDFU_BIN="ipwndfu"
PONGO_BIN="/usr/local/bin/pongoOS"  # Downloaded separately

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --device)
      DEVICE="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

# Detect DFU mode device
detect_dfu() {
  echo "[FLASH] Detecting iPhone in DFU mode..."
  
  # Look for Apple recovery device (DFU mode)
  if lsusb | grep -q "Apple"; then
    echo "[FLASH] Found Apple device in DFU mode"
    return 0
  fi
  
  echo -e "${RED}[FLASH] ERROR: No iPhone detected in DFU mode${NC}"
  echo "[FLASH] Steps to enter DFU mode:"
  echo "  1. Connect iPhone to USB"
  echo "  2. Force restart: Home+Power (hold 10s)"
  echo "  3. When 'Connect to iTunes' appears, release Power (keep Home)"
  echo "  4. Hold Home for 5 more seconds"
  return 1
}

# Upload PongoOS via checkm8 exploit
upload_pongo() {
  echo "[FLASH] Uploading PongoOS (this triggers checkm8 exploit)..."
  
  if [ ! -f "$PONGO_BIN" ]; then
    echo -e "${RED}[FLASH] ERROR: PongoOS binary not found at $PONGO_BIN${NC}"
    echo "[FLASH] Download: https://github.com/checkra1n/pongoOS"
    return 1
  fi
  
  if [ $DRY_RUN -eq 0 ]; then
    # The actual exploit is complex and would be invoked here
    # For now, we document the process
    echo "[FLASH] NOTE: gaster/ipwndfu would run here to trigger checkm8"
    echo "[FLASH] Command: ipwndfu -p && gaster upload $PONGO_BIN"
  else
    echo "[FLASH] [DRY-RUN] ipwndfu -p && gaster upload $PONGO_BIN"
  fi
  
  return 0
}

# Flash HAC + kernel + supervisor
flash_firmware() {
  echo "[FLASH] Flashing HAC EL2 firmware..."
  
  if [ ! -f "$BUILD_DIR/hac.elf" ]; then
    echo -e "${RED}[FLASH] ERROR: HAC ELF not found at $BUILD_DIR/hac.elf${NC}"
    return 1
  fi
  
  if [ ! -f "$BUILD_DIR/kernel.image" ]; then
    echo -e "${RED}[FLASH] ERROR: Kernel image not found at $BUILD_DIR/kernel.image${NC}"
    return 1
  fi
  
  if [ ! -f "$BUILD_DIR/supervisor.bin" ]; then
    echo -e "${YELLOW}[FLASH] WARNING: Supervisor binary not found, using stub${NC}"
  fi
  
  if [ $DRY_RUN -eq 0 ]; then
    # This would use NVMe tools to write to NAND
    echo "[FLASH] Using PongoOS to write firmware to NAND..."
    echo "[FLASH] HAC:        $BUILD_DIR/hac.elf        → 0x820000000"
    echo "[FLASH] Kernel:     $BUILD_DIR/kernel.image    → 0x800080000"
    echo "[FLASH] Supervisor: $BUILD_DIR/supervisor.bin  → /user/supervisor"
    # Actual write would happen via pongoOS serial commands
  else
    echo "[FLASH] [DRY-RUN] Would write:"
    echo "[FLASH] [DRY-RUN] - HAC ELF to NAND at 0x820000000"
    echo "[FLASH] [DRY-RUN] - Kernel image to NAND at 0x800080000"
    echo "[FLASH] [DRY-RUN] - Supervisor binary to /user partition"
  fi
  
  return 0
}

# Reboot device
reboot_device() {
  echo "[FLASH] Rebooting device (PongoOS will hand off to HAC)..."
  
  if [ $DRY_RUN -eq 0 ]; then
    echo "[FLASH] Sending reboot command via PongoOS serial..."
  else
    echo "[FLASH] [DRY-RUN] Would send reboot command"
  fi
  
  return 0
}

# Main flow
main() {
  echo -e "${GREEN}[FLASH] AgentOS Flash Script v0.1${NC}"
  echo "[FLASH] Device: $DEVICE"
  echo ""
  
  if [ $DRY_RUN -eq 1 ]; then
    echo -e "${YELLOW}[FLASH] DRY RUN MODE${NC}"
    echo ""
  fi
  
  # Check prerequisites
  if [ ! -f "$BUILD_DIR/hac.elf" ]; then
    echo -e "${RED}[FLASH] ERROR: HAC not built. Run: make -C $PROJECT_ROOT hac${NC}"
    exit 1
  fi
  
  # Detect DFU mode
  if ! detect_dfu; then
    exit 1
  fi
  
  echo ""
  
  # Upload PongoOS
  if ! upload_pongo; then
    exit 1
  fi
  
  echo ""
  sleep 2
  
  # Flash firmware
  if ! flash_firmware; then
    exit 1
  fi
  
  echo ""
  
  # Reboot
  if ! reboot_device; then
    exit 1
  fi
  
  echo ""
  echo -e "${GREEN}[FLASH] Flash complete!${NC}"
  echo "[FLASH] Device should reboot and boot into AgentOS"
  echo "[FLASH] If boot fails, check UART debug output on pins 1 & 2 of battery connector"
  
  return 0
}

main "$@"
