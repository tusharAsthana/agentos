/*
 * AgentOS HAC — On-device LLM Inference Engine (EL2)
 *
 * Wraps llama.cpp for bare-metal EL2 use.
 * The HAC model is a small (1–1.5B, Q4_K_M) specialist model trained/fine-tuned
 * to translate natural-language hardware requests into HAC primitive sequences.
 *
 * Example:
 *   Input:  "turn the screen brightness to 30%"
 *   Output: "HAC_HVC_DISPLAY_BRIGHT 77"   (77 = 30% of 255)
 *
 * The Supervisor Agent uses a larger model (3B) for general NLU;
 * this model is the hardware-domain expert living at EL2.
 *
 * Build notes:
 *   llama.cpp must be compiled with:
 *     -DLLAMA_NO_METAL=1 -DLLAMA_NO_ACCELERATE=1
 *     -DGGML_USE_CPU_ONLY=1
 *     -freestanding (for EL2 bare-metal)
 *   ARM NEON acceleration is enabled via -DGGML_USE_CPU_AARCH64=1.
 *   The model file (GGUF) is loaded from a fixed NAND LBA at boot.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Model is stored starting at this LBA on the NAND (partition 4 = /supervisor) */
#define INFERENCE_MODEL_LBA_START   0x400000ULL   /* ~2 GB offset */
#define INFERENCE_MODEL_MAX_BYTES   0x40000000ULL  /* 1 GB max model size */
#define INFERENCE_CONTEXT_LEN       512
#define INFERENCE_MAX_TOKENS        128

typedef enum {
    INFER_STATE_UNINITIALIZED = 0,
    INFER_STATE_LOADING,
    INFER_STATE_READY,
    INFER_STATE_BUSY,
    INFER_STATE_ERROR
} infer_state_t;

/* ---------------------------------------------------------------
 * API
 * --------------------------------------------------------------- */

/* Initialize inference engine; loads model from NAND into HAC heap.
 * This is called asynchronously after the kernel boots to avoid
 * delaying boot by model loading time (~2-5s for 1B Q4 model). */
void inference_init(void);

/* Returns current engine state */
infer_state_t inference_state(void);

/* Run inference synchronously.
 * prompt: null-terminated string
 * out_buf: output buffer for response string
 * out_max: max bytes to write
 * Returns number of bytes written to out_buf, or negative error. */
int inference_run(const char *prompt, char *out_buf, size_t out_max);

/* Translate a natural-language hardware request to an HVC call sequence.
 * Fills hvc_ids[] and hvc_args[] with up to max_calls entries.
 * Returns number of HVC calls to issue. */
int inference_nl_to_hvc(const char *nl_request,
                         uint32_t   *hvc_ids,
                         uint64_t   *hvc_args,
                         int         max_calls);
