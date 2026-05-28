/*
 * AgentOS Supervisor Agent — PID 1 Main Entry
 *
 * Runs as the sole userspace process (init).
 * Handles all human interaction, spawns/manages apps, and maintains
 * conversation state + app cache in SQLite.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include "agent.h"

static supervisor_state_t g_agent_state;

/* Signal handler for graceful shutdown */
static void sigterm_handler(int sig)
{
  g_agent_state.running = 0;
}

int main(void)
{
    printf("[SUPERVISOR] AgentOS Supervisor Agent v0.1\n");
    printf("[SUPERVISOR] PID 1 (init)\n");

    /* Initialize agent */
    if (agent_init(&g_agent_state) < 0) {
        fprintf(stderr, "[SUPERVISOR] Agent init failed\n");
        return 1;
    }

    /* Setup signal handlers */
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    /* Run main agent loop */
    int ret = agent_run(&g_agent_state);

    /* Cleanup */
    agent_shutdown(&g_agent_state);

    return ret;
}
