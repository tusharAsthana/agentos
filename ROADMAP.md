# AgentOS Roadmap

This roadmap outlines the planned development milestones for AgentOS, from MVP to production-ready bare-metal iPhone operating system.

## Current Status: v0.1 MVP Foundation

**Status**: In Progress  
**Target**: Functional build with HAC EL2, Linux EL1, Supervisor Agent EL0

### Completed

- [x] Repository structure and build system scaffold
- [x] HAC EL2 exception vectors and entry point
- [x] EL3 secure monitor skeleton
- [x] Physical memory allocator
- [x] UART early console
- [x] SMMU/DART initialization skeleton
- [x] HVC primitive dispatcher
- [x] IPC shared-memory ring buffers
- [x] Capability model and audit log
- [x] Linux HVC proxy drivers:
  - Display framebuffer driver
  - Touchscreen input driver
  - Storage block driver
  - `/dev/hac` char device
- [x] iPhone A11 device tree
- [x] Supervisor Agent core loop
- [x] HAC client library
- [x] Intent parser and command handlers
- [x] Flash and QEMU development scripts
- [x] Documentation and contributing guide

### Remaining for v0.1

- [ ] Linux 6.6 LTS kernel source integration
- [ ] Kernel defconfig for AgentOS
- [ ] Supervisor framebuffer UI renderer
- [ ] Supervisor compiler module (`tcc` integration)
- [ ] App runtime manager (namespaces + cgroups)
- [ ] CRIU checkpoint/restore integration
- [ ] QEMU HAC hypercall emulation shim
- [ ] End-to-end build verification

---

## v0.2 — Buildable Developer Preview

**Goal**: `make all` produces bootable artifacts and QEMU runs to Supervisor shell.

### Deliverables

1. **Kernel Build Integration**
   - Vendor Linux 6.6 LTS as submodule or fetch script
   - Apply HVC driver patchset
   - Build `Image.gz` and DTB
   - Generate initramfs with Supervisor Agent

2. **Supervisor UI**
   - Framebuffer renderer using `/dev/fb0`
   - Minimal bitmap font
   - Touch input event handling via `/dev/input/event0`
   - Command shell overlay

3. **QEMU Harness**
   - ARM virt machine boot to Supervisor shell
   - Mock `/dev/hac` driver for HVC calls
   - GDB debugging workflow
   - Automated smoke test script

4. **CI Pipeline**
   - GitHub Actions build matrix
   - C formatting check
   - Basic compile tests
   - Documentation link checks

### Acceptance Criteria

- `make all` succeeds on clean macOS environment
- `./scripts/qemu-dev.sh` boots to Supervisor prompt
- Basic commands (`help`, `brightness 50`, `list`) work
- CI passes on every PR

---

## v0.3 — App Runtime Preview

**Goal**: Natural-language commands spawn ephemeral apps in Linux namespaces.

### Deliverables

1. **Compiler Module**
   - `tcc` embedded compilation
   - Source code generation prompt templates
   - Binary hash cache
   - Build failure reporting

2. **Runtime Manager**
   - PID namespace creation
   - Mount namespace setup
   - cgroups v2 resource limits
   - Capability grant/revoke around app lifecycle
   - Per-app VMID assignment

3. **State Persistence**
   - SQLite app metadata store
   - Intent hash lookup
   - Binary cache restore
   - Conversation history persistence

4. **CRIU Integration**
   - Checkpoint app processes
   - Restore cached app snapshots
   - Garbage collect old checkpoints

### Acceptance Criteria

- Supervisor can compile a simple C app from generated source
- App runs in isolated namespace
- App requests HAC capabilities through Supervisor
- App process can be checkpointed and restored

---

## v0.4 — Hardware Bring-Up

**Goal**: Boot on real iPhone 8/X via checkm8 + PongoOS and reach UART console.

### Deliverables

1. **Boot Chain**
   - PongoOS module for HAC upload
   - EL3 monitor handoff
   - EL2 HAC initialization
   - EL1 Linux kernel launch
   - Device tree handoff

2. **A11 MMIO Validation**
   - UART confirmed
   - AIC interrupt controller setup
   - GPIO read/write
   - I2C bus scan
   - PMU safe state

3. **Storage**
   - ANS2/NVMe initialization
   - Read-only block access first
   - Write support gated behind explicit flag

4. **Safety**
   - Watchdog handling
   - Thermal readout
   - Battery/PMU status
   - Safe power-off path

### Acceptance Criteria

- iPhone enters HAC and prints UART logs
- Linux starts under HAC control
- `/dev/hac` opens from Supervisor
- Storage read works without data corruption

---

## v0.5 — Display and Touch

**Goal**: Real interactive UI on iPhone display and touchscreen.

### Deliverables

1. **Display Bring-Up**
   - MIPI DSI panel init sequence
   - Framebuffer allocation
   - Backlight control
   - Display blit primitive

2. **Touchscreen**
   - SPI touchscreen controller init
   - Multitouch packet decoding
   - Linux input device events
   - Gesture support in Supervisor UI

3. **UI Polish**
   - Font rendering
   - Scrolling command history
   - Status bar (battery, time, network)
   - Error overlay

### Acceptance Criteria

- AgentOS displays prompt on iPhone screen
- Touch keyboard or gesture input works
- Brightness command affects display
- UI remains responsive under load

---

## v0.6 — Networking and Sensors

**Goal**: WiFi/network access and basic sensors functional.

### Deliverables

1. **WiFi**
   - PCIe WiFi device enumeration
   - Firmware loading
   - TX/RX HVC primitives
   - Linux network interface

2. **Sensors**
   - Accelerometer
   - Gyroscope
   - Ambient light sensor
   - Proximity sensor

3. **Networking Stack**
   - DHCP
   - DNS
   - HTTPS for model/API access if needed

### Acceptance Criteria

- Device obtains IP over WiFi
- Supervisor can fetch remote content
- Sensor readings available through HAC primitives

---

## v1.0 — Usable AgentOS

**Goal**: Daily-driver experimental OS for checkm8 iPhones.

### Deliverables

- Stable boot chain
- Display/touch/storage/WiFi support
- Supervisor Agent with persistent memory
- Ephemeral app runtime
- Capability-based hardware access
- Safe update mechanism
- Comprehensive documentation

### Acceptance Criteria

- Fresh install completes with scripted flow
- Device boots into AgentOS reliably
- User can perform common tasks:
  - Text input and shell commands
  - Network access
  - File browsing
  - Camera capture (raw or processed)
  - App generation and caching
- Recovery path documented and tested

---

## Long-Term Goals

### Hardware

- Audio input/output
- Camera ISP support
- Cellular modem
- Bluetooth
- Neural Engine acceleration
- Secure Enclave crypto interface

### Software

- Sandboxed app marketplace
- Declarative hardware permission policies
- Multi-agent orchestration
- Local model inference acceleration
- Visual app builder from natural language
- OTA updates

### Research

- Persistent untethered boot path
- Formal verification of HAC capability checks
- SMMU isolation proofs
- Model-driven hardware policy synthesis

---

## Non-Goals

- Support for A12+ devices without public bootrom exploit
- Bypassing carrier locks or network restrictions
- Circumventing Secure Enclave key material
- Running iOS apps or Apple frameworks
- Maintaining compatibility with Apple services

---

## How to Help

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and contribution guidelines.

Priority areas:

1. Kernel build integration
2. Supervisor UI renderer
3. QEMU HAC emulation
4. A11 hardware register validation
5. Documentation and testing
