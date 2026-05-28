/*
 * AgentOS HAC — Inference Engine Implementation
 *
 * Integration layer between the HAC and llama.cpp.
 *
 * llama.cpp is compiled separately as a static library:
 *   - ggml.c, llama.cpp with -DGGML_USE_CPU_AARCH64=1 for NEON
 *   - No OS threading: single-threaded batch inference
 *   - malloc/free provided by HAC mm_alloc/mm_free shims
 *   - No file I/O: model loaded directly from NAND via HAC storage primitive
 *
 * The NL→HVC translator uses a structured output grammar that forces
 * the model to emit token sequences of the form:
 *   HVC_ID ARG1 [ARG2 ...]
 * which are then parsed into uint32_t hvc_ids[] / uint64_t hvc_args[].
 */

#include "inference.h"
#include "../core/hac.h"
#include "../primitives/primitives.h"

/* llama.cpp public API (linked as static lib) */
/* #include "llama.h"  -- included during actual build */

/* ---------------------------------------------------------------
 * Internal state
 * --------------------------------------------------------------- */
static infer_state_t g_state = INFER_STATE_UNINITIALIZED;

/* Model buffer in HAC heap (loaded from NAND at init) */
static void    *g_model_buf  = NULL;
static size_t   g_model_size = 0;

/* llama context (opaque pointer to llama_context) */
static void    *g_llama_ctx  = NULL;
static void    *g_llama_model= NULL;

/* ---------------------------------------------------------------
 * Bare-metal malloc shim for llama.cpp
 * llama.cpp calls malloc/free which we redirect to mm_alloc/mm_free
 * via -Wl,--wrap=malloc etc. in the linker flags.
 * --------------------------------------------------------------- */
void *__wrap_malloc(size_t size)  { return mm_alloc(size, 16); }
void  __wrap_free(void *ptr)      { mm_free(ptr); }
void *__wrap_calloc(size_t n, size_t sz) {
    return mm_alloc(n * sz, 16);  /* mm_alloc already zeroes */
}
void *__wrap_realloc(void *ptr, size_t sz) {
    /* Bump allocator: can't resize — alloc new and memcpy */
    void *n = mm_alloc(sz, 16);
    if (ptr && n) {
        /* We don't track allocation sizes, so this is a best-effort copy.
         * Production: replace bump allocator with a proper slab. */
        uint8_t *s = (uint8_t *)ptr, *d = (uint8_t *)n;
        for (size_t i = 0; i < sz; i++) d[i] = s[i];
    }
    mm_free(ptr);
    return n;
}

/* ---------------------------------------------------------------
 * Load model from NAND storage primitive
 * --------------------------------------------------------------- */
static bool load_model_from_nand(void)
{
    hac_uart_puts("[INFER] Loading model from NAND...\r\n");

    /* Query NAND geometry */
    uint64_t total_lba = 0, lba_size = 512;
    /* Simplified: assume 512-byte sectors, model at fixed LBA */

    size_t model_bytes = 1024UL * 1024UL * 900UL;  /* ~900 MB for 1.5B Q4 */
    g_model_buf = mm_alloc(model_bytes, 4096);
    if (!g_model_buf) {
        hac_uart_puts("[INFER] ERROR: not enough memory for model\r\n");
        return false;
    }

    /* Read model blocks from NAND via storage primitive (HAC has CAP_STORAGE) */
    uint64_t lba   = INFERENCE_MODEL_LBA_START;
    uint64_t count = model_bytes / lba_size;
    uint64_t buf   = (uint64_t)(uintptr_t)g_model_buf;

    /* Storage read directly (we're at EL2, no HVC needed) */
    /* prim_storage_read(lba, count, buf) — call internal function */
    /* For stub: mark model as loaded */
    g_model_size = model_bytes;
    (void)total_lba; (void)lba; (void)count; (void)buf;

    hac_uart_puts("[INFER] Model loaded (stub)\r\n");
    return true;
}

/* ---------------------------------------------------------------
 * inference_init — async model load and context setup
 * --------------------------------------------------------------- */
void inference_init(void)
{
    g_state = INFER_STATE_LOADING;

    if (!load_model_from_nand()) {
        g_state = INFER_STATE_ERROR;
        return;
    }

    /*
     * Full llama.cpp initialization would be:
     *
     *   struct llama_model_params mparams = llama_model_default_params();
     *   mparams.n_gpu_layers = 0;   // CPU-only
     *   g_llama_model = llama_load_model_from_buffer(g_model_buf, g_model_size, mparams);
     *
     *   struct llama_context_params cparams = llama_context_default_params();
     *   cparams.n_ctx    = INFERENCE_CONTEXT_LEN;
     *   cparams.n_batch  = 64;
     *   cparams.n_threads= 2;  // 2 perf cores on A11
     *   g_llama_ctx = llama_new_context_with_model(g_llama_model, cparams);
     *
     * Stubbed here — link llama.cpp static library during build.
     */
    g_llama_ctx   = (void *)0x1;  /* non-null stub */
    g_llama_model = (void *)0x1;

    g_state = INFER_STATE_READY;
    hac_uart_puts("[INFER] Inference engine ready\r\n");
}

infer_state_t inference_state(void) { return g_state; }

/* ---------------------------------------------------------------
 * inference_run — synchronous single prompt/response
 * --------------------------------------------------------------- */
int inference_run(const char *prompt, char *out_buf, size_t out_max)
{
    if (g_state != INFER_STATE_READY) return -1;
    if (!prompt || !out_buf || out_max == 0) return -1;

    g_state = INFER_STATE_BUSY;

    /*
     * Full llama.cpp call would be:
     *
     *   llama_tokens tokens[INFERENCE_CONTEXT_LEN];
     *   int n_tokens = llama_tokenize(g_llama_model, prompt, -1, tokens,
     *                                 INFERENCE_CONTEXT_LEN, true, false);
     *   llama_batch batch = llama_batch_get_one(tokens, n_tokens);
     *   llama_decode(g_llama_ctx, batch);
     *
     *   // Greedy sampling loop up to INFERENCE_MAX_TOKENS
     *   int written = 0;
     *   for (int i = 0; i < INFERENCE_MAX_TOKENS && written < (int)out_max - 1; i++) {
     *       llama_token tok = llama_sampler_sample(sampler, g_llama_ctx, -1);
     *       if (tok == llama_token_eos(g_llama_model)) break;
     *       char piece[32];
     *       int n = llama_token_to_piece(g_llama_model, tok, piece, sizeof(piece), 0, false);
     *       memcpy(out_buf + written, piece, n);
     *       written += n;
     *   }
     *   out_buf[written] = '\0';
     *
     * Stub response for testing:
     */
    const char *stub = "HAC_HVC_DISPLAY_BRIGHT 128\n";
    size_t len = 0;
    while (stub[len] && len < out_max - 1) {
        out_buf[len] = stub[len];
        len++;
    }
    out_buf[len] = '\0';

    (void)prompt;
    g_state = INFER_STATE_READY;
    return (int)len;
}

/* ---------------------------------------------------------------
 * inference_nl_to_hvc — parse inference output into HVC calls
 *
 * Grammar the model is constrained to produce:
 *   LINE  ::= HVC_NAME SP ARG*
 *   HVC_NAME ::= "HAC_HVC_" [A-Z_]+
 *   ARG  ::= [0-9]+
 *
 * Example output (multi-line for compound requests):
 *   HAC_HVC_DISPLAY_BRIGHT 77
 *   HAC_HVC_WIFI_CONNECT 0x820f0000 4 0x820f0010 8
 * --------------------------------------------------------------- */

/* Simple string-to-HVC-id lookup table */
static const struct { const char *name; uint32_t id; } hvc_names[] = {
    { "HAC_HVC_VERSION",        0x0000 },
    { "HAC_HVC_DISPLAY_BLIT",   0x0100 },
    { "HAC_HVC_DISPLAY_BLANK",  0x0101 },
    { "HAC_HVC_DISPLAY_BRIGHT", 0x0102 },
    { "HAC_HVC_TOUCH_POLL",     0x0200 },
    { "HAC_HVC_WIFI_TX",        0x0300 },
    { "HAC_HVC_WIFI_RX",        0x0301 },
    { "HAC_HVC_WIFI_CONNECT",   0x0303 },
    { "HAC_HVC_STORAGE_READ",   0x0400 },
    { "HAC_HVC_STORAGE_WRITE",  0x0401 },
    { "HAC_HVC_GPIO_SET",       0x0500 },
    { "HAC_HVC_GPIO_GET",       0x0501 },
    { "HAC_HVC_SENSOR_READ",    0x0600 },
    { "HAC_HVC_CAMERA_CAPTURE", 0x0700 },
    { "HAC_HVC_AUDIO_PLAY",     0x0800 },
    { "HAC_HVC_POWER_SLEEP",    0x0900 },
    { "HAC_HVC_MODEM_AT",       0x0A00 },
    { NULL, 0 }
};

static bool str_starts(const char *s, const char *prefix, const char **rest)
{
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    *rest = s;
    return true;
}

static uint64_t parse_uint(const char *s, const char **end)
{
    uint64_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while ((*s >= '0' && *s <= '9') ||
               (*s >= 'a' && *s <= 'f') ||
               (*s >= 'A' && *s <= 'F')) {
            uint8_t d = (*s >= 'a') ? (*s - 'a' + 10) :
                        (*s >= 'A') ? (*s - 'A' + 10) : (*s - '0');
            v = (v << 4) | d;
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); }
    }
    *end = s;
    return v;
}

int inference_nl_to_hvc(const char *nl_request,
                          uint32_t   *hvc_ids,
                          uint64_t   *hvc_args,
                          int         max_calls)
{
    /* Step 1: run inference to get structured output */
    char out_buf[512];
    if (inference_run(nl_request, out_buf, sizeof(out_buf)) < 0) return 0;

    /* Step 2: parse output lines into HVC calls */
    int n = 0;
    const char *p = out_buf;
    while (*p && n < max_calls) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;

        /* Match HVC name */
        uint32_t matched_id = 0;
        bool found = false;
        for (int i = 0; hvc_names[i].name; i++) {
            const char *rest;
            if (str_starts(p, hvc_names[i].name, &rest)) {
                matched_id = hvc_names[i].id;
                p = rest;
                found = true;
                break;
            }
        }
        if (!found) { /* skip line */ while (*p && *p != '\n') p++; continue; }

        hvc_ids[n]  = matched_id;
        hvc_args[n] = 0;

        /* Parse first argument (primary arg packed in hvc_args[n]) */
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != '\n') {
            const char *end;
            hvc_args[n] = parse_uint(p, &end);
            p = end;
        }
        n++;
        /* Skip to end of line */
        while (*p && *p != '\n') p++;
    }
    return n;
}
