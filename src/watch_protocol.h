#ifndef WATCH_PROTOCOL_H
#define WATCH_PROTOCOL_H

#include <stdint.h>

#define MAX_STREAM_SAMPLES 256
#define MAX_POINT_SAMPLES (MAX_STREAM_SAMPLES / 2U)
#define MAX_SPECTRAL_BINS 256
#define MAX_STREAMS 64
#define MAX_METER_CHANNELS 32U
#define MAX_TITLE_SIZE 384
#define MAX_FTABLE_SAMPLES (1U << 30)
#define MAX_GRID_TICKS 256U
#define WATCH_VIEWER_REFRESH_HZ 60U
#define PROT_VERSION 6
#define WATCH_MAGIC 0x57415448
// local address
#define WATCH_VIEWER_ADDRESS "127.0.0.1"
#define WATCH_VIEWER_PORT 48120

typedef uint32_t WATCH_SIGNAL_DOMAIN;

enum {
    WATCH_DOMAIN_OSCILLOSCOPE = 0U,
    WATCH_DOMAIN_SPECTRUM     = 1U,
    WATCH_DOMAIN_CONTROL      = 2U,
    WATCH_DOMAIN_SPECTROGRAM  = 3U,
    WATCH_DOMAIN_FTABLE       = 4U,
    WATCH_DOMAIN_POINT        = 5U,
    WATCH_DOMAIN_METER        = 6U,
};

typedef uint32_t WATCH_SPECTRAL_FORMAT;

enum {
    WATCH_SPECTRAL_POWER = 0U
};

typedef uint32_t WATCH_SPECTRAL_SCALE;
typedef WATCH_SPECTRAL_SCALE WATCH_METER_SCALE;

enum {
    WATCH_SCALE_LINEAR_GAIN  = 0U,
    WATCH_SCALE_LINEAR_POWER = 1U,
    WATCH_SCALE_DECIBEL      = 2U
};

typedef uint32_t WATCH_GRAPH_THEME;

enum {
    WATCH_THEME_LIGHT = 0U,
    WATCH_THEME_DARK  = 1U,
};

typedef enum {
    CONFIG = 0,
    DATA,
    ACK,
    SPECTRAL_DATA,
    SESSION_CLOSE,
    FTABLE_DATA,
    POINT_DATA,
    METER_DATA,
    GRAPH_CLOSE
} MSG_TYPE;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    int64_t sequence;
    uint32_t payload_size;
} WATCH_MSG_HEADER;

typedef struct {
    uint32_t graph_id;
    uint32_t stream_id;
    float sample_rate; // a-rate or k-rate; fractional control rates are common
    uint32_t sample_count;
    float samples[MAX_STREAM_SAMPLES];
} WATCH_MSG_DATA;

typedef struct {
    uint32_t graph_id;
    uint32_t stream_id;
    uint32_t frame_id;
    uint32_t fft_size;
    uint32_t hop_size;
    uint32_t sample_rate;
    WATCH_SPECTRAL_FORMAT format;
    uint32_t bin_offset;
    uint32_t bin_count;
    uint32_t total_bins;
    float bins[MAX_SPECTRAL_BINS];
} WATCH_MSG_SPECTRAL_DATA;

typedef struct {
    uint32_t graph_id;
    uint32_t stream_id;
    uint32_t transfer_id;
    uint32_t sample_offset;
    uint32_t sample_count;
    uint32_t total_samples;
    float samples[MAX_STREAM_SAMPLES];
} WATCH_MSG_FTABLE_DATA;

typedef struct {
    uint32_t graph_id;
    uint32_t stream_id;
    uint32_t channel_count;
    float levels[MAX_METER_CHANNELS];
} WATCH_MSG_METER_DATA;

typedef struct {
    int32_t is_min_auto;
    int32_t is_max_auto;
    float min;
    float max;
} WATCH_RANGE;

typedef struct {
    float win_size;
    uint32_t nxticks;
    uint32_t nyticks;
    WATCH_RANGE yrange;
} WATCH_MSG_TIME_CONFIG;

typedef struct {
    float history_seconds;
    uint32_t nxticks;
    uint32_t nyticks;
    WATCH_SPECTRAL_SCALE scale;
    WATCH_RANGE frequency_range;
    WATCH_RANGE value_range;
} WATCH_MSG_SPECTRAL_CONFIG;

typedef struct {
    uint32_t win_size;
    WATCH_RANGE yrange;
} WATCH_MSG_FTABLE_CONFIG;

typedef struct {
    WATCH_RANGE xrange;
    WATCH_RANGE yrange;
} WATCH_MSG_POINT_CONFIG;

typedef struct {
    uint32_t nchnls;
    uint32_t nyticks;
    WATCH_METER_SCALE scale;
    WATCH_RANGE yrange;
} WATCH_MSG_METER_CONFIG;

typedef union {
    WATCH_MSG_TIME_CONFIG time;
    WATCH_MSG_SPECTRAL_CONFIG spectral;
    WATCH_MSG_FTABLE_CONFIG ftable;
    WATCH_MSG_POINT_CONFIG point;
    WATCH_MSG_METER_CONFIG meter;
} WATCH_MSG_GRAPH_SETTINGS;

typedef struct {
    uint32_t graph_id;
    WATCH_SIGNAL_DOMAIN domain;
    char title[MAX_TITLE_SIZE];
    WATCH_MSG_GRAPH_SETTINGS settings;
    WATCH_GRAPH_THEME theme;
} WATCH_MSG_CONFIG;

typedef struct {
    uint32_t graph_id;
} WATCH_MSG_CONFIG_ACK;

typedef struct {
    uint32_t reserved;
} WATCH_MSG_SESSION_CLOSE;

/*
 * A graph can end before the session that owns it: an instrument reinitialized
 * mid note replaces its graph, and the window of the previous one no longer
 * stands for anything.
 */
typedef struct {
    uint32_t graph_id;
} WATCH_MSG_GRAPH_CLOSE;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_DATA data;
} WATCH_DATA_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_SPECTRAL_DATA data;
} WATCH_SPECTRAL_DATA_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_FTABLE_DATA data;
} WATCH_FTABLE_DATA_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_METER_DATA data;
} WATCH_METER_DATA_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_CONFIG config;
} WATCH_CONFIG_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_CONFIG_ACK ack;
} WATCH_ACK_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_SESSION_CLOSE close;
} WATCH_SESSION_CLOSE_PACKET;

typedef struct {
    WATCH_MSG_HEADER header;
    WATCH_MSG_GRAPH_CLOSE close;
} WATCH_GRAPH_CLOSE_PACKET;

#endif
