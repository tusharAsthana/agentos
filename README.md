# AgentOS — Kali-Based Bare Metal Agent Kernel for iPhone

A complete replacement operating system for fully wiped iPhones (A5–A11 SoCs), placing an AI **Hardware Agent Controller** at the CPU's hypervisor privilege level (EL2) with direct hardware access, while running a Supervisor Agent as PID 1 for human interaction and dynamic app spawning.

## Features

- **Direct Hardware Access**: No app-store sandbox, no Apple attestation. AI agent directly controls hardware primitives (display, touch, storage, sensors, modem).
- **Ephemeral Apps**: User natural-language requests → LLM generates code → compiler → namespace-isolated process → checkpoint/restore via CRIU.
- **Conversation Memory**: SQLite database persists user intent history and cached app snapshots for instant re-instantiation.
- **Full User Autonomy**: Apps have no permissions by default. Supervisor Agent grants capabilities per request.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  EL0  │ Ephemeral App Containers (Linux namespaces)         │
├─────────────────────────────────────────────────────────────┤
│  EL1  │ Linux 6.6 LTS Kernel + Supervisor Agent (PID 1)     │
│       │ • CRIU process snapshots                             │
│       │ • cgroups v2, namespaces                             │
│       │ • HVC proxy drivers (display, touch, storage, etc.)  │
├─────────────────────────────────────────────────────────────┤
│  EL2  │ Hardware Agent Controller (HAC) — bare-metal         │
│       │ • Direct MMIO control of all hardware blocks         │
│       │ • HVC hypercall dispatcher                           │
│       │ • Capability-based access control                    │
│       │ • llama.cpp inference engine for hardware NLU        │
├─────────────────────────────────────────────────────────────┤
│  EL3  │ Minimal Secure Monitor (hands off at boot)           │
└─────────────────────────────────────────────────────────────┘
        Physical iPhone Hardware (A11 Bionic SoC)
```

## Target Devices

- **iPhone 8** (D20) — A11 Bionic, 3 GB RAM
- **iPhone X** (D21/D22) — A11 Bionic, 3 GB RAM

Requires **checkm8 bootrom exploit** (A5–A11 only; A12+ out of scope).

## Build Requirements

- **Homebrew** (macOS)
- `aarch64-elf-gcc` (bare-metal ARM64 cross-compiler)
- `qemu-system-aarch64` (for QEMU testing)
- `git`

```bash
brew install aarch64-elf-gcc qemu
```

## Project Structure

```
agentos/
├── hac/                           # Hardware Agent Controller (EL2)
│   ├── core/     entry.S, main.c, mm.c, uart.c, smmu.c
│   ├── mmio/     a11_mmio.h (hardware register maps)
│   ├── primitives/  primitives.c (DISPLAY_BLIT, TOUCH_POLL, STORAGE_*, etc.)
│   ├── ipc/      ipc.c/h (EL2↔EL1 ring buffers)
│   ├── security/ caps.c/h (per-VMID capability model)
│   ├── inference/ inference.c/h (llama.cpp wrapper)
│   └── monitor/  monitor.S (EL3 secure monitor)
│
├── kernel/                        # Linux 6.6 LTS (EL1)
│   ├── drivers/  hvc_display.c, hvc_touch.c, hvc_storage.c, hvc_hac.c
│   ├── dts/      iphone-a11.dts (device tree)
│   └── Kconfig.hvc
│
├── supervisor/                    # Supervisor Agent (EL1 userspace, PID 1)
│   ├── core/     main.c (init, agent loop, HAC client)
│   ├── ui/       (minimal framebuffer TUI renderer — stub)
│   ├── compiler/ (code generation + tcc integration — stub)
│   └── runtime/  (namespace/CRIU manager — stub)
│
├── scripts/                       # Boot, flash, build helpers
│   ├── flash.sh  (DFU detection, PongoOS upload, HAC flash)
│   └── qemu-dev.sh (QEMU emulation for development)
│
├── Makefile                       # Top-level build orchestration
└── README.md                      # This file
```

## Building

### HAC EL2 Firmware Only

```bash
make hac
# Output: build/hac.elf
```

### Full Stack (HAC + Kernel + Supervisor)

```bash
make all
# Output: build/hac.elf, build/kernel.image, build/supervisor.bin
```

### Clean

```bash
make clean
```

## Flashing to iPhone

Requires **DFU mode** and USB connection.

```bash
make flash
# Uses scripts/flash.sh to:
# 1. Detect iPhone in DFU via libusb
# 2. Run ipwndfu/gaster to trigger checkm8 exploit
# 3. Upload PongoOS
# 4. Flash HAC EL2 + kernel + initrd to NAND
```

## Development: QEMU Emulation

Test the full boot stack without hardware:

```bash
./scripts/qemu-dev.sh
# Launches: qemu-system-aarch64 -m 3G -M virt -cpu cortex-a72 ...
# Boots: HAC → kernel → Supervisor Agent shell
# Debugging: GDB on localhost:1234
```

## Usage

### Supervisor Agent Shell

Once booted, the Supervisor Agent presents an interactive prompt:

```
[SUPERVISOR] Ready. Enter commands:
> turn screen brightness to 30%
[SUPERVISOR] Intent: "display brightness 30%"
[SUPERVISOR] HAC LLM: "HAC_HVC_DISPLAY_BRIGHT 77"
[SUPERVISOR] Exec: hvc call → screen dimmed

> take a photo
[SUPERVISOR] Intent: "capture camera"
[SUPERVISOR] LLM generates app:
  #include <stdio.h>
  int main() { hac_hvc1(HAC_HVC_CAMERA_CAPTURE, buf_pa); ... }
[SUPERVISOR] Compile → spawn namespace → CRIU checkpoint → kill
[SUPERVISOR] Saved app snapshot: intent_hash=0x1a2b3c4d
[SUPERVISOR] Next time: restore snapshot → resume
```

## Architecture Deep Dive

### HAC Hypercall Interface

The kernel and apps communicate with HAC via **HVC #0** (ARM64 hypervisor call):
- **x0** = `HAC_HVC_*` call ID
- **x1–x7** = arguments (hardware-specific)
- **Return**: x0 contains status (0 = success, negative = error)

Example:

```c
// Kernel driver code:
long ret = hac_hvc2(HAC_HVC_DISPLAY_BLIT, framebuffer_pa, region_id);
```

### IPC Ring Buffers

Two-way shared memory (mapped at `0x84E000000`):
- **EL2 → EL1**: HAC pushes hardware events (touch, WiFi RX, etc.)
- **EL1 → EL2**: Kernel sends async hardware requests

Read via `/dev/hac`:

```c
struct hac_ipc_msg event;
read(hac_fd, &event, sizeof(event));  // Blocks until HAC posts event
```

### Capability Model

Per-VMID hardware primitive access control:
- **VMID 0**: Kernel (all capabilities)
- **VMID 1+**: Apps (default: `DISPLAY | TOUCH` only)
- Supervisor Agent grants caps before spawning app:

```c
ioctl(hac_fd, HAC_IOCTL_CAPS, &req);  // Grant camera to app VMID 5
```

## Known Limitations

1. **A12+ Out of Scope**: checkm8 exploit only covers A5–A11. A12 Bionic and later require undocumented exploits or JTAG.
2. **SEP Not Fully Utilized**: A11 lacks the "blackbird" exploit (A8–A10 only). Hardware-bound encryption unavailable; use software key derivation (Argon2id).
3. **Camera ISP Complex**: Full camera support requires reverse-engineering Apple's proprietary ISP. Current: raw sensor fallback.
4. **Boot Semi-Tethered**: Requires PongoOS + HAC reload on every power cycle (stored in NAND partition; full persistence requires NOR flash exploit).

## Future Work

- Persistent secure boot via NOR flash bootloader
- Cellular modem integration (AT commands via USB)
- WiFi/Bluetooth firmware loading
- Full camera ISP driver
- Neural Engine (ANE) inference acceleration
- Thermal management and power states

## References

- **Architecture**: ARM64 ARMv8-A Reference Manual, ARM DDI 0487
- **Bootrom Exploit**: checkm8 (axi0mX), checkra1n, palera1n
- **Hardware**: Apple t8010 Device Tree (from iOS IPSW), theapplewiki.com
- **Linux Drivers**: AsahiLinux/linux kernel, postmarketOS device trees
- **Inference**: llama.cpp with ARM NEON

## License

GPL v2 (kernel drivers) / Custom (HAC firmware, Supervisor Agent)

## Disclaimer

This project is for **research and educational purposes only**. Modifying your iPhone voids the warranty and may cause permanent damage. Use at your own risk.

---

**AgentOS v0.1** — *Full autonomy through direct hardware access.*
