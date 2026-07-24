#ifndef WATCH_PROCESS_H
#define WATCH_PROCESS_H

#include <stdint.h>

/*
 * Starts the standalone viewer as a detached process.
 *
 * Resolution order:
 *   1. CSOUND_WATCH_VIEWER
 *   2. an executable next to the watch plugin
 *   3. the standard Risset asset directory
 *   4. watch_viewer[.exe] in PATH
 *
 * Returns zero on success, otherwise a platform error code.
 */
int32_t watch_process_launch_viewer(void);

#endif /* WATCH_PROCESS_H */
