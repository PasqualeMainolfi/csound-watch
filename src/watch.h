#ifndef __WATCH_H
#define __WATCH_H

#include "watch_protocol.h"
#include "watch_socket.h"

#include <csdl.h>
#include <stdatomic.h>
#include <float.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_QUEUE_SIZE 50
#define REGISTRY_SIZE 32
#define RANGE_AUTO FLT_MAX
#define INVALID_HANDLE 0U
#define MAX_HANDLE_ID 0x00FFFFFFU
#define REGISTRY_NAME "watch-manager"
#define WATCH_CONFIG_RETRY_MS 250U
#define WATCH_SENDER_SLEEP_MS 2U
#define WATCH_PACKETS_PER_STREAM_PASS 8U
#define WATCH_VIEWER_PROBE_DELAY_MS 150U
#define WATCH_VIEWER_RELAUNCH_DELAY_MS 2000U
#define WATCH_SESSION_CLOSE_REPETITIONS 3U


// TODO: plot f-table (statically)
// TODO: control graph
// CHECK: maybe min/max auto is a wrong idea cause min/max is local


typedef enum {
    GRAPH_REGISTERED = 0,
    GRAPH_REGISTRY_FULL,
    GRAPH_REGISTRY_INCONSISTENT,
    GRAPH_HANDLE_UNAVAILABLE
} GRAPH_REGISTER_RESULT;

typedef union {
    WATCH_MSG_HEADER header;
    WATCH_DATA_PACKET time;
    WATCH_SPECTRAL_DATA_PACKET spectral;
    WATCH_FTABLE_DATA_PACKET ftable;
} WATCH_STREAM_PACKET;

typedef struct {
    uint32_t graph_id;
    uint32_t stream_id;
    uint32_t sample_rate;
    WATCH_STREAM_PACKET slots[MAX_QUEUE_SIZE];
    uint32_t pending_time_samples;
    int64_t pending_time_sequence;
    atomic_uint write_pos;
    atomic_uint read_pos;
    atomic_uint_fast64_t dropped_samples;
    float *ftable_samples;
    uint32_t ftable_total_samples;
    uint32_t ftable_next_sample;
    uint32_t ftable_transfer_id;
    int64_t ftable_sequence;
} WATCH_STREAM;

typedef struct {
    bool is_config_acked;
    bool destroy_requested;
    uint64_t last_config_send_time;
    WATCH_CONFIG_PACKET data_config;
    WATCH_STREAM **streams;
    uint32_t stream_count;
} WATCH_GRAPH;

typedef struct {
    CSOUND *csound; // for lock and mutex
    WATCH_GRAPH *graphs[REGISTRY_SIZE];
    uint32_t graph_count;
    uint32_t graph_capacity;
    uint32_t next_graph_id;
    void *registry_mutex;
    void *sender_thread;
    watch_socket_t socket_fd;
    atomic_bool running;

    uint64_t viewer_probe_started_ms;
    uint64_t viewer_last_launch_ms;
} WATCH_MANAGER;

typedef struct {
    OPDS h;
    // outputs
    MYFLT *handle; // 0 if the graph could not be created
    // inputs
    MYFLT *win_size;   // time interval to show
    MYFLT *nxticks;    // i-optional
    MYFLT *nyticks;    // i-optional
    MYFLT *ymin;       // i-optional
    MYFLT *ymax;       // i-optional
    STRINGDAT *title;  // S-optional
} WATCH_CREATE_TIME;

typedef struct {
    OPDS h;
    // outputs
    MYFLT *handle; // 0 if the graph could not be created
    // inputs
    MYFLT *min_frequency; // i-optional; automatic when omitted
    MYFLT *max_frequency; // i-optional; automatic when omitted
    MYFLT *min_value;     // i-optional; expressed in the selected scale
    MYFLT *max_value;     // i-optional; expressed in the selected scale
    MYFLT *scale;         // i-optional; WATCH_SCALE_LINEAR_GAIN by default
    MYFLT *nxticks;       // i-optional
    MYFLT *nyticks;       // i-optional
    STRINGDAT *title;     // S-optional
} WATCH_CREATE_SPECTRAL;

typedef struct {
    OPDS h;
    // outputs
    MYFLT *handle; // 0 if the graph could not be created
    // inputs
    MYFLT *history_seconds;
    MYFLT *min_frequency; // i-optional; automatic when omitted
    MYFLT *max_frequency; // i-optional; automatic when omitted
    MYFLT *min_value;     // i-optional; expressed in the selected scale
    MYFLT *max_value;     // i-optional; expressed in the selected scale
    MYFLT *scale;         // i-optional; WATCH_SCALE_LINEAR_GAIN by default
    MYFLT *nxticks;       // i-optional
    MYFLT *nyticks;       // i-optional
    STRINGDAT *title;     // S-optional
} WATCH_CREATE_SPECTROGRAM;

typedef struct {
    OPDS h;
    // inputs
    MYFLT *handle;
    MYFLT *signal;
    // private
    WATCH_STREAM *stream;
} WATCH_ADD_TIME;

typedef struct {
    OPDS h;
    // inputs
    MYFLT *handle;
    PVSDAT *signal;
    // private
    WATCH_STREAM *stream;
    uint32_t last_frame_id;
    uint32_t fft_size;
    uint32_t hop_size;
    int32_t pvs_format;
} WATCH_ADD_SPECTRAL;

typedef struct {
    OPDS h;
    // inputs
    MYFLT *ftable;
    MYFLT *ymin;       // i-optional
    MYFLT *ymax;       // i-optional
    MYFLT *theme;      // i-optional
    STRINGDAT *title;  // S-optional
    // private
    WATCH_STREAM *stream;
    uint32_t graph_id;
} WATCH_FTABLE;

typedef struct {
    OPDS h;
    // inputs
    MYFLT *ftable;
    MYFLT *ymin;
    MYFLT *ymax;
    STRINGDAT *title;
    // private
    WATCH_STREAM *stream;
    uint32_t graph_id;
} WATCH_FTABLE_TITLE;

typedef struct {
    OPDS h;
    // inputs
    MYFLT *handle;
    MYFLT *theme;
} WATCH_THEME;


// INTERFACE

int32_t watch_create_scope(CSOUND *csound, WATCH_CREATE_TIME *p); // i-time
int32_t watch_create_spectrum(CSOUND *csound, WATCH_CREATE_SPECTRAL *p); // i-time
int32_t watch_create_spectrogram(CSOUND *csound, WATCH_CREATE_SPECTROGRAM *p); // i-time
int32_t watch_ftable(CSOUND *csound, WATCH_FTABLE *p); // i-time

int32_t watch_theme(CSOUND *csound, WATCH_THEME *p); // i-time

int32_t watch_add_a(CSOUND *csound, WATCH_ADD_TIME *p); // k-time
int32_t watch_add_f(CSOUND *csound, WATCH_ADD_SPECTRAL *p); // k-time



#endif
