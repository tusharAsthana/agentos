#include "agent.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

/* ============== Initialization ============== */

int agent_init(supervisor_state_t *state)
{
  if (!state) return -1;
  
  memset(state, 0, sizeof(*state));
  
  /* Open HAC device */
  int ret = hac_client_open(&state->hac);
  if (ret != HAC_OK) {
    fprintf(stderr, "[AGENT] Failed to open HAC device: %d\n", ret);
    return -1;
  }
  
  /* Initialize session */
  clock_gettime(CLOCK_BOOTTIME, &state->boot_time);
  state->session_id = (uint64_t)time(NULL);
  state->running = 1;
  
  printf("[SUPERVISOR] Agent initialized (VMID=%u, session=%llu)\n",
         state->hac.vmid, state->session_id);
  
  return 0;
}

void agent_shutdown(supervisor_state_t *state)
{
  if (!state) return;
  
  if (state->db_fd >= 0) {
    close(state->db_fd);
    state->db_fd = -1;
  }
  
  hac_client_close(&state->hac);
  printf("[SUPERVISOR] Agent shutdown complete\n");
}

/* ============== Intent Parsing ============== */

/* Simple keyword-based intent classifier (MVP) */
intent_type_t agent_parse_intent(const char *input, intent_t *out)
{
  if (!input || !out) return INTENT_UNKNOWN;
  
  memset(out, 0, sizeof(*out));
  strncpy(out->user_input, input, sizeof(out->user_input) - 1);
  out->timestamp_us = time(NULL) * 1000000ULL;
  
  char lower[512];
  snprintf(lower, sizeof(lower), "%s", input);
  for (char *p = lower; *p; p++) *p = tolower(*p);
  
  /* Keyword matching (MVP) */
  if (strstr(lower, "brightness") || strstr(lower, "bright")) {
    out->type = INTENT_DISPLAY_SET_BRIGHTNESS;
    
    /* Try to extract brightness level (0-100) */
    int level = 50;
    if (sscanf(lower, "%*s %d", &level) == 1 || 
        sscanf(lower, "%*s %*s %d", &level) == 1) {
      if (level < 0) level = 0;
      if (level > 100) level = 100;
    }
    out->parsed_args[0] = (char *)(intptr_t)level;
    out->arg_count = 1;
    return INTENT_DISPLAY_SET_BRIGHTNESS;
  }
  
  if (strstr(lower, "photo") || strstr(lower, "camera") || strstr(lower, "capture")) {
    out->type = INTENT_TAKE_PHOTO;
    out->parsed_args[0] = "/tmp/photo.raw";  /* Default output */
    out->arg_count = 1;
    return INTENT_TAKE_PHOTO;
  }
  
  if (strstr(lower, "unlock") || strstr(lower, "face") || strstr(lower, "auth")) {
    out->type = INTENT_UNLOCK;
    out->arg_count = 0;
    return INTENT_UNLOCK;
  }
  
  if (strstr(lower, "power off") || strstr(lower, "shutdown") || strstr(lower, "halt")) {
    out->type = INTENT_POWER_OFF;
    out->arg_count = 0;
    return INTENT_POWER_OFF;
  }
  
  if (strstr(lower, "reboot") || strstr(lower, "restart")) {
    out->type = INTENT_REBOOT;
    out->arg_count = 0;
    return INTENT_REBOOT;
  }
  
  if (strstr(lower, "list") || strstr(lower, "apps")) {
    out->type = INTENT_LIST_APPS;
    out->arg_count = 0;
    return INTENT_LIST_APPS;
  }
  
  if (strstr(lower, "touch")) {
    out->type = INTENT_TOUCH_ENABLE;
    out->parsed_args[0] = (char *)(intptr_t)(strstr(lower, "enable") ? 1 : 0);
    out->arg_count = 1;
    return INTENT_TOUCH_ENABLE;
  }
  
  out->type = INTENT_UNKNOWN;
  return INTENT_UNKNOWN;
}

/* ============== Intent Execution ============== */

int agent_execute_intent(supervisor_state_t *state, const intent_t *intent)
{
  if (!state || !intent) return -1;
  
  printf("[SUPERVISOR] Intent: %s\n", intent->user_input);
  
  switch (intent->type) {
    case INTENT_DISPLAY_SET_BRIGHTNESS: {
      int level = (intptr_t)intent->parsed_args[0];
      printf("[SUPERVISOR] → Setting brightness to %d%%\n", level);
      return handle_display_brightness(state, level);
    }
    
    case INTENT_TOUCH_ENABLE: {
      int enable = (intptr_t)intent->parsed_args[0];
      printf("[SUPERVISOR] → %s touch input\n", enable ? "Enabling" : "Disabling");
      return handle_touch_enable(state, enable);
    }
    
    case INTENT_TAKE_PHOTO: {
      const char *path = (const char *)intent->parsed_args[0];
      printf("[SUPERVISOR] → Capturing photo to %s\n", path);
      return handle_camera_capture(state, path);
    }
    
    case INTENT_POWER_OFF:
      printf("[SUPERVISOR] → Powering off system\n");
      /* HAC_HVC_POWER_OFF would be issued here */
      return 0;
    
    case INTENT_REBOOT:
      printf("[SUPERVISOR] → Rebooting system\n");
      /* HAC_HVC_REBOOT would be issued here */
      return 0;
    
    case INTENT_LIST_APPS:
      printf("[SUPERVISOR] → Installed apps: (none yet)\n");
      return 0;
    
    case INTENT_UNLOCK:
      printf("[SUPERVISOR] → Face authentication required\n");
      return 0;
    
    case INTENT_UNKNOWN:
    default:
      printf("[SUPERVISOR] → Unknown command. Type 'help' for available commands.\n");
      return -1;
  }
}

/* ============== Handler Implementations ============== */

int handle_display_brightness(supervisor_state_t *state, int level)
{
  if (!state) return -1;
  
  /* Clamp to 0-100 */
  if (level < 0) level = 0;
  if (level > 100) level = 100;
  
  /* Convert to HAC brightness level (0-255) */
  uint8_t hac_level = (level * 255) / 100;
  
  uint64_t args[2] = { hac_level, 0 };
  uint64_t result;
  
  int ret = hac_hvc(&state->hac, 0x1000, args, 2, &result);
  if (ret != HAC_OK) {
    fprintf(stderr, "[SUPERVISOR] Display HVC failed: %d\n", ret);
    return -1;
  }
  
  printf("[SUPERVISOR] Brightness set to %d%%\n", level);
  return 0;
}

int handle_touch_enable(supervisor_state_t *state, int enable)
{
  if (!state) return -1;
  
  uint64_t args[1] = { enable ? 1 : 0 };
  uint64_t result;
  
  int ret = hac_hvc(&state->hac, 0x1001, args, 1, &result);
  if (ret != HAC_OK) {
    fprintf(stderr, "[SUPERVISOR] Touch HVC failed: %d\n", ret);
    return -1;
  }
  
  printf("[SUPERVISOR] Touch input %s\n", enable ? "enabled" : "disabled");
  return 0;
}

int handle_camera_capture(supervisor_state_t *state, const char *output_path)
{
  if (!state || !output_path) return -1;
  
  /* For now, just log the request; full camera capture requires ISP driver */
  printf("[SUPERVISOR] Camera capture requested: %s\n", output_path);
  printf("[SUPERVISOR] Note: Full ISP support requires reverse-engineering Apple's proprietary driver\n");
  
  return 0;
}

int handle_storage_read(supervisor_state_t *state, uint64_t lba, uint32_t count)
{
  if (!state) return -1;
  
  /* Allocate temporary buffer for reading (this would be persistent in real impl) */
  uint64_t *buf = malloc(count * 512);
  if (!buf) return -1;
  
  int ret = hac_storage_read(&state->hac, lba, count, (uint64_t)buf);
  if (ret != HAC_OK) {
    fprintf(stderr, "[SUPERVISOR] Storage read failed: %d\n", ret);
    free(buf);
    return -1;
  }
  
  printf("[SUPERVISOR] Read %u sectors from LBA %llu\n", count, lba);
  free(buf);
  return 0;
}

/* ============== Main Event Loop ============== */

int agent_run(supervisor_state_t *state)
{
  if (!state) return -1;
  
  char line[512];
  
  printf("[SUPERVISOR] Ready. Enter commands:\n");
  printf("[SUPERVISOR] (try: 'brightness 50', 'photo', 'help', 'quit')\n");
  
  while (state->running) {
    printf("> ");
    fflush(stdout);
    
    /* Read user input */
    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }
    
    /* Trim newline */
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
    }
    
    /* Skip empty lines */
    if (strlen(line) == 0) continue;
    
    /* Handle special commands */
    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
      printf("[SUPERVISOR] Shutting down...\n");
      state->running = 0;
      break;
    }
    
    if (strcmp(line, "help") == 0) {
      printf("[SUPERVISOR] Available commands:\n");
      printf("  brightness <0-100>   - Set display brightness\n");
      printf("  photo                - Take a photo\n");
      printf("  unlock               - Request authentication\n");
      printf("  list                 - List installed apps\n");
      printf("  power off            - Power down system\n");
      printf("  reboot               - Restart system\n");
      printf("  quit                 - Exit agent\n");
      continue;
    }
    
    /* Parse and execute intent */
    intent_t intent;
    agent_parse_intent(line, &intent);
    
    int ret = agent_execute_intent(state, &intent);
    if (ret != 0 && intent.type != INTENT_UNKNOWN) {
      printf("[SUPERVISOR] Command failed with code %d\n", ret);
    }
  }
  
  return 0;
}
