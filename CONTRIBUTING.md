# Contributing to AgentOS

Thank you for your interest in contributing to AgentOS! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

- Be respectful and inclusive
- Provide constructive feedback
- Focus on the code, not the person
- Report issues professionally

## Getting Started

### Prerequisites

1. **macOS** (primary development platform)
   - Install Homebrew: https://brew.sh
   - Install required tools:
     ```bash
     brew install aarch64-elf-gcc qemu git
     ```

2. **Cross-Compilation Toolchain**
   - `aarch64-elf-gcc` (HAC bare-metal)
   - `aarch64-linux-gnu-gcc` (Supervisor Agent, Linux kernel)
   - `qemu-system-aarch64` (QEMU testing)

### Repository Setup

```bash
git clone https://github.com/tusharAsthana/agentos.git
cd agentos
git checkout main
```

## Development Workflow

### 1. Create a Feature Branch

```bash
git checkout -b feature/your-feature-name
# or for bugfixes:
git checkout -b bugfix/issue-description
```

### 2. Make Changes

- Follow existing code style (C99 standard)
- Add inline comments for non-obvious logic
- Keep functions under 200 lines where possible
- Use meaningful variable names

### 3. Build and Test

```bash
# Build HAC only
make hac

# Build all targets
make all

# Clean build artifacts
make clean

# QEMU testing (requires kernel build)
./scripts/qemu-dev.sh
```

### 4. Commit with Clear Messages

Include co-author line for Oz:

```bash
git commit -m "feat: add new HVC primitive handler

- Implement HAC_HVC_SENSOR_READ handler
- Add capability check for SENSOR access
- Update primitives.c and primitives.h

Co-Authored-By: Oz <oz-agent@warp.dev>"
```

### 5. Push and Create Pull Request

```bash
git push origin feature/your-feature-name
```

Create a PR on GitHub with:
- Clear title and description
- Link to related issues
- Checklist of changes

## Code Style Guidelines

### C Code (HAC, Supervisor, Drivers)

- Use ANSI C comments (`/* */`)
- Indent with spaces (2 spaces for clarity)
- Max line length: 100 characters
- Use `static` for file-local functions
- Error handling: return negative error codes

Example:
```c
/* Hypercall dispatcher for HVC_ID */
static int hvc_dispatch(uint32_t hvc_id, uint64_t *args)
{
  switch (hvc_id) {
    case HAC_HVC_DISPLAY_BLIT:
      return handle_display_blit(args[0], args[1]);
    default:
      return HAC_EINVAL;
  }
}
```

### Assembly (ARM64)

- Use GAS syntax (AT&T)
- Align sections to 4 KB boundaries
- Document register usage
- Include entry point labels

### Device Tree (DTS)

- Use standard FDT format
- Document custom properties
- Include phandle references for inter-node links

## Commit Message Convention

Use conventional commits:

- `feat:` — New feature
- `fix:` — Bug fix
- `docs:` — Documentation only
- `style:` — Code formatting (no functional change)
- `refactor:` — Code refactoring
- `perf:` — Performance improvement
- `test:` — Test additions/modifications
- `chore:` — Build system, dependencies, etc.

Example:
```
feat(hac): implement MMIO write-combining for framebuffer

Optimize display blit performance by using ARM64 DC ZVA and
write-combining on uncached memory regions.

Fixes #42
```

## Areas of Contribution

### High-Priority Tasks

1. **Kernel Integration** — Link Linux 6.6 LTS into build system
2. **UI Module** — Implement framebuffer TUI renderer (supervisor/ui/)
3. **Compiler Module** — Integrate tcc for app compilation (supervisor/compiler/)
4. **Runtime Manager** — Namespace spawning & CRIU (supervisor/runtime/)
5. **Hardware Testing** — Populate A11 MMIO definitions, test HAC on real device

### Medium-Priority

6. WiFi driver integration (hvc_wifi.c)
7. Audio driver (hvc_audio.c)
8. Modem AT command interface
9. Camera ISP reverse-engineering & driver
10. Neural Engine inference acceleration

### Low-Priority (Future)

11. Persistent NOR flash bootloader
12. Thermal management
13. Power state optimization
14. Bluetooth driver

## Testing

### Unit Tests

Create test files in `tests/` directory:

```bash
tests/
├── hac/
│   ├── test_mm.c
│   ├── test_ipc.c
│   └── test_caps.c
├── supervisor/
│   ├── test_agent.c
│   └── test_hac_client.c
└── Makefile
```

Run tests:
```bash
make test
```

### Integration Testing

1. Build full stack: `make all`
2. Run QEMU: `./scripts/qemu-dev.sh`
3. Verify boot output on UART console
4. Test HAC HVC calls from kernel
5. Test Supervisor Agent intent parsing

### Hardware Testing (iPhone 8/X)

1. Enter DFU mode on device
2. Run: `./scripts/flash.sh --dry-run` (verify first)
3. Run: `./scripts/flash.sh` (flash)
4. Monitor UART debug output
5. Test Supervisor Agent commands

## Documentation

- Update README.md for user-facing changes
- Add code comments for complex algorithms
- Document hardware-specific quirks in comments
- Update CONTRIBUTING.md for process changes

## Pull Request Review Process

1. **Automated Checks**
   - GitHub Actions CI runs linter & build
   - All tests must pass

2. **Code Review**
   - At least one approval required
   - Address comments or request re-review
   - Keep conversations professional

3. **Merge**
   - Squash commits if multiple fixes for same issue
   - Use linear history where possible
   - Delete feature branch after merge

## Reporting Issues

Create GitHub issues for:

- Bugs (include reproduction steps)
- Feature requests (include use cases)
- Documentation improvements
- Build/test failures

### Issue Template

```markdown
## Description
[Clear description of issue]

## Steps to Reproduce
1. ...
2. ...
3. ...

## Expected Behavior
[What should happen]

## Actual Behavior
[What actually happens]

## Environment
- macOS version: [e.g., 13.4]
- aarch64-elf-gcc version: [output of `aarch64-elf-gcc --version`]
- Commit hash: [output of `git rev-parse HEAD`]

## Additional Context
[Any other relevant information]
```

## Learning Resources

- **ARM64 ISA**: ARM DDI 0487 (ARMv8-A Reference Manual)
- **Hypervisor Calls**: ARMv8 HVC/SMC guide
- **Linux Drivers**: Linux Kernel Driver Development book
- **Device Trees**: Devicetree Specification v0.3
- **Checkm8 Exploit**: checkra1n/pongoOS source code

## Project Roadmap

See [ROADMAP.md](ROADMAP.md) for long-term goals and milestones.

## Questions?

- Open a GitHub discussion
- Check existing issues/PRs
- Review code comments in relevant files
- Contact maintainers

---

**Thank you for contributing to AgentOS!** 🚀
