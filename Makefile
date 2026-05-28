MAKEFLAGS += --no-builtin-rules

# Cross-compilation toolchain
CROSS_COMPILE ?= aarch64-elf-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJDUMP := $(CROSS_COMPILE)objdump

# Output directories
OUT := $(CURDIR)/build
HAC_OUT := $(OUT)/hac
KERNEL_OUT := $(OUT)/kernel
SUPERVISOR_OUT := $(OUT)/supervisor

# Build flags
CFLAGS_HAC := -ffreestanding -march=armv8-a -mstrict-align \
              -O2 -Wall -Wextra -nostdlib -nostdinc \
              -I$(CURDIR)/hac/core -I$(CURDIR)/hac/ipc
CFLAGS_KERNEL := -O2 -Wall -Wextra
CFLAGS_SUPERVISOR := -O2 -Wall -Wextra -fPIC

# Source files
HAC_CORE := hac/core/entry.S hac/core/main.c hac/core/mm.c hac/core/uart.c hac/core/smmu.c
HAC_IPC := hac/ipc/ipc.c
HAC_PRIM := hac/primitives/primitives.c
HAC_SEC := hac/security/caps.c
HAC_INF := hac/inference/inference.c
HAC_MON := hac/monitor/monitor.S

HAC_SRCS := $(HAC_CORE) $(HAC_IPC) $(HAC_PRIM) $(HAC_SEC) $(HAC_INF) $(HAC_MON)
HAC_OBJS := $(patsubst %, $(HAC_OUT)/%.o, $(HAC_SRCS))

.PHONY: all clean hac kernel supervisor

all: $(OUT)/hac.elf $(OUT)/kernel.image $(OUT)/supervisor.bin

$(OUT):
	@mkdir -p $(OUT) $(HAC_OUT) $(KERNEL_OUT) $(SUPERVISOR_OUT)

# ========== HAC BUILD ==========
hac: $(OUT)/hac.elf
	@echo "[HAC] EL2 firmware built: $(OUT)/hac.elf"

$(OUT)/hac.elf: $(HAC_OBJS) | $(OUT)
	@echo "[LD] Linking HAC EL2..."
	@$(LD) -Thac/link.ld -o $@ $(HAC_OBJS) -Map=$(OUT)/hac.map

$(HAC_OUT)/%.c.o: %.c | $(OUT)
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS_HAC) -c -o $@ $<

$(HAC_OUT)/%.S.o: %.S | $(OUT)
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	@$(CC) $(CFLAGS_HAC) -c -o $@ $<

# ========== KERNEL BUILD ==========
kernel: $(OUT)/kernel.image
	@echo "[KERNEL] ARM64 Linux built: $(OUT)/kernel.image"

$(OUT)/kernel.image: kernel/iphone-a11.dts | $(OUT)
	@echo "[KERNEL] Cross-compiling Linux 6.6 LTS (stub target)"
	@echo "TODO: Integrate mainline Linux kernel build"
	@touch $(OUT)/kernel.image

# ========== SUPERVISOR AGENT BUILD ==========
supervisor: $(OUT)/supervisor.bin
	@echo "[SUPERVISOR] Agent built: $(OUT)/supervisor.bin"

$(OUT)/supervisor.bin: supervisor/core/main.c | $(OUT)
	@echo "[CC] supervisor/core/main.c"
	@aarch64-linux-gnu-gcc $(CFLAGS_SUPERVISOR) -c -o $(SUPERVISOR_OUT)/main.o supervisor/core/main.c
	@aarch64-linux-gnu-ld -o $@ $(SUPERVISOR_OUT)/main.o -e main

# ========== CLEANING ==========
clean:
	@echo "[CLEAN] Removing build artifacts"
	@rm -rf $(OUT)

distclean: clean
	@echo "[DISTCLEAN] Removing all generated files"
	@find . -name "*.o" -o -name "*.a" -o -name "*.so" | xargs rm -f

# ========== FLASHING ==========
flash:
	@echo "[FLASH] DFU device detection..."
	@./scripts/flash.sh

help:
	@echo "AgentOS Build System"
	@echo "Usage: make [all|hac|kernel|supervisor|clean|flash|help]"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build HAC, kernel, and supervisor"
	@echo "  hac        - Build EL2 firmware only"
	@echo "  kernel     - Build Linux kernel"
	@echo "  supervisor - Build Supervisor Agent"
	@echo "  clean      - Remove build artifacts"
	@echo "  flash      - Flash to connected iPhone (DFU mode)"
