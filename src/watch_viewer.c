#include "SDL3/SDL_blendmode.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_rect.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "watch_font_data.h"
#include "watch_protocol.h"
#include "watch_socket.h"


#define INITIAL_WIDTH 600
#define INITIAL_HEIGTH 400
#define MINIMUM_WIDTH 360
#define MINIMUM_HEIGTH 260
#define MAX_VIEWER_GRAPHS 32
#define MAX_PACKETS_PER_FRAME 4096
#define DEFAULT_X_TICKS 10
#define DEFAULT_Y_TICKS 8
#define VIEWER_REFRESH_HZ WATCH_VIEWER_REFRESH_HZ
#define NANOSECONDS_PER_SECOND 1000000000ULL
#define VIEWER_SHUTDOWN_DELAY_MS 500U
#define SCOPE_RENDER_LATENCY_MS 50U
#define RENDER_SUPERSAMPLE_SCALE 2
#define WAVEFORM_LINE_RADIUS 1.5f
#define SPECTRAL_DB_FLOOR -160.0f
#define MAX_SPECTRAL_TOTAL_BINS 1048576U
#define MAX_SPECTROGRAM_TEXELS 16777216ULL
#define MAX_FTABLE_RENDER_BUCKETS 16384U
/*
 * A point graph keeps a short trail behind the moving point. The length is
 * expressed in seconds so that the same constant produces the same look at any
 * control rate; the sample count is derived from the rate carried by each
 * packet. Below one display frame the trail collapses to a single dot and the
 * direction of the movement is lost; above a third of a second fast paths
 * smear into a blob.
 */
#define POINT_TRAIL_SECONDS 0.1f
#define POINT_TRAIL_MAX_SAMPLES 16384U
#define POINT_TRAIL_MIN_ALPHA 40.0f
#define POINT_HEAD_SIZE 5.0f
#define POINT_TICKS 8U
/*
 * A meter answers "how loud is it now", so the bar carries the level it was
 * given and nothing else: each packet already holds the loudest value of one
 * viewer frame, so smoothing it again would only make the bar trail the sound.
 * The peak mark is what holds, long enough to be read, and then falls towards
 * the bottom of the axis, which is what silence means in any unit.
 */
#define METER_BAR_FILL 0.62f
#define METER_PEAK_HOLD_SECONDS 1.5f
#define METER_PEAK_RELEASE_SECONDS 1.0f
#define METER_PEAK_THICKNESS 3.0f
#define METER_PEAK_HUE 18.0f
#define METER_PEAK_LIGHT_MAXIMUM 0.05f
#define METER_PEAK_DARK_MINIMUM 0.55f
#define METER_HEADROOM_ALPHA 28U
#define STREAM_COLOR_LIGHT_MAXIMUM 0.155f
#define STREAM_COLOR_DARK_MINIMUM 0.30f
#define METER_DEFAULT_DB_SPAN 60.0f
#define CONFIG_FAILURE_LOG_INTERVAL_NS 2000000000ULL
#define AXIS_FONT_SIZE 15.0f
#define TICK_FONT_SIZE 11.0f
#define TICK_MARK_LENGTH 5.0f
#define TICK_LABEL_GAP 3.0f
#define AXIS_TICK_LABEL_GAP 6.0f
#define PLOT_OUTER_PADDING 8.0f
#define MAX_NUMBERED_TICK_INTERVALS 10U
#define TICK_TEXT_SIZE 32U


typedef union {
    WATCH_MSG_HEADER header;
    WATCH_CONFIG_PACKET config;
    WATCH_DATA_PACKET data;
    WATCH_SPECTRAL_DATA_PACKET spectral;
    WATCH_FTABLE_DATA_PACKET ftable;
    WATCH_ACK_PACKET ack;
    WATCH_SESSION_CLOSE_PACKET close;
    WATCH_GRAPH_CLOSE_PACKET graph_close;
    WATCH_METER_DATA_PACKET meter;
} WATCH_INCOMING_PACKET;

typedef struct {
    float left;
    float right;
    float top;
    float bottom;
    float plot_width;
    float plot_heigth;
    float x_center;
    float y_center;
} PLOT_AREA;

typedef struct {
    SDL_Texture *texture;
    float width;
    float height;
} TEXT_LABEL;

typedef uint32_t TICK_VALUE_FORMAT;

enum {
    TICK_FORMAT_NUMBER = 0U,
    TICK_FORMAT_TIME,
    TICK_FORMAT_FREQUENCY,
    TICK_FORMAT_CHANNEL
};

typedef struct {
    TEXT_LABEL label;
    uint32_t grid_index;
} TICK_LABEL_ENTRY;

typedef struct {
    TICK_LABEL_ENTRY *entries;
    uint32_t count;
    uint32_t intervals;
} TICK_AXIS_LABELS;

typedef struct {
    TICK_AXIS_LABELS x;
    TICK_AXIS_LABELS y;
    float xmin;
    float xmax;
    float ymin;
    float ymax;
    float density;
    TICK_VALUE_FORMAT x_format;
    TICK_VALUE_FORMAT y_format;
    bool ready;
} TICK_LABEL_CACHE;

typedef struct {
    float *buffer;
    SDL_FPoint *plot_points;
    uint32_t write_index;
    uint32_t capacity;
    uint32_t window_samples;
    uint32_t valid_samples;
    float ymin;
    float ymax;
    float sample_rate;
    uint64_t total_samples;
    int64_t next_sequence;
    bool has_sequence;
    bool unsupported;
} STREAM_TIME_BUFFER;

typedef struct {
    float *assembly_bins;
    uint8_t *received_bins;
    float *display_bins;
    SDL_FPoint *plot_points;
    SDL_Texture *history_texture;
    uint8_t *color_column;
    int64_t frame_sequence;
    uint32_t frame_id;
    uint32_t fft_size;
    uint32_t hop_size;
    uint32_t sample_rate;
    uint32_t total_bins;
    uint32_t received_count;
    uint32_t history_capacity;
    uint32_t history_write_index;
    uint32_t valid_history_frames;
    bool has_frame;
    bool assembling;
    bool display_ready;
    bool unsupported;
} STREAM_SPECTRAL_BUFFER;

typedef struct {
    float *samples;
    uint8_t *received_chunks;
    float *envelope_minimums;
    float *envelope_maximums;
    uint32_t transfer_id;
    uint32_t total_samples;
    uint32_t total_chunks;
    uint32_t received_count;
    uint32_t envelope_count;
    float data_minimum;
    float data_maximum;
    bool has_range;
    bool assembling;
    bool display_ready;
    bool unsupported;
} STREAM_FTABLE_BUFFER;

typedef struct {
    float *coordinates; // two floats per point, oldest to newest through the ring
    SDL_FPoint *plot_points;
    uint32_t capacity;
    uint32_t write_index;
    uint32_t valid_points;
    float sample_rate;
    bool unsupported;
} STREAM_POINT_BUFFER;

/*
 * A meter graph shows one bar per channel and no history, so a single buffer
 * per graph is enough: the levels are indexed by channel, not by stream.
 */
typedef struct {
    float level[MAX_METER_CHANNELS];
    float peak[MAX_METER_CHANNELS];
    uint64_t peak_time_ns[MAX_METER_CHANNELS];
    uint64_t last_frame_ns;
    bool has_frame;
} STREAM_METER_BUFFER;

typedef struct {
    bool ready;
    WATCH_MSG_CONFIG config;
    watch_endpoint_t sender;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *render_target;
    SDL_WindowID window_id;
    int width;
    int heigth;
    int render_target_width;
    int render_target_heigth;
    float axis_label_density;
    TEXT_LABEL x_axis_label;
    TEXT_LABEL y_axis_label;
    TICK_LABEL_CACHE tick_labels;
    STREAM_TIME_BUFFER *scope_streams;
    STREAM_SPECTRAL_BUFFER *spectral_streams;
    STREAM_FTABLE_BUFFER *ftable_streams;
    STREAM_POINT_BUFFER *point_streams;
    STREAM_METER_BUFFER *meter_stream;
    double scope_display_end_sample;
    uint64_t scope_last_render_ns;
    bool scope_display_started;
} VIEW_GRAPH;

static SDL_Color stream_colors_light[MAX_STREAMS];
static SDL_Color stream_colors_dark[MAX_STREAMS];
static bool stream_colors_ready = false;

static uint8_t themed_component(WATCH_GRAPH_THEME theme, uint8_t light_component) {
    return theme == WATCH_THEME_DARK ? (uint8_t) (255U - light_component) : light_component;
}

static void set_themed_draw_color(
    SDL_Renderer *renderer,
    WATCH_GRAPH_THEME theme,
    uint8_t light_component,
    uint8_t alpha
) {
    uint8_t component = themed_component(theme, light_component);
    SDL_SetRenderDrawColor(renderer, component, component, component, alpha);
}

static SDL_FColor hsv_color(float hue, float saturation, float value) {
    float sector = hue / 60.0f;
    float chroma = value * saturation;
    float x = chroma * (1.0f - fabsf(fmodf(sector, 2.0f) - 1.0f));
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;

    if (sector < 1.0f) {
        red = chroma;
        green = x;
    } else if (sector < 2.0f) {
        red = x;
        green = chroma;
    } else if (sector < 3.0f) {
        green = chroma;
        blue = x;
    } else if (sector < 4.0f) {
        green = x;
        blue = chroma;
    } else if (sector < 5.0f) {
        red = x;
        blue = chroma;
    } else {
        red = chroma;
        blue = x;
    }

    float minimum = value - chroma;
    SDL_FColor color = {
        .r = red + minimum,
        .g = green + minimum,
        .b = blue + minimum,
        .a = 1.0f
    };
    return color;
}

static float linear_color_component(float component) {
    return component <= 0.04045f ? component / 12.92f : powf((component + 0.055f) / 1.055f, 2.4f);
}

static float color_luminance(SDL_FColor color) {
    return 0.2126f * linear_color_component(color.r) + 0.7152f * linear_color_component(color.g) + 0.0722f * linear_color_component(color.b);
}

static SDL_Color byte_color(SDL_FColor color) {
    SDL_Color result = {
        .r = (uint8_t) lrintf(fminf(fmaxf(color.r, 0.0f), 1.0f) * 255.0f),
        .g = (uint8_t) lrintf(fminf(fmaxf(color.g, 0.0f), 1.0f) * 255.0f),
        .b = (uint8_t) lrintf(fminf(fmaxf(color.b, 0.0f), 1.0f) * 255.0f),
        .a = 255U
    };
    return result;
}

/*
 * The same hue is darkened until it stands out on the light background and
 * brightened until it stands out on the dark one, so a color keeps its identity
 * between themes while staying readable in both. The two limits are arguments
 * because what a color is drawn against differs: a curve sits on the
 * background, while a peak mark sits on top of a bar and has to separate itself
 * from that as well.
 */
static void themed_color_pair(
    SDL_FColor base,
    float light_maximum,
    float dark_minimum,
    SDL_Color *light_color,
    SDL_Color *dark_color
) {
    SDL_FColor light = base;
    SDL_FColor dark = base;

    while (color_luminance(light) > light_maximum) {
        light.r *= 0.94f;
        light.g *= 0.94f;
        light.b *= 0.94f;
    }
    while (color_luminance(dark) < dark_minimum) {
        dark.r += (1.0f - dark.r) * 0.08f;
        dark.g += (1.0f - dark.g) * 0.08f;
        dark.b += (1.0f - dark.b) * 0.08f;
    }

    *light_color = byte_color(light);
    *dark_color = byte_color(dark);
}

static void initialize_stream_colors(void) {
    if (stream_colors_ready) {
        return;
    }

    for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
        float hue = fmodf(210.0f + (float) i * 137.507764f, 360.0f);
        float saturation = 0.68f + 0.08f * (float) (i % 4U);
        float value = 0.86f + 0.04f * (float) ((i / 4U) % 3U);
        themed_color_pair(
            hsv_color(hue, saturation, value),
            STREAM_COLOR_LIGHT_MAXIMUM,
            STREAM_COLOR_DARK_MINIMUM,
            &stream_colors_light[i],
            &stream_colors_dark[i]);
    }
    stream_colors_ready = true;
}

static void set_stream_draw_color(SDL_Renderer *renderer, WATCH_GRAPH_THEME theme, uint32_t stream_index) {
    initialize_stream_colors();
    const SDL_Color *palette = theme == WATCH_THEME_DARK ? stream_colors_dark : stream_colors_light;
    SDL_Color color = palette[stream_index % MAX_STREAMS];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

/*
 * The peak mark rides on the bar it belongs to, and while the signal is rising
 * the two coincide: a shade of the bar color would be invisible exactly when
 * the peak is being set. It gets a hue of its own, opposite the bars, which
 * start at 210 degrees, and a lightness pushed past theirs so that hue is not
 * the only thing telling them apart.
 */
static SDL_Color meter_peak_color(WATCH_GRAPH_THEME theme) {
    static SDL_Color peak_light;
    static SDL_Color peak_dark;
    static bool peak_colors_ready = false;

    if (!peak_colors_ready) {
        themed_color_pair(
            hsv_color(METER_PEAK_HUE, 0.92f, 0.95f),
            METER_PEAK_LIGHT_MAXIMUM,
            METER_PEAK_DARK_MINIMUM,
            &peak_light,
            &peak_dark);
        peak_colors_ready = true;
    }

    return theme == WATCH_THEME_DARK ? peak_dark : peak_light;
}

static void set_meter_peak_color(SDL_Renderer *renderer, WATCH_GRAPH_THEME theme) {
    SDL_Color color = meter_peak_color(theme);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// the same color as the peak, laid under everything as a tint rather than a mark
static void set_meter_headroom_color(SDL_Renderer *renderer, WATCH_GRAPH_THEME theme) {
    SDL_Color color = meter_peak_color(theme);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, METER_HEADROOM_ALPHA);
}

static float graph_pixel_density(const VIEW_GRAPH *graph);

static float maximum_tick_width(const TICK_AXIS_LABELS *axis) {
    float maximum = 0.0f;
    for (uint32_t i = 0; i < axis->count; i++) {
        maximum = fmaxf(maximum, axis->entries[i].label.width);
    }
    return maximum;
}

static float maximum_tick_height(const TICK_AXIS_LABELS *axis) {
    float maximum = 0.0f;
    for (uint32_t i = 0; i < axis->count; i++) {
        maximum = fmaxf(maximum, axis->entries[i].label.height);
    }
    return maximum;
}

static bool has_opened_graph(const VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS]) {
    for (uint32_t i = 0; i < MAX_VIEWER_GRAPHS; i++) {
        if (graphs[i].ready) {
            return true;
        }
    }
    return false;
}

static PLOT_AREA get_plot_area(const VIEW_GRAPH *graph) {
    float density = graph_pixel_density(graph);
    float outer_padding = PLOT_OUTER_PADDING * density;
    float tick_gap = TICK_LABEL_GAP * density;
    float axis_gap = AXIS_TICK_LABEL_GAP * density;
    float mark_length = TICK_MARK_LENGTH * density;
    float x_tick_height = maximum_tick_height(&graph->tick_labels.x);
    float x_tick_width = maximum_tick_width(&graph->tick_labels.x);
    float y_tick_height = maximum_tick_height(&graph->tick_labels.y);
    float y_tick_width = maximum_tick_width(&graph->tick_labels.y);

    PLOT_AREA p;
    p.left = outer_padding + graph->y_axis_label.height + axis_gap + y_tick_width + tick_gap + mark_length;
    p.right = (float) graph->width - fmaxf(outer_padding, x_tick_width * 0.5f + outer_padding);
    p.top = fmaxf(outer_padding, y_tick_height * 0.5f + outer_padding);
    p.bottom =
        (float) graph->heigth
        - outer_padding
        - graph->x_axis_label.height
        - axis_gap
        - x_tick_height
        - tick_gap
        - mark_length;
    p.plot_width = p.right - p.left;
    p.plot_heigth = p.bottom - p.top;
    p.x_center = p.left + p.plot_width * 0.5f;
    p.y_center = p.top + p.plot_heigth * 0.5f;
    return p;
}

static SDL_Rect get_plot_clip_rect(const PLOT_AREA *plot) {
    int left = (int) floorf(plot->left);
    int top = (int) floorf(plot->top);
    int right = (int) floorf(plot->right) + 1;
    int bottom = (int) floorf(plot->bottom) + 1;
    return (SDL_Rect) {
        .x = left,
        .y = top,
        .w = right - left,
        .h = bottom - top
    };
}

static const char *x_axis_label(const WATCH_MSG_CONFIG *config) {
    if (config->domain == WATCH_DOMAIN_SPECTRUM) {
        return "Frequency (Hz)";
    }
    if (config->domain == WATCH_DOMAIN_FTABLE) {
        return "Table index";
    }
    if (config->domain == WATCH_DOMAIN_POINT) {
        return "x";
    }
    if (config->domain == WATCH_DOMAIN_METER) {
        return "Channel";
    }
    return "Time (s)";
}

static const char *y_axis_label(const WATCH_MSG_CONFIG *config) {
    switch (config->domain) {
        case WATCH_DOMAIN_OSCILLOSCOPE:
            return "Amplitude";
        case WATCH_DOMAIN_CONTROL:
            return "Value";
        case WATCH_DOMAIN_SPECTRUM:
            switch (config->settings.spectral.scale) {
                case WATCH_SCALE_LINEAR_POWER:
                    return "Power";
                case WATCH_SCALE_DECIBEL:
                    return "Level (dB)";
                case WATCH_SCALE_LINEAR_GAIN:
                default:
                    return "Gain";
            }
        case WATCH_DOMAIN_SPECTROGRAM:
            return "Frequency (Hz)";
        case WATCH_DOMAIN_FTABLE:
            return "Value";
        case WATCH_DOMAIN_POINT:
            return "y";
        case WATCH_DOMAIN_METER:
            return config->settings.meter.scale == WATCH_SCALE_DECIBEL
                ? "Level (dB)"
                : "Gain";
        default:
            return "";
    }
}

static void destroy_text_label(TEXT_LABEL *label) {
    if (label == NULL) {
        return;
    }
    if (label->texture != NULL) {
        SDL_DestroyTexture(label->texture);
    }
    memset(label, 0, sizeof(*label));
}

static TTF_Font *open_embedded_font(float point_size, SDL_IOStream **stream) {
    *stream = SDL_IOFromConstMem(WATCH_FONT_DATA, WATCH_FONT_DATA_SIZE);
    if (*stream == NULL) {
        return NULL;
    }

    TTF_Font *font = TTF_OpenFontIO(*stream, false, point_size);
    if (font == NULL) {
        SDL_CloseIO(*stream);
        *stream = NULL;
        return NULL;
    }
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    return font;
}

static bool create_text_label(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const char *text,
    WATCH_GRAPH_THEME theme,
    TEXT_LABEL *label
) {
    if (renderer == NULL || font == NULL || label == NULL || text == NULL || text[0] == '\0') {
        return false;
    }

    uint8_t component = themed_component(theme, 24U);
    SDL_Color color = {component, component, component, 255U};
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, 0U, color);
    if (surface == NULL) {
        return false;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == NULL) {
        SDL_DestroySurface(surface);
        return false;
    }

    float width = (float) surface->w;
    float height = (float) surface->h;
    SDL_DestroySurface(surface);
    if (!SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) || !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroyTexture(texture);
        return false;
    }

    label->texture = texture;
    label->width = width;
    label->height = height;
    return true;
}

static bool prepare_axis_labels(VIEW_GRAPH *graph, float density) {
    SDL_IOStream *font_stream = NULL;
    TTF_Font *font = open_embedded_font(AXIS_FONT_SIZE * density, &font_stream);
    if (font == NULL) {
        return false;
    }

    const char *xlabel = x_axis_label(&graph->config);
    const char *ylabel = y_axis_label(&graph->config);
    TEXT_LABEL new_x_label = {0};
    TEXT_LABEL new_y_label = {0};
    bool created =
        create_text_label(graph->renderer, font, xlabel, graph->config.theme, &new_x_label)
        && create_text_label(graph->renderer, font, ylabel, graph->config.theme, &new_y_label);
    TTF_CloseFont(font);
    SDL_CloseIO(font_stream);

    if (!created) {
        destroy_text_label(&new_x_label);
        destroy_text_label(&new_y_label);
        return false;
    }

    destroy_text_label(&graph->x_axis_label);
    destroy_text_label(&graph->y_axis_label);
    graph->x_axis_label = new_x_label;
    graph->y_axis_label = new_y_label;
    graph->axis_label_density = density;
    return true;
}

static void destroy_tick_axis(TICK_AXIS_LABELS *axis) {
    if (axis == NULL) {
        return;
    }
    for (uint32_t i = 0; i < axis->count; i++) {
        destroy_text_label(&axis->entries[i].label);
    }
    free(axis->entries);
    memset(axis, 0, sizeof(*axis));
}

static void destroy_tick_labels(TICK_LABEL_CACHE *cache) {
    if (cache == NULL) {
        return;
    }
    destroy_tick_axis(&cache->x);
    destroy_tick_axis(&cache->y);
    memset(cache, 0, sizeof(*cache));
}

static bool format_tick_value(
    char text[TICK_TEXT_SIZE],
    double value,
    TICK_VALUE_FORMAT format
) {
    if (fabs(value) < 1.0e-12) {
        value = 0.0;
    }

    const char *suffix = "";
    if (format == TICK_FORMAT_FREQUENCY) {
        double magnitude = fabs(value);
        if (magnitude >= 1000000.0) {
            value /= 1000000.0;
            suffix = "M";
        } else if (magnitude >= 1000.0) {
            value /= 1000.0;
            suffix = "k";
        }
    }

    int written = snprintf(text, TICK_TEXT_SIZE, "%.4g%s", value, suffix);
    return written > 0 && written < (int) TICK_TEXT_SIZE;
}

static bool prepare_tick_axis(
    SDL_Renderer *renderer,
    TTF_Font *font,
    uint32_t intervals,
    float minimum,
    float maximum,
    TICK_VALUE_FORMAT format,
    WATCH_GRAPH_THEME theme,
    TICK_AXIS_LABELS *axis
) {
    if (intervals == 0U) {
        return false;
    }

    uint32_t stride = (intervals + MAX_NUMBERED_TICK_INTERVALS - 1U) / MAX_NUMBERED_TICK_INTERVALS;
    uint32_t count = 1U;
    for (uint32_t index = 0U; index < intervals; index += stride) {
        count++;
    }

    TICK_LABEL_ENTRY *entries = calloc(count, sizeof(*entries));
    if (entries == NULL) {
        return false;
    }

    uint32_t entry_index = 0U;
    for (uint32_t grid_index = 0U; grid_index < intervals; grid_index += stride) {
        double fraction = (double) grid_index / intervals;
        double value = minimum + (maximum - minimum) * fraction;
        char text[TICK_TEXT_SIZE];
        if (!format_tick_value(text, value, format)
            || !create_text_label(
                renderer,
                font,
                text,
                theme,
                &entries[entry_index].label)) {
            TICK_AXIS_LABELS partial = {
                .entries = entries,
                .count = entry_index,
                .intervals = intervals
            };
            destroy_tick_axis(&partial);
            return false;
        }
        entries[entry_index].grid_index = grid_index;
        entry_index++;
    }

    char text[TICK_TEXT_SIZE];
    if (!format_tick_value(text, maximum, format)
        || !create_text_label(renderer, font, text, theme, &entries[entry_index].label)) {
        TICK_AXIS_LABELS partial = {
            .entries = entries,
            .count = entry_index,
            .intervals = intervals
        };
        destroy_tick_axis(&partial);
        return false;
    }
    entries[entry_index].grid_index = intervals;
    entry_index++;

    axis->entries = entries;
    axis->count = entry_index;
    axis->intervals = intervals;
    return true;
}

/*
 * The channel axis is not a numeric range: its labels are names, one per bar,
 * and they belong at the centre of the bar rather than on its edge. Two grid
 * intervals per channel put the odd indices exactly there, so draw_tick_labels()
 * places them from grid_index/intervals like every other axis. There is no
 * final entry for the maximum either: a category has no upper bound to print.
 */
static bool prepare_channel_tick_axis(
    SDL_Renderer *renderer,
    TTF_Font *font,
    uint32_t nchnls,
    WATCH_GRAPH_THEME theme,
    TICK_AXIS_LABELS *axis
) {
    if (nchnls == 0U || nchnls > MAX_METER_CHANNELS) {
        return false;
    }

    TICK_LABEL_ENTRY *entries = calloc(nchnls, sizeof(*entries));
    if (entries == NULL) {
        return false;
    }

    for (uint32_t i = 0U; i < nchnls; i++) {
        char text[TICK_TEXT_SIZE];
        int written = snprintf(text, TICK_TEXT_SIZE, "CH[%u]", i + 1U);
        bool text_label = create_text_label(renderer, font, text, theme, &entries[i].label);
        if (written <= 0 || written >= (int) TICK_TEXT_SIZE || !text_label) {
            TICK_AXIS_LABELS partial = {
                .entries = entries,
                .count = i,
                .intervals = nchnls * 2U
            };
            destroy_tick_axis(&partial);
            return false;
        }
        entries[i].grid_index = 2U * i + 1U;
    }

    axis->entries = entries;
    axis->count = nchnls;
    axis->intervals = nchnls * 2U;
    return true;
}

static bool prepare_tick_labels(
    VIEW_GRAPH *graph,
    uint32_t x_intervals,
    uint32_t y_intervals,
    float xmin,
    float xmax,
    float ymin,
    float ymax,
    TICK_VALUE_FORMAT x_format,
    TICK_VALUE_FORMAT y_format,
    float density
) {
    SDL_IOStream *font_stream = NULL;
    TTF_Font *font = open_embedded_font(TICK_FONT_SIZE * density, &font_stream);
    if (font == NULL) {
        return false;
    }

    TICK_LABEL_CACHE next = {0};
    bool channel_tick_axis = prepare_channel_tick_axis(graph->renderer, font, (uint32_t) xmax, graph->config.theme, &next.x);
    bool tick_axis = prepare_tick_axis(graph->renderer, font, x_intervals, xmin, xmax, x_format, graph->config.theme, &next.x);
    bool created = x_format == TICK_FORMAT_CHANNEL ? channel_tick_axis : tick_axis;

    if (created) {
        created = prepare_tick_axis(graph->renderer, font, y_intervals, ymin, ymax, y_format, graph->config.theme, &next.y);
    }

    TTF_CloseFont(font);
    SDL_CloseIO(font_stream);

    if (!created) {
        destroy_tick_labels(&next);
        return false;
    }

    next.xmin = xmin;
    next.xmax = xmax;
    next.ymin = ymin;
    next.ymax = ymax;
    next.density = density;
    next.x_format = x_format;
    next.y_format = y_format;
    next.ready = true;

    destroy_tick_labels(&graph->tick_labels);
    graph->tick_labels = next;
    return true;
}

static bool ensure_tick_labels(
    VIEW_GRAPH *graph,
    uint32_t x_intervals,
    uint32_t y_intervals,
    float xmin,
    float xmax,
    float ymin,
    float ymax,
    TICK_VALUE_FORMAT x_format,
    TICK_VALUE_FORMAT y_format
) {
    float density = graph_pixel_density(graph);
    TICK_LABEL_CACHE *cache = &graph->tick_labels;
    if (cache->ready
        && cache->x.intervals == x_intervals
        && cache->y.intervals == y_intervals
        && cache->xmin == xmin
        && cache->xmax == xmax
        && cache->ymin == ymin
        && cache->ymax == ymax
        && cache->x_format == x_format
        && cache->y_format == y_format
        && fabsf(cache->density - density) < 0.01f) {
        return true;
    }

    return prepare_tick_labels(graph, x_intervals, y_intervals, xmin, xmax, ymin, ymax, x_format, y_format, density);
}

static float graph_pixel_density(const VIEW_GRAPH *graph) {
    int logical_width;
    int logical_height;
    if (graph == NULL
        || graph->window == NULL
        || graph->width <= 0
        || graph->heigth <= 0
        || !SDL_GetWindowSize(graph->window, &logical_width, &logical_height)
        || logical_width <= 0
        || logical_height <= 0) {
        return 1.0f;
    }

    float horizontal = (float) graph->width / logical_width;
    float vertical = (float) graph->heigth / logical_height;
    return fmaxf(1.0f, fminf(horizontal, vertical));
}

static bool ensure_axis_labels(VIEW_GRAPH *graph) {
    float density = graph_pixel_density(graph);
    if (graph->x_axis_label.texture != NULL && graph->y_axis_label.texture != NULL && fabsf(graph->axis_label_density - density) < 0.01f) {
        return true;
    }
    return prepare_axis_labels(graph, density);
}

static void draw_axis_labels(VIEW_GRAPH *graph, const PLOT_AREA *plot) {
    float density = graph_pixel_density(graph);
    float mark_length = TICK_MARK_LENGTH * density;
    float tick_gap = TICK_LABEL_GAP * density;
    float axis_gap = AXIS_TICK_LABEL_GAP * density;
    float x_tick_height = maximum_tick_height(&graph->tick_labels.x);
    float x_width = graph->x_axis_label.width;
    float x_height = graph->x_axis_label.height;
    SDL_FRect x_destination = {
        .x = plot->x_center - x_width * 0.5f,
        .y = plot->bottom + mark_length + tick_gap + x_tick_height + axis_gap,
        .w = x_width,
        .h = x_height
    };
    SDL_RenderTexture(graph->renderer, graph->x_axis_label.texture, NULL, &x_destination);

    float y_width = graph->y_axis_label.width;
    float y_height = graph->y_axis_label.height;
    /*
     * Anchored to the plot, like every other label, and not to the window: for
     * the domains whose plot area fills the window this is the same position
     * that get_plot_area() reserved, but a point graph shrinks its area to a
     * square and the name has to follow its tick labels.
     */
    float y_tick_width = maximum_tick_width(&graph->tick_labels.y);
    float y_center_x = plot->left - mark_length - tick_gap - y_tick_width - axis_gap - y_height * 0.5f;
    SDL_FRect y_destination = {
        .x = y_center_x - y_width * 0.5f,
        .y = plot->y_center - y_height * 0.5f,
        .w = y_width,
        .h = y_height
    };
    SDL_RenderTextureRotated(graph->renderer, graph->y_axis_label.texture, NULL, &y_destination, 270.0, NULL, SDL_FLIP_NONE);
}

static void draw_tick_labels(VIEW_GRAPH *graph, const PLOT_AREA *plot) {
    const TICK_LABEL_CACHE *cache = &graph->tick_labels;
    if (!cache->ready) {
        return;
    }

    float density = graph_pixel_density(graph);
    float mark_length = TICK_MARK_LENGTH * density;
    float gap = TICK_LABEL_GAP * density;

    /*
     * The rightmost label is the one the others must not run into. On a numeric
     * axis it sits on the right edge, but a channel axis ends half a bar before
     * it, so its position is taken from the grid like every other label.
     */
    const TICK_LABEL_ENTRY *last_x = &cache->x.entries[cache->x.count - 1U];
    float last_x_center = plot->left + ((float) last_x->grid_index / cache->x.intervals) * plot->plot_width;
    float last_x_left = last_x_center - last_x->label.width * 0.5f;
    float previous_right = -INFINITY;
    for (uint32_t i = 0; i < cache->x.count; i++) {
        const TICK_LABEL_ENTRY *entry = &cache->x.entries[i];
        float fraction = (float) entry->grid_index / cache->x.intervals;
        float center = plot->left + fraction * plot->plot_width;
        float left = center - entry->label.width * 0.5f;
        float right = left + entry->label.width;
        bool endpoint = i == 0U || i + 1U == cache->x.count;
        if (!endpoint && (left < previous_right + gap || right + gap > last_x_left)) {
            continue;
        }

        SDL_FRect destination = {
            .x = left,
            .y = plot->bottom + mark_length + gap,
            .w = entry->label.width,
            .h = entry->label.height
        };
        SDL_RenderTexture(graph->renderer, entry->label.texture, NULL, &destination);
        previous_right = right;
    }

    const TICK_LABEL_ENTRY *last_y = &cache->y.entries[cache->y.count - 1U];
    float last_y_bottom = plot->top + last_y->label.height * 0.5f;
    float previous_top = INFINITY;
    for (uint32_t i = 0; i < cache->y.count; i++) {
        const TICK_LABEL_ENTRY *entry = &cache->y.entries[i];
        float fraction = (float) entry->grid_index / cache->y.intervals;
        float center = plot->bottom - fraction * plot->plot_heigth;
        float top = center - entry->label.height * 0.5f;
        float bottom = top + entry->label.height;
        bool endpoint = i == 0U || i + 1U == cache->y.count;
        if (!endpoint && (bottom + gap > previous_top || top - gap < last_y_bottom)) {
            continue;
        }

        SDL_FRect destination = {
            .x = plot->left - mark_length - gap - entry->label.width,
            .y = top,
            .w = entry->label.width,
            .h = entry->label.height
        };
        SDL_RenderTexture(graph->renderer, entry->label.texture, NULL, &destination);
        previous_top = top;
    }
}

/*
 * Vertical grid lines are left out: a bar already separates itself from its
 * neighbours, and a line between channels would only compete with it. The tick
 * marks sit under the centre of each bar, where prepare_channel_tick_axis()
 * puts the channel names.
 */
static void draw_meter(
    SDL_Renderer *renderer,
    const PLOT_AREA *p,
    float density,
    uint32_t nchnls,
    int nyticks,
    WATCH_GRAPH_THEME theme
) {
    if (nchnls == 0U || nyticks <= 0) {
        return;
    }

    float slot = p->plot_width / (float) nchnls;
    float ystep = p->plot_heigth / (float) nyticks;
    float mark_length = TICK_MARK_LENGTH * density;

    /*
     * The topmost division is the headroom above the declared maximum. Tinting
     * it says where the range ends without a line that would compete with the
     * peak marks, and a bar reaching into it is over by an amount that can be
     * read rather than merely pinned to the top edge.
     */
    set_meter_headroom_color(renderer, theme);
    SDL_FRect headroom = {
        .x = p->left,
        .y = p->top,
        .w = p->plot_width,
        .h = ystep
    };
    SDL_RenderFillRect(renderer, &headroom);

    set_themed_draw_color(renderer, theme, 0U, 25U);
    for (int i = 1; i < nyticks; i++) {
        float y = p->top + i * ystep;
        SDL_RenderLine(renderer, p->left, y, p->right - 1, y);
    }

    set_themed_draw_color(renderer, theme, 0U, 255U);
    for (int i = 0; i <= nyticks; i++) {
        float y = p->top + i * ystep;
        SDL_RenderLine(renderer, p->left, y, p->left - mark_length, y);
    }
    for (uint32_t i = 0U; i < nchnls; i++) {
        float x = p->left + ((float) i + 0.5f) * slot;
        SDL_RenderLine(renderer, x, p->bottom, x, p->bottom + mark_length);
    }
}

static void draw_grid(
    SDL_Renderer *renderer,
    const PLOT_AREA *p,
    float density,
    int nxticks,
    int nyticks,
    WATCH_GRAPH_THEME theme
) {
    if (nxticks <= 0 || nyticks <= 0) {
        return;
    }

    float xstep = p->plot_width / (float) nxticks;
    float ystep = p->plot_heigth / (float) nyticks;
    float mark_length = TICK_MARK_LENGTH * density;

    set_themed_draw_color(renderer, theme, 0U, 25U);
    for (int i = 1; i < nxticks; i++) {
        float x = p->left + i * xstep;
        SDL_RenderLine(renderer, x, p->top, x, p->bottom - 1);
    }

    for (int i = 1; i < nyticks; i++) {
        float y = p->top + i * ystep;
        SDL_RenderLine(renderer, p->left, y, p->right - 1, y);
    }

    set_themed_draw_color(renderer, theme, 0U, 255U);
    for (int i = 0; i <= nxticks; i++) {
        float x = p->left + i * xstep;
        SDL_RenderLine(renderer, x, p->bottom, x, p->bottom + mark_length);
    }
    for (int i = 0; i <= nyticks; i++) {
        float y = p->top + i * ystep;
        SDL_RenderLine(renderer, p->left, y, p->left - mark_length, y);
    }
}

static STREAM_TIME_BUFFER *prepare_scope_buffers(void) {
    return calloc(MAX_STREAMS, sizeof(STREAM_TIME_BUFFER));
}

static void clear_scope_stream(STREAM_TIME_BUFFER *stream) {
    if (stream == NULL) {
        return;
    }
    free(stream->buffer);
    free(stream->plot_points);
    memset(stream, 0, sizeof(*stream));
}

static void free_scope_buffers(STREAM_TIME_BUFFER *buffers) {
    if (buffers == NULL) {
        return;
    }

    for (uint32_t i = 0; i < MAX_STREAMS; i++) {
        clear_scope_stream(&buffers[i]);
    }

    free(buffers);
}

/*
 * A stream whose geometry the viewer cannot honour is remembered instead of
 * being retried: the same packet stream would otherwise repeat the failed
 * allocation, and the diagnostic, for every packet that arrives.
 */
static bool reject_scope_stream(STREAM_TIME_BUFFER *stream, float sample_rate) {
    clear_scope_stream(stream);
    stream->sample_rate = sample_rate;
    stream->unsupported = true;
    return false;
}

static STREAM_SPECTRAL_BUFFER *prepare_spectral_buffers(void) {
    return calloc(MAX_STREAMS, sizeof(STREAM_SPECTRAL_BUFFER));
}

static void clear_spectral_stream(STREAM_SPECTRAL_BUFFER *stream) {
    if (stream == NULL) {
        return;
    }

    free(stream->assembly_bins);
    free(stream->received_bins);
    free(stream->display_bins);
    free(stream->plot_points);
    free(stream->color_column);
    if (stream->history_texture != NULL) {
        SDL_DestroyTexture(stream->history_texture);
    }
    memset(stream, 0, sizeof(*stream));
}

static void free_spectral_buffers(STREAM_SPECTRAL_BUFFER *buffers) {
    if (buffers == NULL) {
        return;
    }

    for (uint32_t i = 0; i < MAX_STREAMS; i++) {
        clear_spectral_stream(&buffers[i]);
    }

    free(buffers);
}

static STREAM_FTABLE_BUFFER *prepare_ftable_buffers(void) {
    return calloc(MAX_STREAMS, sizeof(STREAM_FTABLE_BUFFER));
}

static void clear_ftable_stream(STREAM_FTABLE_BUFFER *stream) {
    if (stream == NULL) {
        return;
    }
    free(stream->samples);
    free(stream->received_chunks);
    free(stream->envelope_minimums);
    free(stream->envelope_maximums);
    memset(stream, 0, sizeof(*stream));
}

static void free_ftable_buffers(STREAM_FTABLE_BUFFER *buffers) {
    if (buffers == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
        clear_ftable_stream(&buffers[i]);
    }
    free(buffers);
}

static STREAM_POINT_BUFFER *prepare_point_buffers(void) {
    return calloc(MAX_STREAMS, sizeof(STREAM_POINT_BUFFER));
}

static void clear_point_stream(STREAM_POINT_BUFFER *stream) {
    if (stream == NULL) {
        return;
    }
    free(stream->coordinates);
    free(stream->plot_points);
    memset(stream, 0, sizeof(*stream));
}

static void free_point_buffers(STREAM_POINT_BUFFER *buffers) {
    if (buffers == NULL) {
        return;
    }
    for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
        clear_point_stream(&buffers[i]);
    }
    free(buffers);
}

static uint32_t point_trail_capacity(float sample_rate) {
    double samples = ceil((double) POINT_TRAIL_SECONDS * (double) sample_rate);
    if (!(samples >= 2.0)) {
        return 2U; // two points are the shortest visible segment
    }
    if (samples > (double) POINT_TRAIL_MAX_SAMPLES) {
        return POINT_TRAIL_MAX_SAMPLES;
    }
    return (uint32_t) samples;
}

static bool ensure_point_buffer(STREAM_POINT_BUFFER *stream, float sample_rate) {
    if (stream->unsupported && stream->sample_rate == sample_rate) {
        return false;
    }

    uint32_t capacity = point_trail_capacity(sample_rate);
    if (stream->coordinates != NULL
        && stream->plot_points != NULL
        && stream->capacity == capacity) {
        stream->sample_rate = sample_rate;
        return true;
    }

    clear_point_stream(stream);

    float *coordinates = calloc((size_t) capacity * 2U, sizeof(float));
    SDL_FPoint *plot_points = calloc(capacity, sizeof(SDL_FPoint));
    if (coordinates == NULL || plot_points == NULL) {
        free(coordinates);
        free(plot_points);
        clear_point_stream(stream);
        stream->sample_rate = sample_rate;
        stream->unsupported = true;
        return false;
    }

    stream->coordinates = coordinates;
    stream->plot_points = plot_points;
    stream->capacity = capacity;
    stream->sample_rate = sample_rate;
    return true;
}

static void append_point(STREAM_POINT_BUFFER *stream, float x, float y) {
    size_t base = (size_t) stream->write_index * 2U;
    stream->coordinates[base] = isfinite(x) ? x : 0.0f;
    stream->coordinates[base + 1U] = isfinite(y) ? y : 0.0f;
    stream->write_index = (stream->write_index + 1U) % stream->capacity;
    if (stream->valid_points < stream->capacity) {
        stream->valid_points++;
    }
}

static STREAM_METER_BUFFER *prepare_meter_buffer(void) {
    return calloc(1, sizeof(STREAM_METER_BUFFER));
}

static void free_meter_buffer(STREAM_METER_BUFFER *stream) {
    free(stream);
}

static uint64_t scope_latency_samples(float sample_rate) {
    double samples = ceil((double) sample_rate * SCOPE_RENDER_LATENCY_MS / 1000.0);
    return samples > 0.0 ? (uint64_t) samples : 0U;
}

static bool ensure_scope_buffer(STREAM_TIME_BUFFER *stream, float win_size, float sample_rate, float ymin, float ymax) {
    if (stream->unsupported && stream->sample_rate == sample_rate) {
        return false;
    }

    double requested_length = (double) win_size * (double) sample_rate;
    if (!isfinite(requested_length) || requested_length <= 0.0 || requested_length > (double) INT_MAX) {
        return reject_scope_stream(stream, sample_rate);
    }
    uint32_t window_samples = (uint32_t) ceil(requested_length);
    uint64_t latency_samples = scope_latency_samples(sample_rate);
    uint64_t requested_capacity = (uint64_t) window_samples + latency_samples + MAX_STREAM_SAMPLES;
    if (requested_capacity > INT_MAX) {
        return reject_scope_stream(stream, sample_rate);
    }
    uint32_t capacity = (uint32_t) requested_capacity;

    if (stream->buffer != NULL
        && stream->plot_points != NULL
        && stream->sample_rate == sample_rate
        && stream->window_samples == window_samples) {
        stream->ymin = ymin;
        stream->ymax = ymax;
        return true;
    }

    clear_scope_stream(stream);

    float *buffer = calloc(capacity, sizeof(float));
    SDL_FPoint *plot_points = calloc(window_samples, sizeof(SDL_FPoint));

    if (buffer == NULL || plot_points == NULL) {
        free(buffer);
        free(plot_points);
        return reject_scope_stream(stream, sample_rate);
    }

    stream->buffer = buffer;
    stream->plot_points = plot_points;
    stream->write_index = 0;
    stream->capacity = capacity;
    stream->window_samples = window_samples;
    stream->valid_samples = 0;
    stream->ymin = ymin;
    stream->ymax = ymax;
    stream->sample_rate = sample_rate;

    return true;
}

static void append_scope_value(STREAM_TIME_BUFFER *stream, float sample) {
    stream->buffer[stream->write_index] = isfinite(sample) ? sample : 0.0f;
    stream->write_index = (stream->write_index + 1U) % stream->capacity;
    stream->total_samples++;
    if (stream->valid_samples < stream->capacity) {
        stream->valid_samples++;
    }
}

static void append_scope_gap(STREAM_TIME_BUFFER *stream, uint64_t gap) {
    if (gap >= stream->capacity) {
        memset(stream->buffer, 0, stream->capacity * sizeof(float));
        stream->total_samples += gap;
        stream->write_index = (uint32_t) (stream->total_samples % stream->capacity);
        stream->valid_samples = stream->capacity;
        return;
    }

    for (uint64_t i = 0; i < gap; i++) {
        append_scope_value(stream, 0.0f);
    }
}

static void append_scope_samples(STREAM_TIME_BUFFER *stream, const float *samples, uint32_t nsamples, int64_t sequence) {
    if (!stream->has_sequence) {
        stream->next_sequence = sequence;
        stream->has_sequence = true;
    }

    if (sequence < stream->next_sequence) {
        uint64_t overlap = (uint64_t) (stream->next_sequence - sequence);
        if (overlap >= nsamples) {
            return;
        }
        samples += overlap;
        nsamples -= (uint32_t) overlap;
        sequence = stream->next_sequence;
    } else if (sequence > stream->next_sequence) {
        append_scope_gap(stream, (uint64_t) (sequence - stream->next_sequence));
    }

    for (uint32_t i = 0; i < nsamples; i++) {
        append_scope_value(stream, samples[i]);
    }
    stream->next_sequence = sequence + (int64_t) nsamples;
}

static float scope_sample_y(const PLOT_AREA *plot, const STREAM_TIME_BUFFER *stream, float sample) {
    float normalized = (sample - stream->ymin) / (stream->ymax - stream->ymin);
    return plot->bottom - normalized * plot->plot_heigth;
}

static void draw_waveform_line(SDL_Renderer *renderer, float x1, float y1, float x2, float y2, bool supersampled) {
    float radius = supersampled ? WAVEFORM_LINE_RADIUS : 1.0f;
    SDL_RenderLine(renderer, x1, y1, x2, y2);
    SDL_RenderLine(renderer, x1 - radius, y1, x2 - radius, y2);
    SDL_RenderLine(renderer, x1 + radius, y1, x2 + radius, y2);
    SDL_RenderLine(renderer, x1, y1 - radius, x2, y2 - radius);
    SDL_RenderLine(renderer, x1, y1 + radius, x2, y2 + radius);
}

static void draw_waveform_point(SDL_Renderer *renderer, float x, float y, bool supersampled) {
    float radius = supersampled ? WAVEFORM_LINE_RADIUS : 1.0f;
    SDL_RenderPoint(renderer, x, y);
    SDL_RenderPoint(renderer, x - radius, y);
    SDL_RenderPoint(renderer, x + radius, y);
    SDL_RenderPoint(renderer, x, y - radius);
    SDL_RenderPoint(renderer, x, y + radius);
}

static void draw_scope_vertical(SDL_Renderer *renderer, float x, float top, float bottom, bool supersampled) {
    draw_waveform_line(renderer, x, top, x, bottom, supersampled);
}

static void draw_scope_polyline(SDL_Renderer *renderer, SDL_FPoint *points, uint32_t point_count, bool supersampled) {
    float radius = supersampled ? WAVEFORM_LINE_RADIUS : 1.0f;
    SDL_RenderLines(renderer, points, (int) point_count);

    for (uint32_t i = 0; i < point_count; i++) {
        points[i].x -= radius;
    }
    SDL_RenderLines(renderer, points, (int) point_count);

    for (uint32_t i = 0; i < point_count; i++) {
        points[i].x += 2.0f * radius;
    }
    SDL_RenderLines(renderer, points, (int) point_count);

    for (uint32_t i = 0; i < point_count; i++) {
        points[i].x -= radius;
        points[i].y -= radius;
    }
    SDL_RenderLines(renderer, points, (int) point_count);

    for (uint32_t i = 0; i < point_count; i++) {
        points[i].y += 2.0f * radius;
    }
    SDL_RenderLines(renderer, points, (int) point_count);

    for (uint32_t i = 0; i < point_count; i++) {
        points[i].y -= radius;
    }
}

static uint32_t scope_buffer_index(const STREAM_TIME_BUFFER *stream, int64_t sequence) {
    uint64_t distance_from_end = (uint64_t) (stream->next_sequence - sequence);
    uint32_t distance = (uint32_t) (distance_from_end % stream->capacity);
    return (stream->write_index + stream->capacity - distance) % stream->capacity;
}

static float scope_display_sample(const STREAM_TIME_BUFFER *stream, uint32_t first_buffer_index, uint32_t display_index) {
    uint32_t index = first_buffer_index + display_index;
    return stream->buffer[index % stream->capacity];
}

static bool advance_scope_timeline(VIEW_GRAPH *graph, uint64_t now_ns) {
    double latest_safe_sample = 0.0;
    double latest_received_sample = 0.0;
    double minimum_full_window_end = 0.0;
    float timeline_sample_rate = 0.0f;
    bool found_stream = false;
    bool found_full_window = false;

    for (uint32_t i = 0; i < MAX_STREAMS; i++) {
        STREAM_TIME_BUFFER *stream = &graph->scope_streams[i];
        if (stream->buffer == NULL || !stream->has_sequence || stream->valid_samples == 0U) {
            continue;
        }

        uint64_t latency_samples = scope_latency_samples(stream->sample_rate);
        double stream_latest = (double) stream->next_sequence - (double) latency_samples;
        if (!found_stream || stream_latest < latest_safe_sample) {
            latest_safe_sample = stream_latest;
        }
        if (!found_stream
            || (double) stream->next_sequence < latest_received_sample) {
            latest_received_sample = (double) stream->next_sequence;
        }
        if (!found_stream) {
            timeline_sample_rate = stream->sample_rate;
        }

        if (stream->valid_samples >= stream->window_samples) {
            double stream_minimum_end =
                (double) stream->next_sequence
                - (double) stream->valid_samples
                + (double) stream->window_samples;
            if (!found_full_window
                || stream_minimum_end > minimum_full_window_end) {
                minimum_full_window_end = stream_minimum_end;
            }
            found_full_window = true;
        }
        found_stream = true;
    }

    if (!found_stream) {
        return false;
    }

    if (!graph->scope_display_started) {
        graph->scope_display_end_sample = latest_safe_sample;
        graph->scope_last_render_ns = now_ns;
        graph->scope_display_started = true;
    } else {
        uint64_t elapsed_ns = now_ns - graph->scope_last_render_ns;
        graph->scope_display_end_sample +=
            ((double) elapsed_ns * (double) timeline_sample_rate)
            / NANOSECONDS_PER_SECOND;
        graph->scope_last_render_ns = now_ns;
    }

    /*
     * Configuration acknowledgement can release a backlog of queued packets
     * much faster than real time. If the ring buffer overtakes the display
     * clock, jump to the oldest complete window that is still available.
     */
    if (found_full_window
        && minimum_full_window_end <= latest_safe_sample
        && graph->scope_display_end_sample < minimum_full_window_end) {
        graph->scope_display_end_sample = minimum_full_window_end;
    }
    if (graph->scope_display_end_sample > latest_received_sample) {
        graph->scope_display_end_sample = latest_received_sample;
    }
    return true;
}

static void draw_scope_stream(
    SDL_Renderer *renderer,
    PLOT_AREA *p,
    STREAM_TIME_BUFFER *stream,
    uint32_t stream_index,
    double graph_display_end_sample,
    bool supersampled,
    WATCH_GRAPH_THEME theme
) {
    if (stream->buffer == NULL || stream->plot_points == NULL || stream->valid_samples == 0U) {
        return;
    }

    int64_t oldest_available = stream->next_sequence - (int64_t) stream->valid_samples;
    int64_t display_end = (int64_t) graph_display_end_sample;
    if (display_end > stream->next_sequence) {
        display_end = stream->next_sequence;
    }
    if (display_end <= oldest_available) {
        return;
    }

    uint64_t available_to_display = (uint64_t) (display_end - oldest_available);
    uint32_t point_count = available_to_display < stream->window_samples ? (uint32_t) available_to_display : stream->window_samples;
    if (point_count == 0U) {
        return;
    }

    int64_t first_sample = display_end - (int64_t) point_count;
    uint32_t first_buffer_index = scope_buffer_index(stream, first_sample);
    float x_step = 0.0;
    if (stream->window_samples > 1U) {
        x_step = p->plot_width / (float) (stream->window_samples - 1U);
    }
    float first_x = p->left;
    float last_x = first_x + (float) (point_count - 1U) * x_step;

    set_stream_draw_color(renderer, theme, stream_index);

    /*
     * When several samples fall inside one output pixel, drawing the raw
     * polyline creates spatial aliasing and moire patterns. Collapse every
     * physical pixel column to the minimum/maximum range it contains while
     * keeping the first and last columns aligned with the exact bounds of the
     * portion filled so far.
     */
    float raster_width = last_x - first_x;
    uint32_t column_count = raster_width >= 0.0f ? (uint32_t) floorf(raster_width) + 1U : 0U;

    if (column_count > 0U && (uint64_t) point_count > (uint64_t) column_count * 2U) {
        bool has_previous = false;
        float previous_x = 0.0f;
        float previous_y = 0.0f;
        for (uint32_t column = 0; column < column_count; column++) {
            uint32_t first_in_column = (uint32_t) (((uint64_t) column * point_count) / column_count);
            uint32_t end_in_column = (uint32_t) (((uint64_t) (column + 1U) * point_count) / column_count);

            float minimum = scope_display_sample(stream, first_buffer_index, first_in_column);
            float maximum = minimum;
            for (uint32_t i = first_in_column + 1U; i < end_in_column; i++) {
                float sample = scope_display_sample(stream, first_buffer_index, i);
                if (sample < minimum) {
                    minimum = sample;
                }
                if (sample > maximum) {
                    maximum = sample;
                }
            }

            float fx_col = first_x + (float) column * raster_width / (float) (column_count - 1U);
            float x = column_count > 1U ? fx_col : first_x;
            float top = scope_sample_y(p, stream, maximum);
            float bottom = scope_sample_y(p, stream, minimum);

            float display_sample_first = scope_display_sample(stream, first_buffer_index, first_in_column);
            float first_y = scope_sample_y(p, stream, display_sample_first);
            if (has_previous) {
                draw_waveform_line(renderer, previous_x, previous_y, x, first_y, supersampled);
            }
            draw_scope_vertical(renderer, x, top, bottom, supersampled);
            previous_x = x;
            float display_sample_end = scope_display_sample(stream, first_buffer_index, end_in_column - 1U);
            previous_y = scope_sample_y(p, stream, display_sample_end);
            has_previous = true;
        }
        return;
    }

    for (uint32_t i = 0; i < point_count; i++) {
        float sample = scope_display_sample(stream, first_buffer_index, i);
        stream->plot_points[i].x = first_x + i * x_step;
        stream->plot_points[i].y = scope_sample_y(p, stream, sample);
    }

    if (point_count == 1U) {
        draw_waveform_point(renderer, stream->plot_points[0].x, stream->plot_points[0].y, supersampled);
    } else {
        draw_scope_polyline(renderer, stream->plot_points, point_count, supersampled);
    }
}

static bool is_time_domain(WATCH_SIGNAL_DOMAIN domain) {
    return domain == WATCH_DOMAIN_OSCILLOSCOPE || domain == WATCH_DOMAIN_CONTROL;
}

static bool is_spectral_domain(WATCH_SIGNAL_DOMAIN domain) {
    return domain == WATCH_DOMAIN_SPECTRUM || domain == WATCH_DOMAIN_SPECTROGRAM;
}

static bool is_ftable_domain(WATCH_SIGNAL_DOMAIN domain) {
    return domain == WATCH_DOMAIN_FTABLE;
}

static bool is_point_domain(WATCH_SIGNAL_DOMAIN domain) {
    return domain == WATCH_DOMAIN_POINT;
}

static bool is_meter_domain(WATCH_SIGNAL_DOMAIN domain) {
    return domain == WATCH_DOMAIN_METER;
}

static float spectral_display_value(float power, WATCH_SPECTRAL_SCALE scale) {
    if (!isfinite(power) || power < 0.0f) {
        power = 0.0f;
    }

    switch (scale) {
        case WATCH_SCALE_LINEAR_GAIN:
            return sqrtf(power);
        case WATCH_SCALE_LINEAR_POWER:
            return power;
        case WATCH_SCALE_DECIBEL:
            if (power <= 0.0f) {
                return SPECTRAL_DB_FLOOR;
            }
            return fmaxf(10.0f * log10f(power), SPECTRAL_DB_FLOOR);
        default:
            return 0.0f;
    }
}

static void resolve_spectral_static_range(const WATCH_MSG_SPECTRAL_CONFIG *config, float *minimum, float *maximum) {
    float default_minimum = config->scale == WATCH_SCALE_DECIBEL ? -120.0f : 0.0f;
    float default_maximum = config->scale == WATCH_SCALE_DECIBEL ? 0.0f : 1.0f;

    *minimum = config->value_range.is_min_auto ? default_minimum : config->value_range.min;
    *maximum = config->value_range.is_max_auto ? default_maximum : config->value_range.max;

    if (*maximum <= *minimum) {
        if (config->value_range.is_max_auto) {
            *maximum = *minimum + 1.0f;
        } else {
            *minimum = *maximum - 1.0f;
        }
    }
}

static void spectral_grayscale(float value, float minimum, float maximum, WATCH_GRAPH_THEME theme, uint8_t rgba[4]) {
    float normalized = (value - minimum) / (maximum - minimum);
    normalized = fminf(fmaxf(normalized, 0.0f), 1.0f);
    uint8_t light_shade = (uint8_t) lroundf((1.0f - normalized) * 255.0f);
    uint8_t shade = themed_component(theme, light_shade);
    rgba[0] = shade;
    rgba[1] = shade;
    rgba[2] = shade;
    rgba[3] = 255U;
}

static SDL_Texture *create_spectrogram_texture(SDL_Renderer *renderer, uint32_t width, uint32_t height, WATCH_GRAPH_THEME theme) {
    if (renderer == NULL
        || width == 0U
        || height == 0U
        || width > INT_MAX
        || height > INT_MAX
        || (uint64_t) width * height > MAX_SPECTROGRAM_TEXELS) {
        return NULL;
    }

    Sint64 max_texture_size = SDL_GetNumberProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    if (max_texture_size > 0 && ((Sint64) width > max_texture_size || (Sint64) height > max_texture_size)) {
        return NULL;
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, (int) width, (int) height);

    if (texture == NULL) {
        return NULL;
    }

    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR) || !SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE)) {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    size_t byte_count = (size_t) width * height * 4U;
    uint8_t *blank = malloc(byte_count);
    if (blank == NULL) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    uint8_t background = themed_component(theme, 255U);
    for (size_t i = 0U; i < byte_count; i += 4U) {
        blank[i] = background;
        blank[i + 1U] = background;
        blank[i + 2U] = background;
        blank[i + 3U] = 255U;
    }

    bool initialized = SDL_UpdateTexture(texture, NULL, blank, (int) ((size_t) width * 4U));

    free(blank);
    if (!initialized) {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    return texture;
}

static bool reject_spectral_stream(STREAM_SPECTRAL_BUFFER *stream, const WATCH_MSG_SPECTRAL_DATA *data) {
    clear_spectral_stream(stream);
    stream->fft_size = data->fft_size;
    stream->hop_size = data->hop_size;
    stream->sample_rate = data->sample_rate;
    stream->total_bins = data->total_bins;
    stream->unsupported = true;
    return false;
}

static bool ensure_spectral_stream(VIEW_GRAPH *graph, STREAM_SPECTRAL_BUFFER *stream, const WATCH_MSG_SPECTRAL_DATA *data) {
    bool metadata_matches =
        stream->assembly_bins != NULL
        && stream->received_bins != NULL
        && stream->fft_size == data->fft_size
        && stream->hop_size == data->hop_size
        && stream->sample_rate == data->sample_rate
        && stream->total_bins == data->total_bins;

    if (stream->unsupported
        && stream->fft_size == data->fft_size
        && stream->hop_size == data->hop_size
        && stream->sample_rate == data->sample_rate
        && stream->total_bins == data->total_bins) {
        return false;
    }
    if (metadata_matches) {
        if (graph->config.domain == WATCH_DOMAIN_SPECTRUM) {
            return stream->display_bins != NULL && stream->plot_points != NULL;
        }
        return stream->history_texture != NULL && stream->color_column != NULL;
    }

    clear_spectral_stream(stream);

    stream->assembly_bins = calloc(data->total_bins, sizeof(float));
    stream->received_bins = calloc(data->total_bins, sizeof(uint8_t));
    if (stream->assembly_bins == NULL || stream->received_bins == NULL) {
        return reject_spectral_stream(stream, data);
    }

    stream->fft_size = data->fft_size;
    stream->hop_size = data->hop_size;
    stream->sample_rate = data->sample_rate;
    stream->total_bins = data->total_bins;

    if (graph->config.domain == WATCH_DOMAIN_SPECTRUM) {
        stream->display_bins = calloc(data->total_bins, sizeof(float));
        stream->plot_points = calloc(data->total_bins, sizeof(SDL_FPoint));
        if (stream->display_bins == NULL || stream->plot_points == NULL) {
            return reject_spectral_stream(stream, data);
        }
        return true;
    }

    const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;
    double requested_history = ceil((double) config->history_seconds * data->sample_rate / data->hop_size);
    if (!isfinite(requested_history)
        || requested_history < 1.0
        || requested_history > INT_MAX
        || requested_history * data->total_bins > MAX_SPECTROGRAM_TEXELS) {
        return reject_spectral_stream(stream, data);
    }

    stream->history_capacity = (uint32_t) requested_history;
    stream->color_column = malloc((size_t) data->total_bins * 4U);
    stream->history_texture = create_spectrogram_texture(graph->renderer, stream->history_capacity, data->total_bins, graph->config.theme);
    if (stream->color_column == NULL || stream->history_texture == NULL) {
        return reject_spectral_stream(stream, data);
    }

    return true;
}

static VIEW_GRAPH *find_graph(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    uint32_t graph_id,
    const watch_endpoint_t *sender
) {
    for (uint32_t i = 0; i < MAX_VIEWER_GRAPHS; i++) {
        if (graphs[i].ready && graphs[i].config.graph_id == graph_id && watch_endpoint_equal(&graphs[i].sender, sender)) {
            return &graphs[i];
        }
    }
    return NULL;
}

static VIEW_GRAPH *find_graph_by_window(VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS], SDL_WindowID window_id) {
    for (uint32_t i = 0; i < MAX_VIEWER_GRAPHS; i++) {
        if (graphs[i].ready && graphs[i].window_id == window_id) {
            return &graphs[i];
        }
    }
    return NULL;
}

static void destroy_graph(VIEW_GRAPH *graph) {
    if (graph == NULL) {
        return;
    }

    free_scope_buffers(graph->scope_streams);
    free_spectral_buffers(graph->spectral_streams);
    free_ftable_buffers(graph->ftable_streams);
    free_point_buffers(graph->point_streams);
    free_meter_buffer(graph->meter_stream);
    destroy_text_label(&graph->x_axis_label);
    destroy_text_label(&graph->y_axis_label);
    destroy_tick_labels(&graph->tick_labels);
    if (graph->render_target != NULL) {
        SDL_DestroyTexture(graph->render_target);
    }
    if (graph->renderer != NULL) {
        SDL_DestroyRenderer(graph->renderer);
    }
    if (graph->window != NULL) {
        SDL_DestroyWindow(graph->window);
    }
    memset(graph, 0, sizeof(*graph));
}

static void update_graph_render_size(VIEW_GRAPH *graph) {
    int width;
    int height;
    if (graph != NULL && graph->renderer != NULL && SDL_GetRenderOutputSize(graph->renderer, &width, &height) && width > 0 && height > 0) {
        graph->width = width;
        graph->heigth = height;
    }
}

static bool ensure_graph_render_target(VIEW_GRAPH *graph) {
    if (graph == NULL
        || graph->renderer == NULL
        || graph->width <= 0
        || graph->heigth <= 0
        || graph->width > INT_MAX / RENDER_SUPERSAMPLE_SCALE
        || graph->heigth > INT_MAX / RENDER_SUPERSAMPLE_SCALE) {
        return false;
    }

    int target_width = graph->width * RENDER_SUPERSAMPLE_SCALE;
    int target_heigth = graph->heigth * RENDER_SUPERSAMPLE_SCALE;
    if (graph->render_target != NULL
        && graph->render_target_width == target_width
        && graph->render_target_heigth == target_heigth) {
        return true;
    }

    if (graph->render_target != NULL) {
        SDL_DestroyTexture(graph->render_target);
        graph->render_target = NULL;
    }

    graph->render_target = SDL_CreateTexture(graph->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, target_width, target_heigth);
    if (graph->render_target == NULL) {
        graph->render_target_width = 0;
        graph->render_target_heigth = 0;
        return false;
    }

    if (!SDL_SetTextureScaleMode(graph->render_target, SDL_SCALEMODE_LINEAR)) {
        SDL_DestroyTexture(graph->render_target);
        graph->render_target = NULL;
        graph->render_target_width = 0;
        graph->render_target_heigth = 0;
        return false;
    }

    graph->render_target_width = target_width;
    graph->render_target_heigth = target_heigth;
    return true;
}

static bool validate_header(const WATCH_MSG_HEADER *header, int64_t received) {
    if (received < (int64_t) sizeof(WATCH_MSG_HEADER) || header->magic != WATCH_MAGIC || header->version != PROT_VERSION) {
        return false;
    }

    uint64_t expected_size = (uint64_t) sizeof(WATCH_MSG_HEADER) + header->payload_size;
    return expected_size == (uint64_t) received;
}

static bool valid_title(const char title[MAX_TITLE_SIZE]) {
    return memchr(title, '\0', MAX_TITLE_SIZE) != NULL;
}

static bool valid_range(const WATCH_RANGE *range, bool nonnegative) {
    if ((range->is_min_auto != 0 && range->is_min_auto != 1) || (range->is_max_auto != 0 && range->is_max_auto != 1)) {
        return false;
    }

    if (!range->is_min_auto && (!isfinite(range->min) || (nonnegative && range->min < 0.0f))) {
        return false;
    }
    if (!range->is_max_auto && (!isfinite(range->max) || (nonnegative && range->max < 0.0f))) {
        return false;
    }

    return range->is_min_auto || range->is_max_auto || range->min < range->max;
}

static bool validate_config_packet(const WATCH_CONFIG_PACKET *packet, int64_t received) {
    if (!validate_header(&packet->header, received)
        || packet->header.type != CONFIG
        || packet->header.payload_size != sizeof(WATCH_MSG_CONFIG)
        || packet->config.graph_id == 0U
        || packet->config.domain > WATCH_DOMAIN_METER
        || packet->config.theme > WATCH_THEME_DARK
        || !valid_title(packet->config.title)) {
        return false;
    }

    if (is_time_domain(packet->config.domain)) {
        const WATCH_MSG_TIME_CONFIG *config = &packet->config.settings.time;
        if (!isfinite(config->win_size)
            || config->win_size <= 0.0f
            || config->nxticks > MAX_GRID_TICKS
            || config->nyticks > MAX_GRID_TICKS
            || !valid_range(&config->yrange, false)) {
            return false;
        }
    } else if (is_ftable_domain(packet->config.domain)) {
        const WATCH_MSG_FTABLE_CONFIG *config = &packet->config.settings.ftable;
        if (config->win_size == 0U || config->win_size > MAX_FTABLE_SAMPLES || !valid_range(&config->yrange, false)) {
            return false;
        }
    } else if (is_point_domain(packet->config.domain)) {
        const WATCH_MSG_POINT_CONFIG *config = &packet->config.settings.point;
        if (!valid_range(&config->xrange, false) || !valid_range(&config->yrange, false)) {
            return false;
        }
    } else if (is_meter_domain(packet->config.domain)) {
        const WATCH_MSG_METER_CONFIG *config = &packet->config.settings.meter;
        // a decibel axis lives below zero, a gain axis cannot go under it
        bool value_is_nonnegative = config->scale != WATCH_SCALE_DECIBEL;
        bool scale_is_supported = config->scale == WATCH_SCALE_LINEAR_GAIN || config->scale == WATCH_SCALE_DECIBEL;
        if (config->nchnls == 0U
            || config->nchnls > MAX_METER_CHANNELS
            || config->nyticks > MAX_GRID_TICKS
            || !scale_is_supported
            || !valid_range(&config->yrange, value_is_nonnegative)) {
            return false;
        }
    } else {
        const WATCH_MSG_SPECTRAL_CONFIG *config = &packet->config.settings.spectral;
        bool value_is_nonnegative = config->scale != WATCH_SCALE_DECIBEL;
        if (config->nxticks > MAX_GRID_TICKS
            || config->nyticks > MAX_GRID_TICKS
            || config->scale > WATCH_SCALE_DECIBEL
            || !valid_range(&config->frequency_range, true)
            || !valid_range(&config->value_range, value_is_nonnegative)
            || (packet->config.domain == WATCH_DOMAIN_SPECTROGRAM && (!isfinite(config->history_seconds) || config->history_seconds <= 0.0f))) {
            return false;
        }
    }

    return true;
}

static bool validate_data_packet(const WATCH_DATA_PACKET *packet, int64_t received) {
    if (!validate_header(&packet->header, received)
        || packet->header.type != DATA
        || packet->header.sequence < 0
        || packet->data.graph_id == 0U
        || packet->data.stream_id >= MAX_STREAMS
        || !isfinite(packet->data.sample_rate)
        || packet->data.sample_rate <= 0.0f
        || packet->data.sample_count == 0U
        || packet->data.sample_count > MAX_STREAM_SAMPLES) {
        return false;
    }

    uint32_t expected_payload = (uint32_t) offsetof(WATCH_MSG_DATA, samples) + packet->data.sample_count * (uint32_t) sizeof(float);
    return packet->header.payload_size == expected_payload;
}

static bool validate_spectral_data_packet(const WATCH_SPECTRAL_DATA_PACKET *packet, int64_t received) {
    if (!validate_header(&packet->header, received)
        || packet->header.type != SPECTRAL_DATA
        || packet->header.sequence < 0
        || packet->data.graph_id == 0U
        || packet->data.stream_id >= MAX_STREAMS
        || packet->data.sample_rate == 0U
        || packet->data.format != WATCH_SPECTRAL_POWER
        || packet->data.fft_size == 0U
        || (packet->data.fft_size & 1U) != 0U
        || packet->data.hop_size == 0U
        || packet->data.hop_size > packet->data.fft_size
        || packet->data.bin_count == 0U
        || packet->data.bin_count > MAX_SPECTRAL_BINS
        || packet->data.total_bins > MAX_SPECTRAL_TOTAL_BINS
        || packet->data.total_bins != packet->data.fft_size / 2U + 1U
        || packet->data.bin_offset >= packet->data.total_bins
        || packet->data.bin_count > packet->data.total_bins - packet->data.bin_offset) {
        return false;
    }

    uint32_t expected_payload = (uint32_t) offsetof(WATCH_MSG_SPECTRAL_DATA, bins) + packet->data.bin_count * (uint32_t) sizeof(float);
    return packet->header.payload_size == expected_payload;
}

/*
 * Point packets travel through the time-domain payload but their sample_count
 * is a number of points: the payload carries two interleaved floats each.
 */
static bool validate_point_data_packet(const WATCH_DATA_PACKET *packet, int64_t received) {
    if (!validate_header(&packet->header, received)
        || packet->header.type != POINT_DATA
        || packet->header.sequence < 0
        || packet->data.graph_id == 0U
        || packet->data.stream_id >= MAX_STREAMS
        || !isfinite(packet->data.sample_rate)
        || packet->data.sample_rate <= 0.0f
        || packet->data.sample_count == 0U
        || packet->data.sample_count > MAX_POINT_SAMPLES) {
        return false;
    }

    uint32_t expected_payload = (uint32_t) offsetof(WATCH_MSG_DATA, samples) + packet->data.sample_count * 2U * (uint32_t) sizeof(float);
    return packet->header.payload_size == expected_payload;
}

static bool validate_ftable_data_packet(const WATCH_FTABLE_DATA_PACKET *packet, int64_t received) {
    if (!validate_header(&packet->header, received)
        || packet->header.type != FTABLE_DATA
        || packet->header.sequence < 0
        || packet->data.graph_id == 0U
        || packet->data.stream_id >= MAX_STREAMS
        || packet->data.transfer_id == 0U
        || packet->data.sample_count == 0U
        || packet->data.sample_count > MAX_STREAM_SAMPLES
        || packet->data.total_samples == 0U
        || packet->data.total_samples > MAX_FTABLE_SAMPLES
        || packet->data.sample_offset >= packet->data.total_samples
        || packet->data.sample_offset % MAX_STREAM_SAMPLES != 0U
        || packet->data.sample_count > packet->data.total_samples - packet->data.sample_offset) {
        return false;
    }

    uint32_t remaining = packet->data.total_samples - packet->data.sample_offset;
    uint32_t expected_count = remaining < MAX_STREAM_SAMPLES ? remaining : MAX_STREAM_SAMPLES;
    if (packet->data.sample_count != expected_count) {
        return false;
    }

    uint32_t expected_payload = (uint32_t) offsetof(WATCH_MSG_FTABLE_DATA, samples) + packet->data.sample_count * (uint32_t) sizeof(float);
    return packet->header.payload_size == expected_payload;
}

static bool validate_meter_data_packet(const WATCH_METER_DATA_PACKET *packet, int64_t received) {
    if (!validate_header(&packet->header, received)
        || packet->header.type != METER_DATA
        || packet->header.sequence < 0
        || packet->data.graph_id == 0U
        || packet->data.stream_id >= MAX_STREAMS
        || packet->data.channel_count == 0U
        || packet->data.channel_count > MAX_METER_CHANNELS) {
        return false;
    }

    uint32_t expected_payload = (uint32_t) offsetof(WATCH_MSG_METER_DATA, levels) + packet->data.channel_count * (uint32_t) sizeof(float);
    return packet->header.payload_size == expected_payload;
}

static bool validate_session_close_packet(const WATCH_SESSION_CLOSE_PACKET *packet, int64_t received) {
    return validate_header(&packet->header, received)
        && packet->header.type == SESSION_CLOSE
        && packet->header.payload_size == sizeof(WATCH_MSG_SESSION_CLOSE)
        && packet->close.reserved == 0U;
}

static bool validate_graph_close_packet(const WATCH_GRAPH_CLOSE_PACKET *packet, int64_t received) {
    return validate_header(&packet->header, received)
        && packet->header.type == GRAPH_CLOSE
        && packet->header.payload_size == sizeof(WATCH_MSG_GRAPH_CLOSE)
        && packet->close.graph_id != 0U;
}

static void resolve_scope_range(const WATCH_MSG_TIME_CONFIG *config, float *ymin, float *ymax) {
    if (config->yrange.is_min_auto && config->yrange.is_max_auto) {
        *ymin = -1.0f;
        *ymax = 1.0f;
    } else if (config->yrange.is_min_auto) {
        *ymax = config->yrange.max;
        *ymin = *ymax - 2.0f;
    } else if (config->yrange.is_max_auto) {
        *ymin = config->yrange.min;
        *ymax = *ymin + 2.0f;
    } else {
        *ymin = config->yrange.min;
        *ymax = config->yrange.max;
    }
}

/*
 * The plane must stay still: an omitted limit falls back to a fixed default
 * instead of being derived from the incoming points, which would make the axes
 * drift under the moving point.
 */
static void resolve_point_range(const WATCH_RANGE *range, float *minimum, float *maximum) {
    if (range->is_min_auto && range->is_max_auto) {
        *minimum = -1.0f;
        *maximum = 1.0f;
    } else if (range->is_min_auto) {
        *maximum = range->max;
        *minimum = *maximum - 2.0f;
    } else if (range->is_max_auto) {
        *minimum = range->min;
        *maximum = *minimum + 2.0f;
    } else {
        *minimum = range->min;
        *maximum = range->max;
    }
}

/*
 * Like the point plane, the meter scale is a reference and must not follow the
 * signal: an omitted limit falls back to a fixed default. The default span
 * depends on the unit - sixty decibels cover the useful part of a mix, while a
 * gain axis is read against unity.
 */
static void resolve_meter_range(const WATCH_MSG_METER_CONFIG *config, float *ymin, float *ymax) {
    bool decibel = config->scale == WATCH_SCALE_DECIBEL;
    float span = decibel ? METER_DEFAULT_DB_SPAN : 1.0f;

    if (config->yrange.is_min_auto && config->yrange.is_max_auto) {
        *ymax = decibel ? 0.0f : 1.0f;
        *ymin = decibel ? -METER_DEFAULT_DB_SPAN : 0.0f;
    } else if (config->yrange.is_min_auto) {
        *ymax = config->yrange.max;
        *ymin = *ymax - span;
    } else if (config->yrange.is_max_auto) {
        *ymin = config->yrange.min;
        *ymax = *ymin + span;
    } else {
        *ymin = config->yrange.min;
        *ymax = config->yrange.max;
    }
}

/*
 * The plot keeps one grid division of headroom above the declared maximum. A
 * bar pinned to the top edge looks the same whether it went over by a hair or
 * by twenty decibels, so the range has to end somewhere visible rather than at
 * the border. One division keeps the tick values round: the step is unchanged
 * and the axis simply carries one more of them.
 */
static void resolve_meter_axis(const WATCH_MSG_METER_CONFIG *config, float *ymin, float *ytop, uint32_t *divisions) {
    float ymax;
    resolve_meter_range(config, ymin, &ymax);
    uint32_t range_divisions = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
    *ytop = ymax + (ymax - *ymin) / (float) range_divisions;
    *divisions = range_divisions + 1U;
}

static bool resolve_ftable_range(const VIEW_GRAPH *graph, float *ymin, float *ymax) {
    const WATCH_MSG_FTABLE_CONFIG *config = &graph->config.settings.ftable;
    bool needs_data = config->yrange.is_min_auto || config->yrange.is_max_auto;
    bool found = false;
    float data_minimum = 0.0f;
    float data_maximum = 0.0f;

    if (needs_data && graph->ftable_streams != NULL) {
        for (uint32_t stream_index = 0U; stream_index < MAX_STREAMS; stream_index++) {
            const STREAM_FTABLE_BUFFER *stream = &graph->ftable_streams[stream_index];
            if (!stream->display_ready || !stream->has_range) {
                continue;
            }
            if (!found) {
                data_minimum = stream->data_minimum;
                data_maximum = stream->data_maximum;
                found = true;
            } else {
                data_minimum = fminf(data_minimum, stream->data_minimum);
                data_maximum = fmaxf(data_maximum, stream->data_maximum);
            }
        }
    }

    if (needs_data && !found) {
        return false;
    }

    *ymin = config->yrange.is_min_auto ? data_minimum : config->yrange.min;
    *ymax = config->yrange.is_max_auto ? data_maximum : config->yrange.max;

    if (*ymax <= *ymin) {
        if (config->yrange.is_min_auto
            && config->yrange.is_max_auto) {
            float center = (*ymin + *ymax) * 0.5f;
            float padding = fmaxf(fabsf(center) * 0.1f, 1.0f);
            *ymin = center - padding;
            *ymax = center + padding;
        } else if (config->yrange.is_min_auto) {
            float padding = fmaxf(fabsf(*ymax) * 0.1f, 1.0f);
            *ymin = *ymax - padding;
        } else if (config->yrange.is_max_auto) {
            float padding = fmaxf(fabsf(*ymin) * 0.1f, 1.0f);
            *ymax = *ymin + padding;
        }
    }
    return *ymax > *ymin;
}

static bool resolve_frequency_axis_range(const VIEW_GRAPH *graph, float *minimum, float *maximum) {
    const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;
    const STREAM_SPECTRAL_BUFFER *reference_stream = NULL;
    for (uint32_t i = 0; i < MAX_STREAMS; i++) {
        if (graph->spectral_streams[i].sample_rate > 0U) {
            reference_stream = &graph->spectral_streams[i];
            break;
        }
    }

    float nyquist = 0.0f;
    if (reference_stream != NULL) {
        nyquist = reference_stream->sample_rate * 0.5f;
    } else if (config->frequency_range.is_max_auto) {
        return false;
    }

    *minimum = config->frequency_range.is_min_auto ? 0.0f : config->frequency_range.min;
    *maximum = config->frequency_range.is_max_auto ? nyquist : config->frequency_range.max;
    if (reference_stream != NULL) {
        *minimum = fminf(fmaxf(*minimum, 0.0f), nyquist);
        *maximum = fminf(fmaxf(*maximum, 0.0f), nyquist);
    }
    return *maximum > *minimum;
}

static bool ensure_graph_tick_labels(VIEW_GRAPH *graph) {
    uint32_t x_intervals, y_intervals;
    float xmin, xmax, ymin, ymax;
    TICK_VALUE_FORMAT x_format, y_format;

    if (is_time_domain(graph->config.domain)) {
        const WATCH_MSG_TIME_CONFIG *config = &graph->config.settings.time;
        x_intervals = config->nxticks == 0U ? DEFAULT_X_TICKS : config->nxticks;
        y_intervals = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
        xmin = 0.0f;
        xmax = config->win_size;
        resolve_scope_range(config, &ymin, &ymax);
        x_format = TICK_FORMAT_TIME;
        y_format = TICK_FORMAT_NUMBER;
    } else if (is_point_domain(graph->config.domain)) {
        const WATCH_MSG_POINT_CONFIG *config = &graph->config.settings.point;
        resolve_point_range(&config->xrange, &xmin, &xmax);
        resolve_point_range(&config->yrange, &ymin, &ymax);
        // a plane reads better with the same number of divisions on both axes
        x_intervals = POINT_TICKS;
        y_intervals = POINT_TICKS;
        x_format = TICK_FORMAT_NUMBER;
        y_format = TICK_FORMAT_NUMBER;
    } else if (is_meter_domain(graph->config.domain)) {
        const WATCH_MSG_METER_CONFIG *config = &graph->config.settings.meter;
        // the labelled axis includes the headroom, so its top is above ymax
        resolve_meter_axis(config, &ymin, &ymax, &y_intervals);
        /*
         * Two intervals per channel: the odd grid indices land on the bar
         * centres, which is where prepare_channel_tick_axis() writes the names.
         */
        xmin = 0.0f;
        xmax = (float) config->nchnls;
        x_intervals = config->nchnls * 2U;
        x_format = TICK_FORMAT_CHANNEL;
        y_format = TICK_FORMAT_NUMBER;
    } else if (is_ftable_domain(graph->config.domain)) {
        const WATCH_MSG_FTABLE_CONFIG *config = &graph->config.settings.ftable;
        if (!resolve_ftable_range(graph, &ymin, &ymax)) {
            return false;
        }
        xmin = 0.0f;
        xmax = config->win_size > 1U ? (float) (config->win_size - 1U) : 1.0f;
        x_intervals = DEFAULT_X_TICKS;
        y_intervals = DEFAULT_Y_TICKS;
        x_format = TICK_FORMAT_NUMBER;
        y_format = TICK_FORMAT_NUMBER;
    } else if (graph->config.domain == WATCH_DOMAIN_SPECTRUM) {
        const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;
        if (!resolve_frequency_axis_range(graph, &xmin, &xmax)) {
            return false;
        }
        resolve_spectral_static_range(config, &ymin, &ymax);
        x_intervals = config->nxticks == 0U ? DEFAULT_X_TICKS : config->nxticks;
        y_intervals = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
        x_format = TICK_FORMAT_FREQUENCY;
        y_format = TICK_FORMAT_NUMBER;
    } else if (graph->config.domain == WATCH_DOMAIN_SPECTROGRAM) {
        const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;
        if (!resolve_frequency_axis_range(graph, &ymin, &ymax)) {
            return false;
        }
        xmin = -config->history_seconds;
        xmax = 0.0f;
        x_intervals = config->nxticks == 0U ? DEFAULT_X_TICKS : config->nxticks;
        y_intervals = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
        x_format = TICK_FORMAT_TIME;
        y_format = TICK_FORMAT_FREQUENCY;
    } else {
        return false;
    }

    return ensure_tick_labels(graph, x_intervals, y_intervals, xmin, xmax, ymin, ymax, x_format, y_format);
}

static void position_graph_window(SDL_Window *window, uint32_t graph_slot) {
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0U) {
        display = SDL_GetPrimaryDisplay();
    }

    SDL_Rect usable;
    if (display == 0U || !SDL_GetDisplayUsableBounds(display, &usable)) {
        return;
    }

    uint32_t columns = usable.w > INITIAL_WIDTH ? (uint32_t) (usable.w / INITIAL_WIDTH) : 1U;
    uint32_t rows = usable.h > INITIAL_HEIGTH ? (uint32_t) (usable.h / INITIAL_HEIGTH) : 1U;
    uint32_t cell_count = columns * rows;
    uint32_t cell = cell_count > 0U ? graph_slot % cell_count : 0U;
    uint32_t column = cell % columns;
    uint32_t row = cell / columns;
    int cell_width = usable.w / (int) columns;
    int cell_height = usable.h / (int) rows;
    int horizontal_margin = (cell_width - INITIAL_WIDTH) / 2;
    int vertical_margin = (cell_height - INITIAL_HEIGTH) / 2;
    if (horizontal_margin < 0) {
        horizontal_margin = 0;
    }
    if (vertical_margin < 0) {
        vertical_margin = 0;
    }

    int x = usable.x + (int) column * cell_width + horizontal_margin;
    int y = usable.y + (int) row * cell_height + vertical_margin;
    if (!SDL_SetWindowPosition(window, x, y)) {
        SDL_Log("[watch-viewer] graph window positioning failed: %s", SDL_GetError());
    }
}

static VIEW_GRAPH *create_graph(VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS], const WATCH_MSG_CONFIG *config, const watch_endpoint_t *sender) {
    VIEW_GRAPH *graph = NULL;
    uint32_t graph_slot = 0U;
    for (uint32_t i = 0; i < MAX_VIEWER_GRAPHS; i++) {
        if (!graphs[i].ready) {
            graph = &graphs[i];
            graph_slot = i;
            break;
        }
    }
    if (graph == NULL) {
        return NULL;
    }

    memset(graph, 0, sizeof(*graph));
    graph->config = *config;
    graph->sender = *sender;
    graph->width = INITIAL_WIDTH;
    graph->heigth = INITIAL_HEIGTH;

    if (is_time_domain(config->domain)) {
        graph->scope_streams = prepare_scope_buffers();
        if (graph->scope_streams == NULL) {
            return NULL;
        }
    } else if (is_spectral_domain(config->domain)) {
        graph->spectral_streams = prepare_spectral_buffers();
        if (graph->spectral_streams == NULL) {
            return NULL;
        }
    } else if (is_ftable_domain(config->domain)) {
        graph->ftable_streams = prepare_ftable_buffers();
        if (graph->ftable_streams == NULL) {
            return NULL;
        }
    } else if (is_point_domain(config->domain)) {
        graph->point_streams = prepare_point_buffers();
        if (graph->point_streams == NULL) {
            return NULL;
        }
    } else if (is_meter_domain(config->domain)) {
        graph->meter_stream = prepare_meter_buffer();
        if (graph->meter_stream == NULL) {
            return NULL;
        }
    } else {
        return NULL;
    }

    const char *title = config->title[0] == '\0' ? "Csound Signal-Watcher" : config->title;
    if (!SDL_CreateWindowAndRenderer(
            title,
            INITIAL_WIDTH,
            INITIAL_HEIGTH,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &graph->window,
            &graph->renderer)) {
        if (is_time_domain(config->domain)) {
            free_scope_buffers(graph->scope_streams);
        } else if (is_spectral_domain(config->domain)) {
            free_spectral_buffers(graph->spectral_streams);
        } else if (is_point_domain(config->domain)) {
            free_point_buffers(graph->point_streams);
        } else if (is_meter_domain(config->domain)) {
            free_meter_buffer(graph->meter_stream);
        } else {
            free_ftable_buffers(graph->ftable_streams);
        }

        memset(graph, 0, sizeof(*graph));
        return NULL;
    }

    if (!SDL_SetWindowMinimumSize(graph->window, MINIMUM_WIDTH, MINIMUM_HEIGTH)) {
        SDL_Log(
            "[watch-viewer] graph %u minimum window size failed: %s",
            config->graph_id,
            SDL_GetError());
        destroy_graph(graph);
        return NULL;
    }

    position_graph_window(graph->window, graph_slot);

    SDL_SetRenderDrawBlendMode(graph->renderer, SDL_BLENDMODE_BLEND);
    update_graph_render_size(graph);
    if (!ensure_axis_labels(graph)) {
        SDL_Log("[watch-viewer] graph %u axis label initialization failed", config->graph_id);
        destroy_graph(graph);
        return NULL;
    }
    graph->window_id = SDL_GetWindowID(graph->window);
    graph->ready = true;
    if (!SDL_RaiseWindow(graph->window)) {
        SDL_Log(
            "[watch-viewer] graph %u could not be brought to the foreground: %s",
            config->graph_id,
            SDL_GetError());
    }
    return graph;
}

static bool send_config_ack(watch_socket_t receiver_socket, const watch_endpoint_t *sender, const WATCH_CONFIG_PACKET *config_packet) {
    WATCH_ACK_PACKET ack = {0};
    ack.header.magic = WATCH_MAGIC;
    ack.header.version = PROT_VERSION;
    ack.header.type = ACK;
    ack.header.sequence = config_packet->header.sequence;
    ack.header.payload_size = sizeof(WATCH_MSG_CONFIG_ACK);
    ack.ack.graph_id = config_packet->config.graph_id;

    uint32_t packet_size = (uint32_t) offsetof(WATCH_ACK_PACKET, ack) + (uint32_t) sizeof(WATCH_MSG_CONFIG_ACK);
    return watch_socket_send_to(receiver_socket, &ack, packet_size, sender) == 0;
}

static void apply_graph_theme(
    VIEW_GRAPH *graph,
    WATCH_GRAPH_THEME theme
) {
    if (graph->config.theme == theme) {
        return;
    }

    graph->config.theme = theme;
    destroy_text_label(&graph->x_axis_label);
    destroy_text_label(&graph->y_axis_label);
    destroy_tick_labels(&graph->tick_labels);
    graph->axis_label_density = 0.0f;

    if (graph->config.domain == WATCH_DOMAIN_SPECTROGRAM
        && graph->spectral_streams != NULL) {
        for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
            clear_spectral_stream(&graph->spectral_streams[i]);
        }
    }
}

/*
 * Configuration packets are retried by the sender until they are acknowledged,
 * so a condition that cannot be resolved here - no free viewer slot, a refused
 * window - would repeat its diagnostic four times per second for the whole
 * session.
 */
static bool should_report_config_failure(void) {
    static uint64_t last_report_ns = 0U;
    uint64_t now_ns = SDL_GetTicksNS();
    if (last_report_ns != 0U && now_ns - last_report_ns < CONFIG_FAILURE_LOG_INTERVAL_NS) {
        return false;
    }
    last_report_ns = now_ns;
    return true;
}

static void process_config_packet(
    watch_socket_t receiver_socket,
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_CONFIG_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_config_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph = find_graph(graphs, packet->config.graph_id, sender);
    if (graph == NULL) {
        graph = create_graph(graphs, &packet->config, sender);
        if (graph == NULL) {
            if (should_report_config_failure()) {
                SDL_Log("[watch-viewer] graph %u initialization failed", packet->config.graph_id);
            }
            return;
        }
    } else {
        apply_graph_theme(graph, packet->config.theme);
    }

    if (!send_config_ack(receiver_socket, sender, packet)
        && should_report_config_failure()) {
        SDL_Log("[watch-viewer] graph %u ACK failed", packet->config.graph_id);
    }
}

static uint32_t process_session_close_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_SESSION_CLOSE_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_session_close_packet(packet, received)) {
        return 0U;
    }

    uint32_t closed_graphs = 0U;
    for (uint32_t i = 0U; i < MAX_VIEWER_GRAPHS; i++) {
        if (graphs[i].ready
            && watch_endpoint_equal(&graphs[i].sender, sender)) {
            destroy_graph(&graphs[i]);
            closed_graphs++;
        }
    }
    return closed_graphs;
}

/*
 * One graph of a session that is still running, so the viewer keeps waiting for
 * the rest: a window closing here is not a reason to shut down, unlike the last
 * window being closed by hand.
 */
static void process_graph_close_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_GRAPH_CLOSE_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_graph_close_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph = find_graph(graphs, packet->close.graph_id, sender);
    if (graph != NULL) {
        destroy_graph(graph);
    }
}

static void process_data_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_DATA_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_data_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph = find_graph(graphs, packet->data.graph_id, sender);
    if (graph == NULL || !is_time_domain(graph->config.domain) || graph->scope_streams == NULL) {
        return;
    }

    float ymin, ymax;
    const WATCH_MSG_TIME_CONFIG *config = &graph->config.settings.time;
    resolve_scope_range(config, &ymin, &ymax);

    STREAM_TIME_BUFFER *stream = &graph->scope_streams[packet->data.stream_id];
    bool already_reported = stream->unsupported;
    if (!ensure_scope_buffer(stream, config->win_size, packet->data.sample_rate, ymin, ymax)) {
        if (already_reported) {
            return;
        }
        SDL_Log(
            "[watch-viewer] graph %u stream %u buffer initialization failed "
            "(window %.6g s, sample rate %.6g Hz)",
            packet->data.graph_id,
            packet->data.stream_id,
            (double) config->win_size,
            (double) packet->data.sample_rate);
        return;
    }

    append_scope_samples(stream, packet->data.samples, packet->data.sample_count, packet->header.sequence);
}

static void begin_spectral_frame(STREAM_SPECTRAL_BUFFER *stream, const WATCH_SPECTRAL_DATA_PACKET *packet) {
    memset(stream->received_bins, 0, stream->total_bins * sizeof(uint8_t));
    stream->received_count = 0U;
    stream->frame_sequence = packet->header.sequence;
    stream->frame_id = packet->data.frame_id;
    stream->has_frame = true;
    stream->assembling = true;
}

static bool append_spectrogram_frame(const WATCH_MSG_SPECTRAL_CONFIG *config, STREAM_SPECTRAL_BUFFER *stream, WATCH_GRAPH_THEME theme) {
    float minimum, maximum;
    resolve_spectral_static_range(config, &minimum, &maximum);

    for (uint32_t bin = 0; bin < stream->total_bins; bin++) {
        uint32_t texture_row = stream->total_bins - 1U - bin;
        float value = spectral_display_value(stream->assembly_bins[bin], config->scale);
        spectral_grayscale(
            value,
            minimum,
            maximum,
            theme,
            &stream->color_column[(size_t) texture_row * 4U]);
    }

    SDL_Rect column = {
        .x = (int) stream->history_write_index,
        .y = 0,
        .w = 1,
        .h = (int) stream->total_bins
    };
    if (!SDL_UpdateTexture(stream->history_texture, &column, stream->color_column, 4)) {
        return false;
    }

    stream->history_write_index = (stream->history_write_index + 1U) % stream->history_capacity;
    if (stream->valid_history_frames < stream->history_capacity) {
        stream->valid_history_frames++;
    }
    return true;
}

static void process_spectral_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_SPECTRAL_DATA_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_spectral_data_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph = find_graph(graphs, packet->data.graph_id, sender);
    if (graph == NULL || !is_spectral_domain(graph->config.domain) || graph->spectral_streams == NULL) {
        return;
    }

    STREAM_SPECTRAL_BUFFER *stream = &graph->spectral_streams[packet->data.stream_id];
    if (stream->has_frame && packet->header.sequence < stream->frame_sequence) {
        return;
    }
    if (stream->has_frame
        && packet->header.sequence == stream->frame_sequence
        && (packet->data.frame_id != stream->frame_id
            || packet->data.fft_size != stream->fft_size
            || packet->data.hop_size != stream->hop_size
            || packet->data.sample_rate != stream->sample_rate
            || packet->data.total_bins != stream->total_bins)) {
        return;
    }

    bool already_reported = stream->unsupported;
    if (!ensure_spectral_stream(graph, stream, &packet->data)) {
        if (already_reported) {
            return;
        }
        SDL_Log(
            "[watch-viewer] graph %u stream %u spectral buffer initialization failed "
            "(%u bins, %.6g s of history)",
            packet->data.graph_id,
            packet->data.stream_id,
            packet->data.total_bins,
            (double) graph->config.settings.spectral.history_seconds);
        return;
    }

    if (!stream->has_frame || packet->header.sequence > stream->frame_sequence) {
        begin_spectral_frame(stream, packet);
    } else if (packet->header.sequence < stream->frame_sequence || packet->data.frame_id != stream->frame_id || !stream->assembling) {
        return;
    }

    for (uint32_t i = 0; i < packet->data.bin_count; i++) {
        uint32_t destination = packet->data.bin_offset + i;
        float power = packet->data.bins[i];
        stream->assembly_bins[destination] = isfinite(power) && power >= 0.0f ? power : 0.0f;
        if (stream->received_bins[destination] == 0U) {
            stream->received_bins[destination] = 1U;
            stream->received_count++;
        }
    }

    if (stream->received_count != stream->total_bins) {
        return;
    }

    stream->assembling = false;
    const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;
    if (graph->config.domain == WATCH_DOMAIN_SPECTRUM) {
        memcpy(stream->display_bins, stream->assembly_bins, stream->total_bins * sizeof(float));
        stream->display_ready = true;
    } else if (!append_spectrogram_frame(config, stream, graph->config.theme)) {
        /*
         * The texture is unusable from here on: reject the stream so that the
         * next frames neither retry the update nor repeat the diagnostic.
         */
        reject_spectral_stream(stream, &packet->data);
        SDL_Log(
            "[watch-viewer] graph %u stream %u spectrogram history update failed",
            packet->data.graph_id,
            packet->data.stream_id);
    }
}

static bool reject_ftable_stream(STREAM_FTABLE_BUFFER *stream, const WATCH_MSG_FTABLE_DATA *data) {
    clear_ftable_stream(stream);
    stream->transfer_id = data->transfer_id;
    stream->total_samples = data->total_samples;
    stream->unsupported = true;
    return false;
}

static bool ensure_ftable_stream(STREAM_FTABLE_BUFFER *stream, const WATCH_MSG_FTABLE_DATA *data) {
    if (stream->samples != NULL && stream->transfer_id == data->transfer_id && stream->total_samples == data->total_samples) {
        return true;
    }
    if (stream->unsupported
        && stream->transfer_id == data->transfer_id
        && stream->total_samples == data->total_samples) {
        return false;
    }

    clear_ftable_stream(stream);
    stream->samples = calloc(data->total_samples, sizeof(float));
    stream->total_chunks = (data->total_samples + MAX_STREAM_SAMPLES - 1U) / MAX_STREAM_SAMPLES;
    stream->received_chunks = calloc(stream->total_chunks, sizeof(uint8_t));
    if (stream->samples == NULL || stream->received_chunks == NULL) {
        return reject_ftable_stream(stream, data);
    }

    stream->transfer_id = data->transfer_id;
    stream->total_samples = data->total_samples;
    stream->assembling = true;
    return true;
}

static bool prepare_ftable_envelope(STREAM_FTABLE_BUFFER *stream) {
    uint32_t count = stream->total_samples < MAX_FTABLE_RENDER_BUCKETS ? stream->total_samples : MAX_FTABLE_RENDER_BUCKETS;
    float *minimums = malloc((size_t) count * sizeof(float));
    float *maximums = malloc((size_t) count * sizeof(float));
    if (minimums == NULL || maximums == NULL) {
        free(minimums);
        free(maximums);
        return false;
    }

    for (uint32_t bucket = 0U; bucket < count; bucket++) {
        uint32_t first = (uint32_t) (((uint64_t) bucket * stream->total_samples) / count);
        uint32_t end = (uint32_t) (((uint64_t) (bucket + 1U) * stream->total_samples) / count);
        float minimum = stream->samples[first];
        float maximum = minimum;
        for (uint32_t i = first + 1U; i < end; i++) {
            minimum = fminf(minimum, stream->samples[i]);
            maximum = fmaxf(maximum, stream->samples[i]);
        }
        minimums[bucket] = minimum;
        maximums[bucket] = maximum;
    }

    stream->envelope_minimums = minimums;
    stream->envelope_maximums = maximums;
    stream->envelope_count = count;
    return true;
}

static void process_point_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_DATA_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_point_data_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph = find_graph(graphs, packet->data.graph_id, sender);
    if (graph == NULL || !is_point_domain(graph->config.domain) || graph->point_streams == NULL) {
        return;
    }

    STREAM_POINT_BUFFER *stream = &graph->point_streams[packet->data.stream_id];
    bool already_reported = stream->unsupported;
    if (!ensure_point_buffer(stream, packet->data.sample_rate)) {
        if (already_reported) {
            return;
        }
        SDL_Log(
            "[watch-viewer] graph %u stream %u point buffer initialization failed "
            "(sample rate %.6g Hz)",
            packet->data.graph_id,
            packet->data.stream_id,
            (double) packet->data.sample_rate);
        return;
    }

    for (uint32_t i = 0U; i < packet->data.sample_count; i++) {
        append_point(
            stream,
            packet->data.samples[i * 2U],
            packet->data.samples[i * 2U + 1U]);
    }
}

static void process_ftable_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_FTABLE_DATA_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_ftable_data_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph =
        find_graph(graphs, packet->data.graph_id, sender);
    if (graph == NULL
        || !is_ftable_domain(graph->config.domain)
        || graph->ftable_streams == NULL
        || packet->data.total_samples != graph->config.settings.ftable.win_size) {
        return;
    }

    STREAM_FTABLE_BUFFER *stream =
        &graph->ftable_streams[packet->data.stream_id];
    if (stream->display_ready
        && stream->transfer_id == packet->data.transfer_id
        && stream->total_samples == packet->data.total_samples) {
        return;
    }
    bool already_reported = stream->unsupported;
    if (!ensure_ftable_stream(stream, &packet->data)) {
        if (already_reported) {
            return;
        }
        SDL_Log(
            "[watch-viewer] graph %u stream %u ftable buffer allocation failed",
            packet->data.graph_id,
            packet->data.stream_id);
        return;
    }
    if (!stream->assembling) {
        return;
    }

    uint32_t chunk_index = packet->data.sample_offset / MAX_STREAM_SAMPLES;
    if (stream->received_chunks[chunk_index] != 0U) {
        return;
    }
    for (uint32_t i = 0U; i < packet->data.sample_count; i++) {
        float sample = packet->data.samples[i];
        sample = isfinite(sample) ? sample : 0.0f;
        stream->samples[packet->data.sample_offset + i] = sample;
        if (!stream->has_range) {
            stream->data_minimum = sample;
            stream->data_maximum = sample;
            stream->has_range = true;
        } else {
            stream->data_minimum = fminf(stream->data_minimum, sample);
            stream->data_maximum = fmaxf(stream->data_maximum, sample);
        }
    }
    stream->received_chunks[chunk_index] = 1U;
    stream->received_count += packet->data.sample_count;

    if (stream->received_count == stream->total_samples) {
        free(stream->received_chunks);
        stream->received_chunks = NULL;
        stream->assembling = false;
        if (!prepare_ftable_envelope(stream)) {
            SDL_Log(
                "[watch-viewer] graph %u stream %u ftable envelope "
                "allocation failed",
                packet->data.graph_id,
                packet->data.stream_id);
            return;
        }
        stream->display_ready = true;
        SDL_Log(
            "[watch-viewer] graph %u stream %u ftable transfer %u complete "
            "(%u samples)",
            packet->data.graph_id,
            packet->data.stream_id,
            packet->data.transfer_id,
            packet->data.total_samples);
    }
}

/*
 * The levels arrive ready to be plotted: they are whatever the user measured -
 * an rms, a follow, a decibel reading - expressed in the unit the graph was
 * configured with, so nothing is converted here.
 *
 * Only the attack is applied: packets arrive in bursts and at irregular
 * intervals, so decaying on arrival would tie the fall of the bar to the packet
 * rate. The release runs in render_meter(), against the wall clock.
 */
static void process_meter_packet(
    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS],
    const WATCH_METER_DATA_PACKET *packet,
    int64_t received,
    const watch_endpoint_t *sender
) {
    if (!validate_meter_data_packet(packet, received)) {
        return;
    }

    VIEW_GRAPH *graph = find_graph(graphs, packet->data.graph_id, sender);
    if (graph == NULL || !is_meter_domain(graph->config.domain) || graph->meter_stream == NULL) {
        return;
    }

    // the configuration decides how many bars exist; extra channels are ignored
    uint32_t channel_count = packet->data.channel_count;
    if (channel_count > graph->config.settings.meter.nchnls) {
        channel_count = graph->config.settings.meter.nchnls;
    }

    STREAM_METER_BUFFER *stream = graph->meter_stream;
    uint64_t now_ns = SDL_GetTicksNS();
    if (!stream->has_frame) {
        /*
         * Silence is the bottom of the axis, not zero: on a decibel meter zero
         * is full scale, and a freshly allocated buffer would show every bar
         * pinned to the top until the first release.
         */
        float ymin, ymax;
        resolve_meter_range(&graph->config.settings.meter, &ymin, &ymax);
        for (uint32_t i = 0U; i < MAX_METER_CHANNELS; i++) {
            stream->level[i] = ymin;
            stream->peak[i] = ymin;
        }
        stream->last_frame_ns = now_ns;
        stream->has_frame = true;
    }

    for (uint32_t i = 0U; i < channel_count; i++) {
        float level = packet->data.levels[i];
        if (!isfinite(level)) {
            continue;
        }
        /*
         * The bar takes the value as it comes, up or down: the packet already
         * carries the loudest value of one viewer frame, so holding it any
         * longer would only put the display behind the sound.
         */
        stream->level[i] = level;
        if (level >= stream->peak[i]) {
            stream->peak[i] = level;
            stream->peak_time_ns[i] = now_ns;
        }
    }
}

static bool spectral_bin_range(
    const WATCH_MSG_SPECTRAL_CONFIG *config,
    const STREAM_SPECTRAL_BUFFER *stream,
    uint32_t *first_bin,
    uint32_t *last_bin,
    float *minimum_frequency,
    float *maximum_frequency
) {
    float nyquist = stream->sample_rate * 0.5f;
    *minimum_frequency = config->frequency_range.is_min_auto ? 0.0f : fminf(fmaxf(config->frequency_range.min, 0.0f), nyquist);
    *maximum_frequency = config->frequency_range.is_max_auto ? nyquist : fminf(fmaxf(config->frequency_range.max, 0.0f), nyquist);
    if (*maximum_frequency <= *minimum_frequency) {
        return false;
    }

    double bin_width = (double) stream->sample_rate / stream->fft_size;
    uint32_t first = (uint32_t) ceil(*minimum_frequency / bin_width);
    uint32_t last = (uint32_t) floor(*maximum_frequency / bin_width);
    if (first >= stream->total_bins) {
        return false;
    }
    if (last >= stream->total_bins) {
        last = stream->total_bins - 1U;
    }
    if (last < first) {
        return false;
    }

    *first_bin = first;
    *last_bin = last;
    return true;
}

static void render_spectrum(VIEW_GRAPH *graph, PLOT_AREA *plot, bool supersampled) {
    const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;
    uint32_t nxticks = config->nxticks == 0U ? DEFAULT_X_TICKS : config->nxticks;
    uint32_t nyticks = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
    draw_grid(
        graph->renderer,
        plot,
        graph_pixel_density(graph),
        (int) nxticks,
        (int) nyticks,
        graph->config.theme);

    float minimum_value, maximum_value;
    resolve_spectral_static_range(config, &minimum_value, &maximum_value);

    SDL_Rect clip = get_plot_clip_rect(plot);
    SDL_SetRenderClipRect(graph->renderer, &clip);

    for (uint32_t stream_index = 0; stream_index < MAX_STREAMS; stream_index++) {
        STREAM_SPECTRAL_BUFFER *stream = &graph->spectral_streams[stream_index];
        if (!stream->display_ready || stream->display_bins == NULL || stream->plot_points == NULL) {
            continue;
        }
        set_stream_draw_color(graph->renderer, graph->config.theme, stream_index);

        uint32_t first_bin, last_bin;
        float minimum_frequency, maximum_frequency;
        if (!spectral_bin_range(config, stream, &first_bin, &last_bin, &minimum_frequency, &maximum_frequency)) {
            continue;
        }

        uint32_t point_count = last_bin - first_bin + 1U;
        for (uint32_t i = 0; i < point_count; i++) {
            uint32_t bin = first_bin + i;
            float frequency = (float) ((double) bin * stream->sample_rate / stream->fft_size);
            float value = spectral_display_value(stream->display_bins[bin], config->scale);
            float x_normalized = (frequency - minimum_frequency) / (maximum_frequency - minimum_frequency);
            float y_normalized = (value - minimum_value) / (maximum_value - minimum_value);
            stream->plot_points[i].x = plot->left + x_normalized * plot->plot_width;
            stream->plot_points[i].y = plot->bottom - y_normalized * plot->plot_heigth;
        }

        if (point_count == 1U) {
            draw_waveform_point(
                graph->renderer,
                stream->plot_points[0].x,
                stream->plot_points[0].y,
                supersampled);
        } else {
            draw_scope_polyline(graph->renderer, stream->plot_points, point_count, supersampled);
        }
    }

    SDL_SetRenderClipRect(graph->renderer, NULL);
}

static void render_spectrogram_history(SDL_Renderer *renderer, const PLOT_AREA *plot, const WATCH_MSG_SPECTRAL_CONFIG *config, STREAM_SPECTRAL_BUFFER *stream) {
    if (stream->history_texture == NULL || stream->valid_history_frames == 0U) {
        return;
    }

    uint32_t first_bin, last_bin;
    float minimum_frequency;
    float maximum_frequency;
    if (!spectral_bin_range(config, stream, &first_bin, &last_bin, &minimum_frequency, &maximum_frequency)) {
        return;
    }
    (void) minimum_frequency;
    (void) maximum_frequency;

    float source_y = (float) (stream->total_bins - 1U - last_bin);
    float source_height = (float) (last_bin - first_bin + 1U);
    if (stream->valid_history_frames < stream->history_capacity) {
        float fraction = (float) stream->valid_history_frames / stream->history_capacity;
        SDL_FRect source = {
            .x = 0.0f,
            .y = source_y,
            .w = (float) stream->valid_history_frames,
            .h = source_height
        };
        SDL_FRect destination = {
            .x = plot->right - plot->plot_width * fraction,
            .y = plot->top,
            .w = plot->plot_width * fraction,
            .h = plot->plot_heigth
        };
        SDL_RenderTexture(renderer, stream->history_texture, &source, &destination);
        return;
    }

    uint32_t oldest = stream->history_write_index;
    uint32_t first_count = stream->history_capacity - oldest;
    float first_fraction = (float) first_count / stream->history_capacity;

    if (first_count > 0U) {
        SDL_FRect source = {
            .x = (float) oldest,
            .y = source_y,
            .w = (float) first_count,
            .h = source_height
        };
        SDL_FRect destination = {
            .x = plot->left,
            .y = plot->top,
            .w = plot->plot_width * first_fraction,
            .h = plot->plot_heigth
        };
        SDL_RenderTexture(renderer, stream->history_texture, &source, &destination);
    }

    if (oldest > 0U) {
        SDL_FRect source = {
            .x = 0.0f,
            .y = source_y,
            .w = (float) oldest,
            .h = source_height
        };
        SDL_FRect destination = {
            .x = plot->left + plot->plot_width * first_fraction,
            .y = plot->top,
            .w = plot->plot_width * (1.0f - first_fraction),
            .h = plot->plot_heigth
        };
        SDL_RenderTexture(renderer, stream->history_texture, &source, &destination);
    }
}

static void render_spectrogram(VIEW_GRAPH *graph, PLOT_AREA *plot) {
    const WATCH_MSG_SPECTRAL_CONFIG *config = &graph->config.settings.spectral;

    for (uint32_t stream_index = 0; stream_index < MAX_STREAMS; stream_index++) {
        STREAM_SPECTRAL_BUFFER *stream = &graph->spectral_streams[stream_index];
        if (stream->history_texture != NULL && stream->valid_history_frames > 0U) {
            render_spectrogram_history(graph->renderer, plot, config, stream);
        }
    }

    uint32_t nxticks = config->nxticks == 0U ? DEFAULT_X_TICKS : config->nxticks;
    uint32_t nyticks = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
    draw_grid(
        graph->renderer,
        plot,
        graph_pixel_density(graph),
        (int) nxticks,
        (int) nyticks,
        graph->config.theme);
}

static float ftable_sample_y(const PLOT_AREA *plot, float sample, float ymin, float ymax) {
    float normalized = (sample - ymin) / (ymax - ymin);
    return plot->bottom - normalized * plot->plot_heigth;
}

static void render_ftable(VIEW_GRAPH *graph, PLOT_AREA *plot, bool supersampled) {
    uint32_t nxticks = DEFAULT_X_TICKS;
    uint32_t nyticks = DEFAULT_Y_TICKS;
    draw_grid(
        graph->renderer,
        plot,
        graph_pixel_density(graph),
        (int) nxticks,
        (int) nyticks,
        graph->config.theme);

    float ymin, ymax;
    if (!resolve_ftable_range(graph, &ymin, &ymax)) {
        return;
    }

    if (ymin <= 0.0f && ymax >= 0.0f) {
        float zero = ftable_sample_y(plot, 0.0f, ymin, ymax);
        set_themed_draw_color(
            graph->renderer,
            graph->config.theme,
            0U,
            255U);
        SDL_RenderLine(graph->renderer, plot->left, zero, plot->right - 1.0f, zero);
    }

    SDL_Rect clip = get_plot_clip_rect(plot);
    SDL_SetRenderClipRect(graph->renderer, &clip);

    float raster_width = plot->right - plot->left;
    uint32_t column_count = raster_width >= 0.0f ? (uint32_t) floorf(raster_width) + 1U : 0U;

    for (uint32_t stream_index = 0U;
         stream_index < MAX_STREAMS;
         stream_index++) {
        STREAM_FTABLE_BUFFER *stream = &graph->ftable_streams[stream_index];
        if (!stream->display_ready || stream->samples == NULL || stream->total_samples == 0U) {
            continue;
        }
        set_stream_draw_color(graph->renderer, graph->config.theme, stream_index);

        if (column_count > 0U
            && (uint64_t) stream->total_samples > (uint64_t) column_count * 2U) {
            bool has_previous = false;
            float previous_x = 0.0f;
            float previous_y = 0.0f;
            for (uint32_t column = 0U; column < column_count; column++) {
                uint32_t first = (uint32_t) (((uint64_t) column * stream->envelope_count) / column_count);
                uint32_t end = (uint32_t) (((uint64_t) (column + 1U) * stream->envelope_count) / column_count);
                if (end <= first) {
                    end = first + 1U;
                }

                float minimum = stream->envelope_minimums[first];
                float maximum = stream->envelope_maximums[first];
                for (uint32_t i = first + 1U; i < end; i++) {
                    minimum = fminf(minimum, stream->envelope_minimums[i]);
                    maximum = fmaxf(maximum, stream->envelope_maximums[i]);
                }
                uint64_t first_sample_count = ((uint64_t) column * stream->total_samples) / column_count;
                uint64_t end_sample_count = ((uint64_t) (column + 1U) * stream->total_samples) / column_count;
                uint32_t first_sample = (uint32_t) first_sample_count;
                uint32_t end_sample = (uint32_t) end_sample_count;
                float sample_col = (float) column * raster_width / (float) (column_count - 1U);
                float x = column_count > 1U ? plot->left + sample_col : plot->left;

                float s = stream->samples[first_sample];
                float first_y = ftable_sample_y(plot, s, ymin, ymax);
                if (has_previous) {
                    draw_waveform_line(
                        graph->renderer,
                        previous_x,
                        previous_y,
                        x,
                        first_y,
                        supersampled);
                }

                float top = ftable_sample_y(plot, maximum, ymin, ymax);
                float bottom = ftable_sample_y(plot, minimum, ymin, ymax);
                draw_scope_vertical(
                    graph->renderer,
                    x,
                    top,
                    bottom,
                    supersampled);
                previous_x = x;
                previous_y = ftable_sample_y(
                    plot,
                    stream->samples[end_sample - 1U],
                    ymin,
                    ymax);
                has_previous = true;
            }
            continue;
        }

        if (stream->total_samples == 1U) {
            float f = ftable_sample_y(plot, stream->samples[0], ymin, ymax);
            draw_waveform_point(graph->renderer, plot->left, f, supersampled);
            continue;
        }

        float x_step = plot->plot_width / (float) (stream->total_samples - 1U);
        float previous_x = plot->left;
        float previous_y = ftable_sample_y(plot, stream->samples[0], ymin, ymax);
        for (uint32_t i = 1U; i < stream->total_samples; i++) {
            float x = plot->left + (float) i * x_step;
            float y = ftable_sample_y(plot, stream->samples[i], ymin, ymax);
            draw_waveform_line(graph->renderer, previous_x, previous_y, x, y, supersampled);
            previous_x = x;
            previous_y = y;
        }
    }

    SDL_SetRenderClipRect(graph->renderer, NULL);
}

/*
 * A plane must keep equal units per pixel on both axes, otherwise a circular
 * path is drawn as an ellipse. The largest centred square inside the plot area
 * is used and every label follows it, because they are positioned from the same
 * PLOT_AREA.
 */
static PLOT_AREA square_plot_area(const PLOT_AREA *plot) {
    PLOT_AREA squared = *plot;
    if (plot->plot_width <= 0.0f || plot->plot_heigth <= 0.0f) {
        return squared;
    }

    float side = fminf(plot->plot_width, plot->plot_heigth);
    squared.left = plot->x_center - side * 0.5f;
    squared.right = plot->x_center + side * 0.5f;
    squared.top = plot->y_center - side * 0.5f;
    squared.bottom = plot->y_center + side * 0.5f;
    squared.plot_width = side;
    squared.plot_heigth = side;
    return squared;
}

static float point_axis_x(const PLOT_AREA *plot, float value, float minimum, float maximum) {
    return plot->left + ((value - minimum) / (maximum - minimum)) * plot->plot_width;
}

static float point_axis_y(const PLOT_AREA *plot, float value, float minimum, float maximum) {
    return plot->bottom - ((value - minimum) / (maximum - minimum)) * plot->plot_heigth;
}

static void render_point(VIEW_GRAPH *graph, PLOT_AREA *plot, bool supersampled) {
    const WATCH_MSG_POINT_CONFIG *config = &graph->config.settings.point;
    float density = graph_pixel_density(graph);
    draw_grid(
        graph->renderer,
        plot,
        density,
        (int) POINT_TICKS,
        (int) POINT_TICKS,
        graph->config.theme);

    float xmin, xmax, ymin, ymax;
    resolve_point_range(&config->xrange, &xmin, &xmax);
    resolve_point_range(&config->yrange, &ymin, &ymax);
    if (!(xmax > xmin) || !(ymax > ymin)) {
        return;
    }

    set_themed_draw_color(graph->renderer, graph->config.theme, 0U, 255U);
    if (ymin <= 0.0f && ymax >= 0.0f) {
        float zero = point_axis_y(plot, 0.0f, ymin, ymax);
        SDL_RenderLine(graph->renderer, plot->left, zero, plot->right - 1.0f, zero);
    }
    if (xmin <= 0.0f && xmax >= 0.0f) {
        float zero = point_axis_x(plot, 0.0f, xmin, xmax);
        SDL_RenderLine(graph->renderer, zero, plot->top, zero, plot->bottom - 1.0f);
    }

    SDL_Rect clip = get_plot_clip_rect(plot);
    SDL_SetRenderClipRect(graph->renderer, &clip);

    for (uint32_t stream_index = 0U; stream_index < MAX_STREAMS; stream_index++) {
        STREAM_POINT_BUFFER *stream = &graph->point_streams[stream_index];
        if (stream->coordinates == NULL || stream->plot_points == NULL || stream->valid_points == 0U) {
            continue;
        }

        uint32_t first = (stream->write_index + stream->capacity - stream->valid_points) % stream->capacity;
        for (uint32_t i = 0U; i < stream->valid_points; i++) {
            size_t base = (size_t) ((first + i) % stream->capacity) * 2U;
            stream->plot_points[i].x = point_axis_x(plot, stream->coordinates[base], xmin, xmax);
            stream->plot_points[i].y = point_axis_y(plot, stream->coordinates[base + 1U], ymin, ymax);
        }

        set_stream_draw_color(graph->renderer, graph->config.theme, stream_index);

        uint8_t red, green, blue, alpha;
        SDL_GetRenderDrawColor(graph->renderer, &red, &green, &blue, &alpha);

        /*
         * The trail fades towards the oldest end so that the direction of the
         * movement is readable from a single frame.
         */
        for (uint32_t i = 1U; i < stream->valid_points; i++) {
            float age = stream->valid_points > 1U ? (float) i / (float) (stream->valid_points - 1U) : 1.0f;
            float segment_alpha = POINT_TRAIL_MIN_ALPHA + (255.0f - POINT_TRAIL_MIN_ALPHA) * age;
            SDL_SetRenderDrawColor(graph->renderer, red, green, blue, (uint8_t) lrintf(segment_alpha));
            SDL_RenderLine(
                graph->renderer,
                stream->plot_points[i - 1U].x,
                stream->plot_points[i - 1U].y,
                stream->plot_points[i].x,
                stream->plot_points[i].y);
        }

        SDL_SetRenderDrawColor(graph->renderer, red, green, blue, 255U);
        const SDL_FPoint *head = &stream->plot_points[stream->valid_points - 1U];
        float size = POINT_HEAD_SIZE * density * (supersampled ? 1.0f : 0.8f);
        SDL_FRect head_rect = {
            .x = head->x - size * 0.5f,
            .y = head->y - size * 0.5f,
            .w = size,
            .h = size
        };
        SDL_RenderFillRect(graph->renderer, &head_rect);
    }

    SDL_SetRenderClipRect(graph->renderer, NULL);
}

static float meter_value_y(const PLOT_AREA *plot, float value, float ymin, float ymax) {
    value = fminf(fmaxf(value, ymin), ymax);
    return plot->bottom - ((value - ymin) / (ymax - ymin)) * plot->plot_heigth;
}

static void render_meter(VIEW_GRAPH *graph, PLOT_AREA *plot) {
    const WATCH_MSG_METER_CONFIG *config = &graph->config.settings.meter;
    uint32_t nchnls = config->nchnls;
    float density = graph_pixel_density(graph);

    float ymin;
    float ytop;
    uint32_t divisions;
    resolve_meter_axis(config, &ymin, &ytop, &divisions);

    draw_meter(graph->renderer, plot, density, nchnls, (int) divisions, graph->config.theme);

    STREAM_METER_BUFFER *stream = graph->meter_stream;
    if (stream == NULL || !stream->has_frame || nchnls == 0U || !(ytop > ymin)) {
        return;
    }

    uint64_t now_ns = SDL_GetTicksNS();
    float elapsed = (float) ((double) (now_ns - stream->last_frame_ns) / 1.0e9);
    stream->last_frame_ns = now_ns;
    if (!(elapsed > 0.0f) || elapsed > 1.0f) {
        elapsed = 0.0f;
    }

    float slot = plot->plot_width / (float) nchnls;
    float bar_width = slot * METER_BAR_FILL;
    float peak_thickness = fmaxf(METER_PEAK_THICKNESS * density, 1.0f);

    SDL_Rect clip = get_plot_clip_rect(plot);
    SDL_SetRenderClipRect(graph->renderer, &clip);

    for (uint32_t i = 0U; i < nchnls; i++) {
        /*
         * Only the peak falls on its own, and only once it has been held: the
         * bar carries the level as received, so it reaches the eye a frame
         * after the sound rather than a release later.
         *
         * The fall stops on the bar. Nothing else does: the bar no longer
         * decays along with it, so a peak left above a steady level would sink
         * through it and end up as a mark drawn across the middle of a bar.
         */
        float held = (float) ((double) (now_ns - stream->peak_time_ns[i]) / 1.0e9);
        if (held > METER_PEAK_HOLD_SECONDS) {
            stream->peak[i] = ymin + (stream->peak[i] - ymin) * expf(-elapsed / METER_PEAK_RELEASE_SECONDS);
        }
        stream->peak[i] = fmaxf(stream->peak[i], stream->level[i]);

        float center_x = plot->left + ((float) i + 0.5f) * slot;
        float bar_left = center_x - bar_width * 0.5f;

        float level_value = stream->level[i];
        if (level_value > ymin) {
            // one color for every bar: they are told apart by their position
            set_stream_draw_color(graph->renderer, graph->config.theme, 0U);
            float level_y = meter_value_y(plot, level_value, ymin, ytop);
            SDL_FRect bar = {
                .x = bar_left,
                .y = level_y,
                .w = bar_width,
                .h = plot->bottom - level_y
            };
            SDL_RenderFillRect(graph->renderer, &bar);
        }

        float peak_value = stream->peak[i];
        if (peak_value > ymin) {
            set_meter_peak_color(graph->renderer, graph->config.theme);
            float peak_y = meter_value_y(plot, peak_value, ymin, ytop);
            SDL_FRect peak_mark = {
                .x = bar_left,
                .y = peak_y - peak_thickness * 0.5f,
                .w = bar_width,
                .h = peak_thickness
            };
            SDL_RenderFillRect(graph->renderer, &peak_mark);
        }
    }

    SDL_SetRenderClipRect(graph->renderer, NULL);
}

static void render_graph(VIEW_GRAPH *graph) {
    if (!graph->ready || graph->renderer == NULL) {
        return;
    }

    if (!ensure_axis_labels(graph)) {
        return;
    }
    (void) ensure_graph_tick_labels(graph);

    /*
     * The scale belongs to the supersampled target: setting it before knowing
     * whether that target exists leaves the window renderer scaled by two when
     * the texture could not be created, and the frame is then drawn into a
     * quarter of the window.
     */
    bool supersampled = ensure_graph_render_target(graph);
    if (supersampled) {
        supersampled =
            SDL_SetRenderTarget(graph->renderer, graph->render_target)
            && SDL_SetRenderScale(graph->renderer, (float) RENDER_SUPERSAMPLE_SCALE, (float) RENDER_SUPERSAMPLE_SCALE);
    }
    if (!supersampled) {
        SDL_SetRenderTarget(graph->renderer, NULL);
        SDL_SetRenderScale(graph->renderer, 1.0f, 1.0f);
    }

    PLOT_AREA plot = get_plot_area(graph);
    if (is_point_domain(graph->config.domain)) {
        plot = square_plot_area(&plot);
    }
    SDL_FRect border = {
        .x = plot.left,
        .y = plot.top,
        .w = plot.plot_width,
        .h = plot.plot_heigth
    };

    set_themed_draw_color(graph->renderer, graph->config.theme, 224U, 255U);
    SDL_RenderClear(graph->renderer);
    set_themed_draw_color(graph->renderer, graph->config.theme, 255U, 255U);
    SDL_RenderFillRect(graph->renderer, &border);
    set_themed_draw_color(graph->renderer, graph->config.theme, 0U, 255U);
    SDL_RenderRect(graph->renderer, &border);

    if (is_time_domain(graph->config.domain)) {
        const WATCH_MSG_TIME_CONFIG *config = &graph->config.settings.time;
        uint32_t nxticks = config->nxticks == 0U ? DEFAULT_X_TICKS : config->nxticks;
        uint32_t nyticks = config->nyticks == 0U ? DEFAULT_Y_TICKS : config->nyticks;
        draw_grid(
            graph->renderer,
            &plot,
            graph_pixel_density(graph),
            (int) nxticks,
            (int) nyticks,
            graph->config.theme);

        float ymin;
        float ymax;
        resolve_scope_range(config, &ymin, &ymax);
        if (ymin <= 0.0f && ymax >= 0.0f) {
            float zero = plot.bottom - ((0.0f - ymin) / (ymax - ymin)) * plot.plot_heigth;
            set_themed_draw_color(graph->renderer, graph->config.theme, 0U, 255U);
            SDL_RenderLine(graph->renderer, plot.left, zero, plot.right - 1.0f, zero);
        }

        SDL_Rect clip = get_plot_clip_rect(&plot);
        SDL_SetRenderClipRect(graph->renderer, &clip);
        uint64_t now_ns = SDL_GetTicksNS();
        if (advance_scope_timeline(graph, now_ns)) {
            for (uint32_t i = 0; i < MAX_STREAMS; i++) {
                draw_scope_stream(
                    graph->renderer,
                    &plot,
                    &graph->scope_streams[i],
                    i,
                    graph->scope_display_end_sample,
                    supersampled,
                    graph->config.theme);
            }
        }
        SDL_SetRenderClipRect(graph->renderer, NULL);
    } else if (graph->config.domain == WATCH_DOMAIN_SPECTRUM) {
        render_spectrum(graph, &plot, supersampled);
    } else if (graph->config.domain == WATCH_DOMAIN_SPECTROGRAM) {
        render_spectrogram(graph, &plot);
    } else if (graph->config.domain == WATCH_DOMAIN_FTABLE) {
        render_ftable(graph, &plot, supersampled);
    } else if (graph->config.domain == WATCH_DOMAIN_POINT) {
        render_point(graph, &plot, supersampled);
    } else if (graph->config.domain == WATCH_DOMAIN_METER) {
        render_meter(graph, &plot);
    }

    set_themed_draw_color(
        graph->renderer,
        graph->config.theme,
        0U,
        255U);
    SDL_RenderRect(graph->renderer, &border);
    draw_tick_labels(graph, &plot);
    draw_axis_labels(graph, &plot);

    if (supersampled) {
        SDL_SetRenderTarget(graph->renderer, NULL);
        SDL_SetRenderScale(graph->renderer, 1.0f, 1.0f);
        SDL_RenderTexture(graph->renderer, graph->render_target, NULL, NULL);
    }

    SDL_RenderPresent(graph->renderer);
}


int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    VIEW_GRAPH graphs[MAX_VIEWER_GRAPHS] = {0};
    watch_socket_t receiver_socket = WATCH_INVALID_SOCKET;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("[watch-viewer] SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    if (!TTF_Init()) {
        SDL_Log("[watch-viewer] SDL_ttf initialization failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (watch_net_init() != 0) {
        SDL_Log("[watch-viewer] network initialization failed");
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    receiver_socket = watch_socket_udp_bind(WATCH_VIEWER_ADDRESS, WATCH_VIEWER_PORT);
    if (receiver_socket == WATCH_INVALID_SOCKET) {
        SDL_Log("[watch-viewer] UDP bind failed (error %d)", watch_socket_last_error());
        watch_net_cleanup();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    const uint64_t frame_interval_ns = NANOSECONDS_PER_SECOND / VIEWER_REFRESH_HZ;
    uint64_t next_frame_ns = SDL_GetTicksNS();
    uint64_t shutdown_deadline_ns = 0U;
    int running = 1;
    while (running) {

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    VIEW_GRAPH *graph = find_graph_by_window(graphs, event.window.windowID);
                    if (graph != NULL) {
                        destroy_graph(graph);
                        if (!has_opened_graph(graphs)) {
                            running = 0;
                        }
                    }
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                {
                    VIEW_GRAPH *graph = find_graph_by_window(graphs, event.window.windowID);
                    if (graph != NULL) {
                        update_graph_render_size(graph);
                    }
                    break;
                }
            }
        }

        if (!running) {
            break;
        }

        for (uint32_t i = 0; i < MAX_PACKETS_PER_FRAME; i++) {
            WATCH_INCOMING_PACKET packet;
            watch_endpoint_t sender;

            int64_t received = watch_socket_receive_from(receiver_socket, &packet, sizeof(packet), &sender);
            if (received < 0) {
                int32_t err = watch_socket_last_error();
                if (!watch_socket_would_block(err)) {
                    SDL_Log("[watch-viewer] receive failed: %d", err);
                }
                break;
            }

            if (!validate_header(&packet.header, received)) {
                continue;
            }

            switch (packet.header.type) {
                case CONFIG:
                    process_config_packet(receiver_socket, graphs, &packet.config, received, &sender);
                    if (has_opened_graph(graphs)) shutdown_deadline_ns = 0U;
                    break;
                case DATA:
                    process_data_packet(graphs, &packet.data, received, &sender);
                    break;
                case SPECTRAL_DATA:
                    process_spectral_packet(graphs, &packet.spectral, received, &sender);
                    break;
                case FTABLE_DATA:
                    process_ftable_packet(graphs, &packet.ftable, received, &sender);
                    break;
                case POINT_DATA:
                    process_point_packet(graphs, &packet.data, received, &sender);
                    break;
                case METER_DATA:
                    process_meter_packet(graphs, &packet.meter, received, &sender);
                    break;
                case GRAPH_CLOSE:
                    process_graph_close_packet(graphs, &packet.graph_close, received, &sender);
                    break;
                case SESSION_CLOSE:
                    if (process_session_close_packet(graphs, &packet.close, received, &sender) > 0U && !has_opened_graph(graphs)) {
                        shutdown_deadline_ns = SDL_GetTicksNS() + (uint64_t) VIEWER_SHUTDOWN_DELAY_MS * 1000000ULL;
                    }
                    break;
                case ACK:
                default:
                    break;
            }
        }

        if (!running) {
            break;
        }

        if (shutdown_deadline_ns != 0U && !has_opened_graph(graphs) && SDL_GetTicksNS() >= shutdown_deadline_ns) {
            running = 0;
            break;
        }

        for (uint32_t i = 0; i < MAX_VIEWER_GRAPHS; i++) {
            render_graph(&graphs[i]);
        }

        next_frame_ns += frame_interval_ns;
        uint64_t now_ns = SDL_GetTicksNS();
        if (next_frame_ns > now_ns) {
            SDL_DelayPrecise(next_frame_ns - now_ns);
        } else if (now_ns - next_frame_ns > frame_interval_ns) {
            /*
             * Do not try to render missed frames: resume from the current
             * time instead of producing a burst of catch-up frames.
             */
            next_frame_ns = now_ns;
        }
    }

    for (uint32_t i = 0; i < MAX_VIEWER_GRAPHS; i++) {
        destroy_graph(&graphs[i]);
    }

    watch_socket_close(receiver_socket);
    watch_net_cleanup();
    TTF_Quit();
    SDL_Quit();
    return 0;
}
