#ifndef SUPERVISOR_AGENT_H
#define SUPERVISOR_AGENT_H

#include <stdint.h>
#include <time.h>
#include "hac_client.h"

/* ============== Agent State ============== */

typedef struct {
  hac_client_t hac;           /* HAC device connection */
  int db_fd;                  /* SQLite database FD */
  uint64_t session_id;        /* Conversation session ID */
  int running;                /* Main loop control */
  struct timespec boot_time;  /* Time of agent start */
} supervisor_state_t;

/* ============== Intent Types ============== */

typedef enum {
  INTENT_DISPLAY_SET_BRIGHTNESS,
  INTENT_TOUCH_ENABLE,
  INTENT_TAKE_PHOTO,
  INTENT_UNLOCK,
  INTENT_OPEN_APP,
  INTENT_POWER_OFF,
  INTENT_REBOOT,
  INTENT_LIST_APPS,
  INTENT_UNKNOWN,
} intent_type_t;

typedef struct {
  intent_type_t type;
  char user_input[512];       /* Original user command */
  uint64_t timestamp_us;      /* Timestamp of request */
  uint32_t target_vmid;       /* VMID of target app/system */
  char *parsed_args[8];       /* Parsed command arguments */
  int arg_count;
} intent_t;

/* ============== Agent Functions ============== */

/* Initialize supervisor agent state */
int agent_init(supervisor_state_t *state);

/* Shutdown agent cleanly */
void agent_shutdown(supervisor_state_t *state);

/* Main event loop (runs until state->running == 0) */
int agent_run(supervisor_state_t *state);

/* Parse user input into intent */
intent_type_t agent_parse_intent(const char *input, intent_t *out);

/* Execute intent (dispatch to appropriate handler) */
int agent_execute_intent(supervisor_state_t *state, const intent_t *intent);

/* Display handler */
int handle_display_brightness(supervisor_state_t *state, int level);

/* Touch handler */
int handle_touch_enable(supervisor_state_t *state, int enable);

/* Camera handler */
int handle_camera_capture(supervisor_state_t *state, const char *output_path);

/* Storage handler */
int handle_storage_read(supervisor_state_t *state, uint64_t lba, uint32_t count);

#endif /* SUPERVISOR_AGENT_H */
