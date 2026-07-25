#include "watch.h"
#include "watch_process.h"
#include "watch_protocol.h"
#include <pstream.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>


_Static_assert(
    offsetof(WATCH_DATA_PACKET, data) == sizeof(WATCH_MSG_HEADER),
    "time-data packet header must be contiguous with its payload"
);
_Static_assert(
    offsetof(WATCH_SPECTRAL_DATA_PACKET, data) == sizeof(WATCH_MSG_HEADER),
    "spectral-data packet header must be contiguous with its payload"
);
_Static_assert(
    offsetof(WATCH_FTABLE_DATA_PACKET, data) == sizeof(WATCH_MSG_HEADER),
    "ftable-data packet header must be contiguous with its payload"
);
_Static_assert(
    offsetof(WATCH_SESSION_CLOSE_PACKET, close) == sizeof(WATCH_MSG_HEADER),
    "session-close packet header must be contiguous with its payload"
);

static void try_launch_viewer(WATCH_MANAGER *manager, uint64_t now_ms) {
    if (manager->viewer_probe_started_ms == UINT64_MAX) {
        manager->viewer_probe_started_ms = now_ms;
        return;
    }

    if (now_ms - manager->viewer_probe_started_ms < WATCH_VIEWER_PROBE_DELAY_MS) {
        return;
    }
    if (manager->viewer_last_launch_ms != UINT64_MAX
        && now_ms - manager->viewer_last_launch_ms < WATCH_VIEWER_RELAUNCH_DELAY_MS) {
        return;
    }

    manager->viewer_last_launch_ms = now_ms;
    int32_t error_code = watch_process_launch_viewer();
    if (error_code != 0) {
        manager->csound->ErrorMsg(manager->csound, "[watch] could not launch watch_viewer (error %d)\n", error_code);
    }
}

static int32_t string_to_title(char *dest, const STRINGDAT *src) {
    if (src == NULL || src->data == NULL) {
        dest[0] = '\0';
        return OK;
    }

    int32_t written = snprintf(dest, MAX_TITLE_SIZE, "%s", src->data);
    if (written < 0 || written >= MAX_TITLE_SIZE) {
        dest[0] = '\0';
        return NOTOK;
    }

    return OK;
}

static void free_stream(CSOUND *csound, WATCH_STREAM *stream) {
    if (stream == NULL) {
        return;
    }
    csound->Free(csound, stream->ftable_samples);
    csound->Free(csound, stream);
}

static void free_graph(CSOUND *csound, WATCH_GRAPH *graph) {
    if (graph == NULL) {
        return;
    }

    if (graph->streams != NULL) {
        for (uint32_t i = 0; i < MAX_STREAMS; i++) {
            free_stream(csound, graph->streams[i]);
        }
        csound->Free(csound, graph->streams);
    }

    csound->Free(csound, graph);
}

static WATCH_GRAPH *find_graph(WATCH_MANAGER *manager, uint32_t handle) {
    for (uint32_t i = 0; i < manager->graph_capacity; i++) {
        WATCH_GRAPH *candidate = manager->graphs[i];
        if (candidate != NULL && candidate->data_config.config.graph_id == handle) {
            return candidate;
        }
    }
    return NULL;
}

static watch_socket_t create_socket_udp(CSOUND *csound) {
    watch_socket_t socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd == WATCH_INVALID_SOCKET) {
        csound->ErrorMsg(csound, "[watch] UDP socket creation failed (error %d)\n", watch_socket_last_error());
        return WATCH_INVALID_SOCKET;
    }

    struct sockaddr_in viewer_address = {0};
    viewer_address.sin_family = AF_INET;
    viewer_address.sin_port = htons(WATCH_VIEWER_PORT);

    if (inet_pton(AF_INET, WATCH_VIEWER_ADDRESS, &viewer_address.sin_addr) != 1) {
        csound->ErrorMsg(csound, "[watch] invalid viewer address: %s\n", WATCH_VIEWER_ADDRESS);
        watch_socket_close(socket_fd);
        return WATCH_INVALID_SOCKET;
    }

    if (connect(socket_fd, (const struct sockaddr *) &viewer_address, (int) sizeof(viewer_address)) != 0) {
        csound->ErrorMsg(csound, "[watch] UDP socket connect failed (error %d)\n", watch_socket_last_error());
        watch_socket_close(socket_fd);
        return WATCH_INVALID_SOCKET;
    }

    if (watch_socket_set_nonblocking(socket_fd) != 0) {
        csound->ErrorMsg(csound, "[watch] could not make UDP socket non-blocking (error %d)\n", watch_socket_last_error());
        watch_socket_close(socket_fd);
        return WATCH_INVALID_SOCKET;
    }

    return socket_fd;
}

static int32_t send_packet(WATCH_MANAGER *manager, const void *packet, uint32_t packet_size) {
#if defined(_WIN32)
    int sent = send(manager->socket_fd, (const char *) packet, (int) packet_size, 0);
    return sent == (int) packet_size ? OK : NOTOK;
#else
    ssize_t sent = send(manager->socket_fd, packet, (size_t) packet_size, 0);
    return sent == (ssize_t) packet_size ? OK : NOTOK;
#endif
}

static void send_session_close(WATCH_MANAGER *manager) {
    WATCH_SESSION_CLOSE_PACKET packet = {0};
    packet.header.magic = WATCH_MAGIC;
    packet.header.version = PROT_VERSION;
    packet.header.type = SESSION_CLOSE;
    packet.header.sequence = manager->csound->GetCurrentTimeSamples(manager->csound);
    packet.header.payload_size = sizeof(WATCH_MSG_SESSION_CLOSE);

    uint32_t packet_size = (uint32_t) offsetof(WATCH_SESSION_CLOSE_PACKET, close) + (uint32_t) sizeof(WATCH_MSG_SESSION_CLOSE);
    for (uint32_t attempt = 0U; attempt < WATCH_SESSION_CLOSE_REPETITIONS; attempt++) {
        (void) send_packet(manager, &packet, packet_size);
    }
}

static int64_t receive_packet(WATCH_MANAGER *manager, void *packet, uint32_t packet_capacity) {
#if defined(_WIN32)
    return (int64_t) recv(manager->socket_fd, (char *) packet, (int) packet_capacity, 0);
#else
    return (int64_t) recv(manager->socket_fd, packet, (size_t) packet_capacity, 0);
#endif
}

static void receive_acks(WATCH_MANAGER *manager) {
    CSOUND *csound = manager->csound;

    while (atomic_load_explicit(&manager->running, memory_order_acquire)) {
        WATCH_ACK_PACKET packet;
        int64_t received = receive_packet(manager, &packet, sizeof(packet));

        if (received < 0) {
            int32_t error_code = watch_socket_last_error();
            if (watch_socket_would_block(error_code)) {
                return;
            }
            return;
        }

        uint32_t expected_size = (uint32_t) offsetof(WATCH_ACK_PACKET, ack) + (uint32_t) sizeof(WATCH_MSG_CONFIG_ACK);
        if (received != (int64_t) expected_size
            || packet.header.magic != WATCH_MAGIC
            || packet.header.version != PROT_VERSION
            || packet.header.type != ACK
            || packet.header.payload_size != sizeof(WATCH_MSG_CONFIG_ACK)) {
            continue;
        }

        csound->LockMutex(manager->registry_mutex);
        WATCH_GRAPH *graph = find_graph(manager, packet.ack.graph_id);
        if (graph != NULL
            && packet.header.sequence
                == graph->data_config.header.sequence) {
            graph->is_config_acked = true;
        }
        csound->UnlockMutex(manager->registry_mutex);
    }
}

static void send_stream_packets(WATCH_MANAGER *manager, WATCH_STREAM *stream) {
    for (uint32_t sent_count = 0; sent_count < WATCH_PACKETS_PER_STREAM_PASS; sent_count++) {
        unsigned read = atomic_load_explicit(&stream->read_pos, memory_order_relaxed);
        unsigned write = atomic_load_explicit(&stream->write_pos, memory_order_acquire);
        if (read == write) {
            return;
        }

        WATCH_STREAM_PACKET *packet = &stream->slots[read];
        uint32_t max_payload_size;
        if (packet->header.type == DATA) {
            max_payload_size = sizeof(WATCH_MSG_DATA);
        } else if (packet->header.type == SPECTRAL_DATA) {
            max_payload_size = sizeof(WATCH_MSG_SPECTRAL_DATA);
        } else if (packet->header.type == FTABLE_DATA) {
            max_payload_size = sizeof(WATCH_MSG_FTABLE_DATA);
        } else {
            max_payload_size = 0U;
        }

        if (packet->header.payload_size > max_payload_size) {
            unsigned next_read = (read + 1U) % MAX_QUEUE_SIZE;
            atomic_store_explicit(&stream->read_pos, next_read, memory_order_release);
            continue;
        }

        uint32_t packet_size = (uint32_t) sizeof(WATCH_MSG_HEADER) + packet->header.payload_size;
        if (send_packet(manager, packet, packet_size) != OK) {
            return;
        }

        unsigned next_read = (read + 1U) % MAX_QUEUE_SIZE;
        atomic_store_explicit(&stream->read_pos, next_read, memory_order_release);
    }
}

static void send_ftable_packets(WATCH_MANAGER *manager, WATCH_STREAM *stream) {
    CSOUND *csound = manager->csound;
    for (uint32_t sent_count = 0U;
         sent_count < WATCH_PACKETS_PER_STREAM_PASS
             && stream->ftable_samples != NULL
             && stream->ftable_next_sample < stream->ftable_total_samples;
         sent_count++) {
        uint32_t offset = stream->ftable_next_sample;
        uint32_t remaining = stream->ftable_total_samples - offset;
        uint32_t count =
            remaining < MAX_STREAM_SAMPLES ? remaining : MAX_STREAM_SAMPLES;

        WATCH_FTABLE_DATA_PACKET packet = {0};
        packet.header.magic = WATCH_MAGIC;
        packet.header.version = PROT_VERSION;
        packet.header.type = FTABLE_DATA;
        packet.header.sequence = stream->ftable_sequence;
        packet.header.payload_size =
            (uint32_t) offsetof(WATCH_MSG_FTABLE_DATA, samples)
            + count * (uint32_t) sizeof(float);

        packet.data.graph_id = stream->graph_id;
        packet.data.stream_id = stream->stream_id;
        packet.data.transfer_id = stream->ftable_transfer_id;
        packet.data.sample_offset = offset;
        packet.data.sample_count = count;
        packet.data.total_samples = stream->ftable_total_samples;
        memcpy(packet.data.samples, &stream->ftable_samples[offset], count * sizeof(float));

        uint32_t packet_size =
            (uint32_t) sizeof(WATCH_MSG_HEADER) + packet.header.payload_size;
        if (send_packet(manager, &packet, packet_size) != OK) {
            return;
        }
        stream->ftable_next_sample += count;
    }

    if (stream->ftable_samples != NULL && stream->ftable_next_sample == stream->ftable_total_samples) {
        csound->Free(csound, stream->ftable_samples);
        stream->ftable_samples = NULL;
    }
}

static bool stream_transfer_complete(const WATCH_STREAM *stream) {
    unsigned read =
        atomic_load_explicit(&stream->read_pos, memory_order_relaxed);
    unsigned write =
        atomic_load_explicit(&stream->write_pos, memory_order_acquire);
    return read == write
        && atomic_load_explicit(&stream->pending_time_samples, memory_order_relaxed) == 0U
        && stream->ftable_next_sample >= stream->ftable_total_samples;
}

static bool graph_has_streams(const WATCH_GRAPH *graph) {
    for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
        if (graph->streams[i] != NULL) {
            return true;
        }
    }
    return false;
}

/*
 * A stream outlives the opcode instance that feeds it only until the queued
 * packets reach the socket. Releasing the slot here keeps the graph usable by
 * later instrument instances, and keeping the stream alive until its owner has
 * deinitialized stops the graph from being freed under a running watchadd.
 */
static void release_finished_streams(CSOUND *csound, WATCH_GRAPH *graph) {
    for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
        WATCH_STREAM *stream = graph->streams[i];
        if (stream == NULL
            || !stream->release_requested
            || !stream_transfer_complete(stream)) {
            continue;
        }

        graph->streams[i] = NULL;
        if (graph->stream_count > 0U) {
            graph->stream_count--;
        }
        free_stream(csound, stream);
    }
}

static uintptr_t sender_thread_main(void *user_data) {
    WATCH_MANAGER *manager = (WATCH_MANAGER *) user_data;
    CSOUND *csound = manager->csound;
    RTCLOCK clock;
    csound->InitTimerStruct(&clock);

    while (atomic_load_explicit(&manager->running, memory_order_acquire)) {
        receive_acks(manager);

        double elapsed_seconds = csound->GetRealTime(&clock);
        uint64_t now_ms = elapsed_seconds > 0.0 ? (uint64_t) (elapsed_seconds * 1000.0) : 0U;

        bool has_unacked_graph = false;

        csound->LockMutex(manager->registry_mutex);

        for (uint32_t i = 0; i < manager->graph_capacity; i++) {
            WATCH_GRAPH *graph = manager->graphs[i];
            if (graph == NULL) {
                continue;
            }

            if (!graph->is_config_acked) {
                has_unacked_graph = true;
                if (graph->last_config_send_time == UINT64_MAX || now_ms - graph->last_config_send_time >= WATCH_CONFIG_RETRY_MS) {
                    uint32_t packet_size = (uint32_t) offsetof(WATCH_CONFIG_PACKET, config) + graph->data_config.header.payload_size;

                    graph->last_config_send_time = now_ms;
                    send_packet(manager, &graph->data_config, packet_size);
                }
                continue;
            }

            for (uint32_t stream_index = 0; stream_index < MAX_STREAMS; stream_index++) {
                WATCH_STREAM *stream = graph->streams[stream_index];
                if (stream != NULL) {
                    send_stream_packets(manager, stream);
                    send_ftable_packets(manager, stream);
                }
            }
            release_finished_streams(csound, graph);

            if (graph->destroy_requested
                && !graph_has_streams(graph)) {
                manager->graphs[i] = NULL;
                if (manager->graph_count > 0U) {
                    manager->graph_count--;
                }
                free_graph(csound, graph);
            }
        }

        csound->UnlockMutex(manager->registry_mutex);

        if (has_unacked_graph) {
            try_launch_viewer(manager, now_ms);
        } else {
            manager->viewer_probe_started_ms = UINT64_MAX;
        }

        csound->Sleep(WATCH_SENDER_SLEEP_MS);
    }

    return 0;
}

static int32_t manager_reset(CSOUND *csound, void *user_data) {
    WATCH_MANAGER *manager = (WATCH_MANAGER *) user_data;
    if (manager == NULL) {
        return OK;
    }

    atomic_store(&manager->running, false);

    if (manager->sender_thread != NULL) {
        csound->JoinThread(manager->sender_thread);
        manager->sender_thread = NULL;
    }

    if (manager->socket_fd != WATCH_INVALID_SOCKET) {
        send_session_close(manager);
        watch_socket_close(manager->socket_fd);
        manager->socket_fd = WATCH_INVALID_SOCKET;
    }
    watch_net_cleanup();

    if (manager->registry_mutex != NULL) {
        csound->LockMutex(manager->registry_mutex);
    }

    for (uint32_t i = 0; i < manager->graph_capacity; i++) {
        WATCH_GRAPH *graph = manager->graphs[i];
        manager->graphs[i] = NULL;
        free_graph(csound, graph);
    }

    manager->graph_count = 0;

    if (manager->registry_mutex != NULL) {
        csound->UnlockMutex(manager->registry_mutex);
        csound->DestroyMutex(manager->registry_mutex);
        manager->registry_mutex = NULL;
    }

    return OK;
}

static WATCH_MANAGER *get_manager(CSOUND *csound) {
    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager != NULL) {
        return manager;
    }

    int32_t result = csound->CreateGlobalVariable(csound, REGISTRY_NAME, sizeof(WATCH_MANAGER));
    if (result != 0) {
        csound->ErrorMsg(csound, "[watch] global manager allocation failed (error %d, size %zu)\n", result, sizeof(WATCH_MANAGER));
        return NULL;
    }

    manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager == NULL) {
        csound->ErrorMsg(csound, "[watch] allocated global manager not found\n");
        return NULL;
    }

    manager->csound = csound;
    manager->graph_count = 0;
    manager->graph_capacity = REGISTRY_SIZE;
    manager->next_graph_id = 1;
    manager->socket_fd = WATCH_INVALID_SOCKET;
    manager->sender_thread = NULL;
    manager->viewer_probe_started_ms = UINT64_MAX;
    manager->viewer_last_launch_ms = UINT64_MAX;
    atomic_init(&manager->running, false);

    manager->registry_mutex = csound->Create_Mutex(0);
    if (manager->registry_mutex == NULL) {
        csound->ErrorMsg(csound, "[watch] registry mutex creation failed\n");
        goto failed_global;
    }

    if (watch_net_init() != 0) {
        csound->ErrorMsg(csound, "[watch] network initialization failed\n");
        goto failed_mutex;
    }

    manager->socket_fd = create_socket_udp(csound);
    if (manager->socket_fd == WATCH_INVALID_SOCKET) {
        goto failed_network;
    }

    atomic_store_explicit(&manager->running, true, memory_order_release);
    manager->sender_thread = csound->CreateThread(sender_thread_main, manager);
    if (manager->sender_thread == NULL) {
        csound->ErrorMsg(csound, "[watch] sender thread creation failed\n");
        atomic_store_explicit(&manager->running, false, memory_order_release);
        goto failed_socket;
    }

    if (csound->RegisterResetCallback(csound, manager, manager_reset) != OK) {
        csound->ErrorMsg(csound, "[watch] reset callback registration failed\n");
        atomic_store_explicit(&manager->running, false, memory_order_release);
        csound->JoinThread(manager->sender_thread);
        manager->sender_thread = NULL;
        goto failed_socket;
    }

    return manager;

failed_socket:
    watch_socket_close(manager->socket_fd);
    manager->socket_fd = WATCH_INVALID_SOCKET;

failed_network:
    watch_net_cleanup();

failed_mutex:
    csound->DestroyMutex(manager->registry_mutex);
    manager->registry_mutex = NULL;

failed_global:
    csound->DestroyGlobalVariable(csound, REGISTRY_NAME);
    return NULL;
}

static uint32_t allocate_graph_id(WATCH_MANAGER *manager) {
    for (uint32_t attempt = 0; attempt <= manager->graph_capacity; attempt++) {
        uint32_t candidate = manager->next_graph_id;
        manager->next_graph_id++;

        if (manager->next_graph_id == INVALID_HANDLE || manager->next_graph_id > MAX_HANDLE_ID) {
            manager->next_graph_id = 1;
        }

        int32_t in_use = 0;
        for (uint32_t i = 0; i < manager->graph_capacity; i++) {
            WATCH_GRAPH *graph = manager->graphs[i];
            if (graph != NULL && graph->data_config.config.graph_id == candidate) {
                in_use = 1;
                break;
            }
        }

        if (!in_use) {
            return candidate;
        }
    }

    return INVALID_HANDLE;
}

static GRAPH_REGISTER_RESULT register_graph(CSOUND *csound, WATCH_MANAGER *manager, WATCH_GRAPH *graph) {
    GRAPH_REGISTER_RESULT result = GRAPH_REGISTERED;
    uint32_t slot = manager->graph_capacity;

    csound->LockMutex(manager->registry_mutex);

    if (manager->graph_count >= manager->graph_capacity) {
        result = GRAPH_REGISTRY_FULL;
        goto unlock;
    }

    for (uint32_t i = 0; i < manager->graph_capacity; i++) {
        if (manager->graphs[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (slot == manager->graph_capacity) {
        result = GRAPH_REGISTRY_INCONSISTENT;
        goto unlock;
    }

    graph->data_config.config.graph_id = allocate_graph_id(manager);
    if (graph->data_config.config.graph_id == INVALID_HANDLE) {
        result = GRAPH_HANDLE_UNAVAILABLE;
        goto unlock;
    }

    graph->data_config.header.magic = WATCH_MAGIC;
    graph->data_config.header.version = PROT_VERSION;
    graph->data_config.header.type = CONFIG;
    graph->data_config.header.sequence = csound->GetCurrentTimeSamples(csound);
    graph->data_config.header.payload_size = sizeof(WATCH_MSG_CONFIG);

    graph->is_config_acked = false;
    graph->last_config_send_time = UINT64_MAX;

    manager->graphs[slot] = graph;
    manager->graph_count++;

unlock:
    csound->UnlockMutex(manager->registry_mutex);
    return result;
}

static int32_t tick_count(CSOUND *csound, const MYFLT *value, const char *name, uint32_t *result) {
    if (value == NULL) {
        *result = 0;
        return OK;
    }

    if (!isfinite((double) *value) || *value < FL(0.0) || *value > (MYFLT) MAX_GRID_TICKS) {
        return csound->InitError(csound, "[watch] %s must be between 0 and %u", name, MAX_GRID_TICKS);
    }

    *result = (uint32_t) *value;
    return OK;
}

static int32_t numeric_range(
    CSOUND *csound,
    const MYFLT *min_value,
    const MYFLT *max_value,
    const char *name,
    float lower_bound,
    float upper_bound,
    WATCH_RANGE *range
) {
    range->is_min_auto = min_value == NULL;
    range->is_max_auto = max_value == NULL;
    range->min = 0.0f;
    range->max = 0.0f;

    if (min_value != NULL) {
        if (!isfinite((double) *min_value) || *min_value < (MYFLT) lower_bound || *min_value > (MYFLT) upper_bound) {
            return csound->InitError(csound, "[watch] %s minimum is outside the supported range", name);
        }
        range->min = (float) *min_value;
    }

    if (max_value != NULL) {
        if (!isfinite((double) *max_value) || *max_value < (MYFLT) lower_bound || *max_value > (MYFLT) upper_bound) {
            return csound->InitError(csound, "[watch] %s maximum is outside the supported range", name);
        }
        range->max = (float) *max_value;
    }

    if (!range->is_min_auto && !range->is_max_auto && range->min >= range->max) {
        return csound->InitError(csound, "[watch] %s minimum must be less than maximum", name);
    }

    return OK;
}

static int32_t create_graph(
    CSOUND *csound,
    MYFLT *handle,
    WATCH_SIGNAL_DOMAIN domain,
    const WATCH_MSG_GRAPH_SETTINGS *settings,
    const STRINGDAT *title,
    WATCH_GRAPH_THEME theme_id
) {
    if (handle != NULL) {
        *handle = (MYFLT) INVALID_HANDLE;
    }

    WATCH_MANAGER *manager = get_manager(csound);
    if (manager == NULL) {
        return csound->InitError(csound, "[watch] graph manager initialization failed");
    }

    WATCH_GRAPH *graph = csound->Calloc(csound, sizeof(WATCH_GRAPH));
    if (graph == NULL) {
        return csound->InitError(csound, "[watch] graph memory allocation failed");
    }

    graph->data_config.config.domain = domain;
    graph->data_config.config.theme = theme_id;
    graph->data_config.config.settings = *settings;
    int32_t title_result = string_to_title(graph->data_config.config.title, title);
    if (title_result != OK) {
        free_graph(csound, graph);
        return csound->InitError(csound, "[watch] title string too long");
    }

    graph->streams = csound->Calloc(csound, sizeof(*graph->streams) * MAX_STREAMS);
    if (graph->streams == NULL) {
        free_graph(csound, graph);
        return csound->InitError(csound, "[watch] graph streams memory allocation failed");
    }

    GRAPH_REGISTER_RESULT register_result = register_graph(csound, manager, graph);
    if (register_result != GRAPH_REGISTERED) {
        free_graph(csound, graph);

        if (register_result == GRAPH_REGISTRY_FULL) {
            csound->Message(csound, "[watch] graph registry full\n");
            return OK;
        }
        if (register_result == GRAPH_REGISTRY_INCONSISTENT) {
            return csound->InitError(csound, "[watch] graph registry is inconsistent");
        }
        return csound->InitError(csound, "[watch] graph handle allocation failed");
    }

    if (handle != NULL) {
        *handle = graph->data_config.config.graph_id;
    }

    return OK;
}

static int32_t watch_deinit(CSOUND *csound, WATCH_CREATE_TIME *p) {
    uint32_t graph_id = (uint32_t) *p->handle;
    if (graph_id == INVALID_HANDLE) {
        return OK;
    }

    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager != NULL) {
        csound->LockMutex(manager->registry_mutex);
        WATCH_GRAPH *graph = find_graph(manager, graph_id);
        if (graph != NULL) {
            /*
             * The graph is dropped by the sender thread once every stream has
             * been released by its own watchadd instance and drained.
             */
            graph->destroy_requested = true;
        }
        csound->UnlockMutex(manager->registry_mutex);
    }

    *p->handle = INVALID_HANDLE;
    return OK;
}

static int32_t create_time_helper(CSOUND *csound, WATCH_CREATE_TIME *p, WATCH_MSG_TIME_CONFIG *config) {
    uint32_t nxticks;
    uint32_t nyticks;
    int32_t result = tick_count(csound, p->nxticks, "nxticks", &nxticks);
    if (result != OK) {
        return result;
    }

    result = tick_count(csound, p->nyticks, "nyticks", &nyticks);
    if (result != OK) {
        return result;
    }

    config->win_size = (float) *p->win_size;
    config->nxticks = nxticks;
    config->nyticks = nyticks;
    result = numeric_range(csound, p->ymin, p->ymax, "y range", -FLT_MAX, FLT_MAX, &config->yrange);
    if (result != OK) {
        return result;
    }

    return OK;
}

int32_t watch_create_scope(CSOUND *csound, WATCH_CREATE_TIME *p) {
    *p->handle = (MYFLT) INVALID_HANDLE;

    if (p->win_size == NULL || !isfinite((double) *p->win_size) || *p->win_size <= FL(0.0)) {
        return csound->InitError(csound, "[watch] window size must be greater than zero");
    }

    WATCH_MSG_GRAPH_SETTINGS settings = {0};
    WATCH_MSG_TIME_CONFIG *config = &settings.time;

    int32_t result = create_time_helper(csound, p, config);
    if (result != OK) {
        return result;
    }

    return create_graph(csound, p->handle, WATCH_DOMAIN_OSCILLOSCOPE, &settings, p->title, WATCH_THEME_LIGHT);
}

static int32_t create_spectral_graph(
    CSOUND *csound,
    MYFLT *handle,
    WATCH_SIGNAL_DOMAIN domain,
    float history_seconds,
    const MYFLT *min_frequency,
    const MYFLT *max_frequency,
    const MYFLT *min_value,
    const MYFLT *max_value,
    const MYFLT *scale_value,
    const MYFLT *nxticks_value,
    const MYFLT *nyticks_value,
    const STRINGDAT *title,
    float sample_rate
) {
    WATCH_MSG_GRAPH_SETTINGS settings = {0};
    WATCH_MSG_SPECTRAL_CONFIG *config = &settings.spectral;
    config->history_seconds = history_seconds;

    config->scale = WATCH_SCALE_LINEAR_GAIN;
    if (scale_value != NULL) {
        if (!isfinite((double) *scale_value)
            || *scale_value < (MYFLT) WATCH_SCALE_LINEAR_GAIN
            || *scale_value > (MYFLT) WATCH_SCALE_DECIBEL
            || *scale_value != (MYFLT) (uint32_t) *scale_value) {
            return csound->InitError(csound, "[watch] spectral scale must be 0 (gain), 1 (power), or 2 (dB)");
        }
        config->scale = (WATCH_SPECTRAL_SCALE) *scale_value;
    }

    int32_t result = tick_count(csound, nxticks_value, "nxticks", &config->nxticks);
    if (result != OK) {
        return result;
    }
    result = tick_count(csound, nyticks_value, "nyticks", &config->nyticks);
    if (result != OK) {
        return result;
    }

    float nyquist = sample_rate * 0.5f;
    result = numeric_range(csound, min_frequency, max_frequency, "frequency range", 0.0f, nyquist, &config->frequency_range);
    if (result != OK) {
        return result;
    }
    float value_lower_bound = config->scale == WATCH_SCALE_DECIBEL ? -FLT_MAX : 0.0f;
    result = numeric_range(csound, min_value, max_value, "spectral value range", value_lower_bound, FLT_MAX, &config->value_range);
    if (result != OK) {
        return result;
    }

    return create_graph(csound, handle, domain, &settings, title, WATCH_THEME_LIGHT);
}

int32_t watch_create_spectrum(CSOUND *csound, WATCH_CREATE_SPECTRAL *p) {
    return create_spectral_graph(
        csound,
        p->handle,
        WATCH_DOMAIN_SPECTRUM,
        0.0f,
        p->min_frequency,
        p->max_frequency,
        p->min_value,
        p->max_value,
        p->scale,
        p->nxticks,
        p->nyticks,
        p->title,
        (float) p->h.insdshead->esr
    );
}

int32_t watch_create_spectrogram(CSOUND *csound, WATCH_CREATE_SPECTROGRAM *p) {
    *p->handle = (MYFLT) INVALID_HANDLE;
    if (p->history_seconds == NULL
        || !isfinite((double) *p->history_seconds)
        || *p->history_seconds <= FL(0.0)
        || *p->history_seconds > (MYFLT) FLT_MAX) {
        return csound->InitError(csound, "[watch] spectrogram history must be greater than zero seconds");
    }

    return create_spectral_graph(
        csound,
        p->handle,
        WATCH_DOMAIN_SPECTROGRAM,
        (float) *p->history_seconds,
        p->min_frequency,
        p->max_frequency,
        p->min_value,
        p->max_value,
        p->scale,
        p->nxticks,
        p->nyticks,
        p->title,
        (float) p->h.insdshead->esr
    );
}

static int32_t create_stream(
    CSOUND *csound,
    const MYFLT *handle,
    WATCH_SIGNAL_DOMAIN first_domain,
    WATCH_SIGNAL_DOMAIN second_domain,
    float sample_rate,
    WATCH_STREAM **result
) {
    *result = NULL;
    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager == NULL) {
        return csound->InitError(csound, "[watch] graph manager not found");
    }

    WATCH_STREAM *s = csound->Calloc(csound, sizeof(WATCH_STREAM));
    if (s == NULL) {
        return csound->InitError(csound, "[watch] stream allocation failed");
    }

    csound->LockMutex(manager->registry_mutex);

    WATCH_GRAPH *graph = find_graph(manager, (uint32_t) *handle);

    if (graph == NULL || graph->destroy_requested) {
        csound->UnlockMutex(manager->registry_mutex);
        csound->Free(csound, s);
        return csound->InitError(csound, "[watch] invalid graph handle");
    }

    WATCH_SIGNAL_DOMAIN domain = graph->data_config.config.domain;
    if (domain != first_domain && domain != second_domain) {
        csound->UnlockMutex(manager->registry_mutex);
        csound->Free(csound, s);
        return csound->InitError(csound, "[watch] signal type does not match graph domain");
    }

    // slots left behind by finished instrument instances are reused
    uint32_t stream_slot = MAX_STREAMS;
    for (uint32_t i = 0U; i < MAX_STREAMS; i++) {
        if (graph->streams[i] == NULL) {
            stream_slot = i;
            break;
        }
    }

    if (stream_slot == MAX_STREAMS) {
        csound->UnlockMutex(manager->registry_mutex);
        csound->Free(csound, s);
        return csound->InitError(csound, "[watch] graph stream registry full");
    }

    s->graph_id = graph->data_config.config.graph_id;
    s->stream_id = stream_slot;
    s->sample_rate = sample_rate;

    atomic_init(&s->pending_time_samples, 0);
    atomic_init(&s->write_pos, 0);
    atomic_init(&s->read_pos, 0);
    atomic_init(&s->dropped_samples, 0);

    graph->streams[stream_slot] = s;
    graph->stream_count++;
    csound->UnlockMutex(manager->registry_mutex);

    *result = s;
    return OK;
}

int32_t watch_add_a_init(CSOUND *csound, WATCH_ADD_TIME *p) {
    return create_stream(
        csound,
        p->handle,
        WATCH_DOMAIN_OSCILLOSCOPE,
        WATCH_DOMAIN_OSCILLOSCOPE,
        (float) p->h.insdshead->esr,
        &p->stream
    );
}

static uint32_t pending_time_samples(const WATCH_STREAM *stream) {
    return atomic_load_explicit(&stream->pending_time_samples, memory_order_relaxed);
}

static void publish_time_packet(WATCH_STREAM *stream, unsigned next_write) {
    unsigned write_pos = atomic_load_explicit(&stream->write_pos, memory_order_relaxed);
    uint32_t pending = pending_time_samples(stream);
    WATCH_DATA_PACKET *packet = &stream->slots[write_pos].time;
    packet->data.sample_count = pending;
    packet->header.payload_size = (uint32_t) offsetof(WATCH_MSG_DATA, samples) + pending * (uint32_t) sizeof(float);
    atomic_store_explicit(&stream->pending_time_samples, 0U, memory_order_relaxed);
    atomic_store_explicit(&stream->write_pos, next_write, memory_order_release);
}

/*
 * Called from the opcode deinitializer: the sender thread keeps the stream
 * alive until the residual packet and everything already queued has been sent.
 */
static void release_stream(CSOUND *csound, WATCH_STREAM **stream_ref) {
    WATCH_STREAM *stream = *stream_ref;
    if (stream == NULL) {
        return;
    }
    *stream_ref = NULL;

    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager == NULL) {
        return;
    }

    csound->LockMutex(manager->registry_mutex);
    if (pending_time_samples(stream) > 0U) {
        unsigned write = atomic_load_explicit(&stream->write_pos, memory_order_relaxed);
        publish_time_packet(stream, (write + 1U) % MAX_QUEUE_SIZE);
    }
    stream->release_requested = true;
    csound->UnlockMutex(manager->registry_mutex);
}

static int32_t watch_add_deinit(CSOUND *csound, WATCH_ADD_TIME *p) {
    release_stream(csound, &p->stream);
    return OK;
}

static int32_t watch_add_f_deinit(CSOUND *csound, WATCH_ADD_SPECTRAL *p) {
    release_stream(csound, &p->stream);
    return OK;
}

int32_t watch_add_a(CSOUND *csound, WATCH_ADD_TIME *p) {
    WATCH_STREAM *stream = p->stream;
    uint32_t offset = p->h.insdshead->ksmps_offset;
    uint32_t early = p->h.insdshead->ksmps_no_end;
    uint32_t end = CS_KSMPS - early;
    uint32_t nsamples = end - offset;

    if (nsamples == 0) {
        return OK;
    }

    uint32_t source_pos = offset;
    int64_t bstart = csound->GetCurrentTimeSamples(csound);
    while (source_pos < end) {
        unsigned write = atomic_load_explicit(&stream->write_pos, memory_order_relaxed);
        unsigned next_write = (write + 1) % MAX_QUEUE_SIZE;
        WATCH_DATA_PACKET *packet = &stream->slots[write].time;
        int64_t sequence = bstart + (int64_t) source_pos;
        uint32_t pending = pending_time_samples(stream);

        if (pending > 0U
            && sequence != stream->pending_time_sequence + (int64_t) pending) {
            publish_time_packet(stream, next_write);
            continue;
        }

        if (pending == 0U) {
            unsigned read = atomic_load_explicit(&stream->read_pos, memory_order_acquire);
            if (next_write == read) {
                atomic_fetch_add_explicit(&stream->dropped_samples, end - source_pos, memory_order_relaxed);
                break;
            }

            packet->header.magic = WATCH_MAGIC;
            packet->header.type = DATA;
            packet->header.version = PROT_VERSION;
            packet->header.sequence = sequence;

            packet->data.graph_id = stream->graph_id;
            packet->data.stream_id = stream->stream_id;
            packet->data.sample_rate = stream->sample_rate;
            stream->pending_time_sequence = sequence;
        }

        uint32_t remaining = end - source_pos;
        uint32_t available = MAX_STREAM_SAMPLES - pending;
        uint32_t chunk = remaining < available ? remaining : available;

        for (uint32_t i = 0; i < chunk; i++) {
            float sample = (float) p->signal[i + source_pos];
            packet->data.samples[pending + i] = sample;
        }

        pending += chunk;
        source_pos += chunk;
        atomic_store_explicit(&stream->pending_time_samples, pending, memory_order_relaxed);

        if (pending == MAX_STREAM_SAMPLES) {
            publish_time_packet(stream, next_write);
        }
    }

    return OK;
}

static int32_t watch_add_f_init(CSOUND *csound, WATCH_ADD_SPECTRAL *p) {
    p->stream = NULL;
    p->last_frame_id = 0U;

    if (p->signal == NULL) {
        return csound->InitError(csound, "[watch] spectral signal is null");
    }
    if (p->signal->sliding) {
        return csound->InitError(csound, "[watch] sliding spectral signals are not supported yet");
    }
    if (p->signal->N <= 0 || (p->signal->N & 1) != 0 || p->signal->overlap <= 0) {
        return csound->InitError(csound, "[watch] invalid spectral FFT size or overlap");
    }
    if (p->signal->format != PVS_AMP_FREQ && p->signal->format != PVS_AMP_PHASE && p->signal->format != PVS_COMPLEX) {
        return csound->InitError(csound, "[watch] unsupported spectral format");
    }

    p->fft_size = (uint32_t) p->signal->N;
    p->hop_size = (uint32_t) p->signal->overlap;
    p->pvs_format = p->signal->format;
    p->last_frame_id = p->signal->framecount;

    return create_stream(
        csound,
        p->handle,
        WATCH_DOMAIN_SPECTRUM,
        WATCH_DOMAIN_SPECTROGRAM,
        (float) p->h.insdshead->esr,
        &p->stream
    );
}

static float spectral_bin_power(const float *frame, uint32_t bin, int32_t format) {
    double first = frame[bin * 2U];
    double power;
    if (format == PVS_COMPLEX) {
        double second = frame[bin * 2U + 1U];
        power = first * first + second * second;
    } else {
        power = first * first;
    }

    if (isnan(power) || power < 0.0) {
        return 0.0f;
    }
    if (power > (double) FLT_MAX) {
        return FLT_MAX;
    }
    return (float) power;
}

int32_t watch_add_f(CSOUND *csound, WATCH_ADD_SPECTRAL *p) {
    uint32_t frame_id = p->signal->framecount;
    if (frame_id == p->last_frame_id) {
        return OK;
    }

    const float *frame = (const float *) p->signal->frame.auxp;
    if (frame == NULL) {
        return OK;
    }

    uint32_t total_bins = p->fft_size / 2U + 1U;
    uint32_t source_bin = 0U;
    int64_t sequence = csound->GetCurrentTimeSamples(csound);

    while (source_bin < total_bins) {
        uint32_t remaining = total_bins - source_bin;
        uint32_t chunk = remaining > MAX_SPECTRAL_BINS ? MAX_SPECTRAL_BINS : remaining;

        unsigned write = atomic_load_explicit(&p->stream->write_pos, memory_order_relaxed);
        unsigned next_write = (write + 1U) % MAX_QUEUE_SIZE;
        unsigned read = atomic_load_explicit(&p->stream->read_pos, memory_order_acquire);
        if (next_write == read) {
            atomic_fetch_add_explicit(&p->stream->dropped_samples, remaining, memory_order_relaxed);
            break;
        }

        WATCH_SPECTRAL_DATA_PACKET *packet = &p->stream->slots[write].spectral;
        packet->header.magic = WATCH_MAGIC;
        packet->header.version = PROT_VERSION;
        packet->header.type = SPECTRAL_DATA;
        packet->header.sequence = sequence;
        packet->header.payload_size = (uint32_t) offsetof(WATCH_MSG_SPECTRAL_DATA, bins) + chunk * (uint32_t) sizeof(float);

        packet->data.graph_id = p->stream->graph_id;
        packet->data.stream_id = p->stream->stream_id;
        packet->data.frame_id = frame_id;
        packet->data.fft_size = p->fft_size;
        packet->data.hop_size = p->hop_size;
        packet->data.sample_rate = p->stream->sample_rate;
        packet->data.format = WATCH_SPECTRAL_POWER;
        packet->data.bin_offset = source_bin;
        packet->data.bin_count = chunk;
        packet->data.total_bins = total_bins;

        for (uint32_t i = 0; i < chunk; i++) {
            packet->data.bins[i] = spectral_bin_power(frame, source_bin + i, p->pvs_format);
        }

        atomic_store_explicit(&p->stream->write_pos, next_write, memory_order_release);
        source_bin += chunk;
    }

    p->last_frame_id = frame_id;
    return OK;
}


static int32_t watch_ftable_deinit(CSOUND *csound, WATCH_FTABLE *p) {
    uint32_t graph_id = (uint32_t) p->graph_id;
    if (graph_id == INVALID_HANDLE) {
        return OK;
    }

    release_stream(csound, &p->stream);

    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager != NULL) {
        csound->LockMutex(manager->registry_mutex);
        WATCH_GRAPH *graph = find_graph(manager, graph_id);
        if (graph != NULL) {
            graph->destroy_requested = true;
        }
        csound->UnlockMutex(manager->registry_mutex);
    }

    p->graph_id = INVALID_HANDLE;
    return OK;
}

static int32_t watch_ftable_title_deinit(CSOUND *csound, WATCH_FTABLE_TITLE *p) {
    WATCH_FTABLE adapted = { .h = p->h, .stream = p->stream, .graph_id = p->graph_id };
    int32_t result = watch_ftable_deinit(csound, &adapted);
    p->stream = adapted.stream;
    p->graph_id = adapted.graph_id;
    return result;
}

int32_t watch_ftable(CSOUND *csound, WATCH_FTABLE *p) {
    p->stream = NULL;
    p->graph_id = INVALID_HANDLE;
    MYFLT graph_handle = (MYFLT) INVALID_HANDLE;
    FUNC *f = csound->FTFind(csound, p->ftable);
    if (f == NULL || f->ftable == NULL || f->flen <= 0) {
        return csound->InitError(csound, "[watch] Table not found");
    }

    WATCH_MSG_GRAPH_SETTINGS settings = {0};
    WATCH_MSG_FTABLE_CONFIG *config = &settings.ftable;
    int32_t result = numeric_range(csound, p->ymin, p->ymax, "y range", -FLT_MAX, FLT_MAX, &config->yrange);
    if (result != OK) {
        return result;
    }
    if ((uint64_t) f->flen > MAX_FTABLE_SAMPLES) {
        return csound->InitError(csound, "[watch] ftable exceeds the supported maximum of %u samples", MAX_FTABLE_SAMPLES);
    }

    config->win_size = (uint32_t) f->flen;

    if (p->theme != NULL
        && (!isfinite((double) *p->theme)
            || (*p->theme != (MYFLT) WATCH_THEME_LIGHT
                && *p->theme != (MYFLT) WATCH_THEME_DARK))) {
        return csound->InitError(csound, "[watch] theme must be 0 (light) or 1 (dark)");
    }

    size_t snapshot_size = (size_t) f->flen * sizeof(float);
    float *snapshot = csound->Malloc(csound, snapshot_size);
    if (snapshot == NULL) {
        return csound->InitError(csound, "[watch] ftable snapshot allocation failed");
    }

    for (uint32_t i = 0U; i < (uint32_t) f->flen; i++) {
        snapshot[i] = (float) f->ftable[i];
    }

    WATCH_GRAPH_THEME theme_id = p->theme == NULL ? WATCH_THEME_LIGHT : (WATCH_GRAPH_THEME) *p->theme;

    result = create_graph(csound, &graph_handle, WATCH_DOMAIN_FTABLE, &settings, p->title, theme_id);

    if (result != OK || graph_handle == (MYFLT) INVALID_HANDLE) {
        csound->Free(csound, snapshot);
        return result;
    }

    p->graph_id = (uint32_t) graph_handle;
    result = create_stream(
        csound,
        &graph_handle,
        WATCH_DOMAIN_FTABLE,
        WATCH_DOMAIN_FTABLE,
        (float) CS_ESR,
        &p->stream
    );

    if (result != OK) {
        csound->Free(csound, snapshot);
        watch_ftable_deinit(csound, p);
        return result;
    }

    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager == NULL) {
        csound->Free(csound, snapshot);
        watch_ftable_deinit(csound, p);
        return csound->InitError(csound, "[watch] graph manager not found");
    }

    csound->LockMutex(manager->registry_mutex);
    p->stream->ftable_samples = snapshot;
    p->stream->ftable_total_samples = (uint32_t) f->flen;
    p->stream->ftable_next_sample = 0U;
    p->stream->ftable_transfer_id = 1U;
    p->stream->ftable_sequence = csound->GetCurrentTimeSamples(csound);
    csound->UnlockMutex(manager->registry_mutex);
    return OK;
}

static int32_t watch_ftable_title(CSOUND *csound, WATCH_FTABLE_TITLE *p) {
    // h carries insdshead: the CS_ESR macro used by watch_ftable reads it
    WATCH_FTABLE adapted = {
        .h = p->h,
        .ftable = p->ftable,
        .ymin = p->ymin,
        .ymax = p->ymax,
        .theme = NULL,
        .title = p->title
    };
    int32_t result = watch_ftable(csound, &adapted);
    p->stream = adapted.stream;
    p->graph_id = adapted.graph_id;
    return result;
}

int32_t watch_theme(CSOUND *csound, WATCH_THEME *p) {
    WATCH_MANAGER *manager = csound->QueryGlobalVariable(csound, REGISTRY_NAME);
    if (manager == NULL) {
        return csound->InitError(csound, "[watch] manager not found");
    }

    if (p->handle == NULL
        || !isfinite((double) *p->handle)
        || *p->handle < FL(1.0)
        || *p->handle > (MYFLT) MAX_HANDLE_ID
        || *p->handle != (MYFLT) (uint32_t) *p->handle) {
        return csound->InitError(csound, "[watch] invalid graph handle");
    }
    if (p->theme == NULL
        || !isfinite((double) *p->theme)
        || (*p->theme != (MYFLT) WATCH_THEME_LIGHT && *p->theme != (MYFLT) WATCH_THEME_DARK)) {
        return csound->InitError(csound, "[watch] theme must be 0 (light) or 1 (dark)");
    }

    uint32_t handle = (uint32_t) *p->handle;
    WATCH_GRAPH_THEME theme = (WATCH_GRAPH_THEME) *p->theme;
    csound->LockMutex(manager->registry_mutex);
    WATCH_GRAPH *graph = find_graph(manager, handle);
    if (graph == NULL) {
        csound->UnlockMutex(manager->registry_mutex);
        return csound->InitError(csound, "[watch] invalid graph handle");
    }

    if (graph->data_config.config.theme == theme) {
        csound->UnlockMutex(manager->registry_mutex);
        return OK;
    }

    graph->data_config.config.theme = theme;
    graph->data_config.header.sequence = graph->data_config.header.sequence == INT64_MAX ? 0 : graph->data_config.header.sequence + 1;
    graph->is_config_acked = false;
    graph->last_config_send_time = UINT64_MAX;
    csound->UnlockMutex(manager->registry_mutex);
    return OK;
}

int32_t watch_create_control(CSOUND *csound, WATCH_CREATE_TIME *p) {
    *p->handle = (MYFLT) INVALID_HANDLE;

    if (p->win_size == NULL || !isfinite((double) *p->win_size) || *p->win_size <= FL(0.0)) {
        return csound->InitError(csound, "[watch] window size must be greater than zero");
    }

    WATCH_MSG_GRAPH_SETTINGS settings = {0};
    WATCH_MSG_TIME_CONFIG *config = &settings.time;

    int result = create_time_helper(csound, p, config);
    if (result != OK) {
        return result;
    }

    return create_graph(csound, p->handle, WATCH_DOMAIN_CONTROL, &settings, p->title, WATCH_THEME_LIGHT);
}

int32_t watch_add_k_init(CSOUND *csound, WATCH_ADD_TIME *p) {
    return create_stream(
        csound,
        p->handle,
        WATCH_DOMAIN_CONTROL,
        WATCH_DOMAIN_CONTROL,
        (float) p->h.insdshead->ekr,
        &p->stream
    );
}

int32_t watch_add_k(CSOUND *csound, WATCH_ADD_TIME *p) {
    (void) csound;
    WATCH_STREAM *stream = p->stream;
    int64_t sequence = (int64_t) p->h.insdshead->kcounter;

    for (;;) {
        unsigned write = atomic_load_explicit(&stream->write_pos, memory_order_relaxed);
        unsigned next_write = (write + 1U) % MAX_QUEUE_SIZE;

        WATCH_DATA_PACKET *packet = &stream->slots[write].time;
        uint32_t pending = pending_time_samples(stream);

        int64_t off_pend = stream->pending_time_sequence + (int64_t) pending;
        if (pending > 0U && sequence != off_pend) {
            publish_time_packet(stream, next_write);
            continue;
        }

        if (pending == 0U) {
            unsigned read = atomic_load_explicit(&stream->read_pos, memory_order_acquire);
            if (next_write == read) {
                atomic_fetch_add_explicit(&stream->dropped_samples, 1, memory_order_relaxed);
                return OK;
            }

            packet->header.magic = WATCH_MAGIC;
            packet->header.type = DATA;
            packet->header.version = PROT_VERSION;
            packet->header.sequence = sequence;

            packet->data.graph_id = stream->graph_id;
            packet->data.stream_id = stream->stream_id;
            packet->data.sample_rate = stream->sample_rate;
            stream->pending_time_sequence = sequence;
        }

        packet->data.samples[pending] = isfinite(*p->signal) ? (float) *p->signal : 0.0f;
        pending++;
        atomic_store_explicit(&stream->pending_time_samples, pending, memory_order_relaxed);

        if (pending == MAX_STREAM_SAMPLES) {
            publish_time_packet(stream, next_write);
        }

        return OK;
    }
}


#define S(s) sizeof(s)

static OENTRY localops[] = {
    { "watchtable",           S(WATCH_FTABLE),             0, "",  "i",           (SUBR) watch_ftable,             NULL,                  (SUBR) watch_ftable_deinit,       NULL, 0 },
    { "watchtable.m",         S(WATCH_FTABLE),             0, "",  "ii",          (SUBR) watch_ftable,             NULL,                  (SUBR) watch_ftable_deinit,       NULL, 0 },
    { "watchtable.mm",        S(WATCH_FTABLE),             0, "",  "iii",         (SUBR) watch_ftable,             NULL,                  (SUBR) watch_ftable_deinit,       NULL, 0 },
    { "watchtable.s",         S(WATCH_FTABLE_TITLE),       0, "",  "iiiS",        (SUBR) watch_ftable_title,       NULL,                  (SUBR) watch_ftable_title_deinit, NULL, 0 },
    { "watchtable.t",         S(WATCH_FTABLE),             0, "",  "iiii",        (SUBR) watch_ftable,             NULL,                  (SUBR) watch_ftable_deinit,       NULL, 0 },
    { "watchtable.ts",        S(WATCH_FTABLE),             0, "",  "iiiiS",       (SUBR) watch_ftable,             NULL,                  (SUBR) watch_ftable_deinit,       NULL, 0 },

    { "watchscope",           S(WATCH_CREATE_TIME),        0, "i", "ioo",         (SUBR) watch_create_scope,       NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchscope.m",         S(WATCH_CREATE_TIME),        0, "i", "iiii",        (SUBR) watch_create_scope,       NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchscope.mm",        S(WATCH_CREATE_TIME),        0, "i", "iiiii",       (SUBR) watch_create_scope,       NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchscope.s",         S(WATCH_CREATE_TIME),        0, "i", "iiiiiS",      (SUBR) watch_create_scope,       NULL,                  (SUBR) watch_deinit,              NULL, 0 },

    { "watchcontrol",         S(WATCH_CREATE_TIME),        0, "i", "ioo",         (SUBR) watch_create_control,     NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchcontrol.m",       S(WATCH_CREATE_TIME),        0, "i", "iiii",        (SUBR) watch_create_control,     NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchcontrol.mm",      S(WATCH_CREATE_TIME),        0, "i", "iiiii",       (SUBR) watch_create_control,     NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchcontrol.s",       S(WATCH_CREATE_TIME),        0, "i", "iiiiiS",      (SUBR) watch_create_control,     NULL,                  (SUBR) watch_deinit,              NULL, 0 },

    { "watchspectrum",        S(WATCH_CREATE_SPECTRAL),    0, "i", "",            (SUBR) watch_create_spectrum,    NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchspectrum.r",      S(WATCH_CREATE_SPECTRAL),    0, "i", "iiii",        (SUBR) watch_create_spectrum,    NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchspectrum.t",      S(WATCH_CREATE_SPECTRAL),    0, "i", "iiiiioo",     (SUBR) watch_create_spectrum,    NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchspectrum.s",      S(WATCH_CREATE_SPECTRAL),    0, "i", "iiiiiiiS",    (SUBR) watch_create_spectrum,    NULL,                  (SUBR) watch_deinit,              NULL, 0 },

    { "watchspectrogram",     S(WATCH_CREATE_SPECTROGRAM), 0, "i", "i",           (SUBR) watch_create_spectrogram, NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchspectrogram.r",   S(WATCH_CREATE_SPECTROGRAM), 0, "i", "iiiii",       (SUBR) watch_create_spectrogram, NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchspectrogram.t",   S(WATCH_CREATE_SPECTROGRAM), 0, "i", "iiiiiioo",    (SUBR) watch_create_spectrogram, NULL,                  (SUBR) watch_deinit,              NULL, 0 },
    { "watchspectrogram.s",   S(WATCH_CREATE_SPECTROGRAM), 0, "i", "iiiiiiiiS",   (SUBR) watch_create_spectrogram, NULL,                  (SUBR) watch_deinit,              NULL, 0 },

    { "watchadd.a",           S(WATCH_ADD_TIME),           0, "",  "ia",          (SUBR) watch_add_a_init,         (SUBR) watch_add_a,    (SUBR) watch_add_deinit,          NULL, 0 },
    { "watchadd.k",           S(WATCH_ADD_TIME),           0, "",  "ik",          (SUBR) watch_add_k_init,         (SUBR) watch_add_k,    (SUBR) watch_add_deinit,          NULL, 0 },
    { "watchadd.f",           S(WATCH_ADD_SPECTRAL),       0, "",  "if",          (SUBR) watch_add_f_init,         (SUBR) watch_add_f,    (SUBR) watch_add_f_deinit,        NULL, 0 },

    { "watchtheme",           S(WATCH_THEME),              0, "",  "ii",          (SUBR) watch_theme,              NULL,                  NULL,                             NULL, 0 }
};

LINKAGE
