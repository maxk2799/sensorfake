#include "sensorfake_name.h"
#include "sensorfake_profile.h"
#include "sensorfake_sim.h"

#include <furi.h>
#include <furi_hal.h>
#include <bt/bt_service/bt.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <input/input.h>
#include <storage/storage.h>

#define TAG "SensorFake"

#define SENSORFAKE_BT_KEYS_STORAGE_NAME ".sensorfake.keys"
/* Text file on the SD card holding the Bluetooth name */
#define SENSORFAKE_NAME_FILE            "name.txt"
#define SENSORFAKE_NAME_FILE_MAX_SIZE   64
#define SENSORFAKE_UPDATE_INTERVAL_MS   1000

typedef enum {
    SensorFakeEventTypeTick,
    SensorFakeEventTypeInput,
} SensorFakeEventType;

typedef struct {
    SensorFakeEventType type;
    InputEvent input;
} SensorFakeEvent;

typedef struct {
    Bt* bt;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriTimer* timer;
    FuriMutex* mutex;
    FuriHalBleProfileBase* profile;
    SensorFakeProfileParams profile_params;
    char device_name[SENSORFAKE_NAME_MAX_LENGTH + 1];
    uint32_t connection_count;

    /* Guarded by mutex: shared between the app thread and the GUI thread */
    SensorFakeSim sim;
    bool connected;
} SensorFakeApp;

uint32_t sensorfake_sim_rand(void) {
    return furi_hal_random_get();
}

static const char* sensorfake_phase_name(const SensorFakeSim* sim) {
    switch(sensorfake_sim_get_phase(sim)) {
    case SensorFakePhaseWarmup:
        return "Warm-up";
    case SensorFakePhaseBuild:
        return "Build";
    case SensorFakePhaseEffort:
        return sim->walk_mode ? "Steady" : "Tempo";
    default:
        return "Cool-down";
    }
}

static void sensorfake_draw_callback(Canvas* canvas, void* context) {
    SensorFakeApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    const SensorFakeSim sim = app->sim;
    const bool connected = app->connected;
    furi_mutex_release(app->mutex);

    char text[32];
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "SensorFake");
    canvas_set_font(canvas, FontSecondary);
    const char* status = "No Bluetooth";
    if(app->profile) status = connected ? "Connected" : "Advertising";
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, status);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    /* Heart rate */
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(text, sizeof(text), "%u", sim.broadcast_hr);
    canvas_draw_str(canvas, 2, 32, text);
    uint16_t hr_width = canvas_string_width(canvas, text);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2 + hr_width + 3, 32, "bpm");

    /* Mode and phase */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas, 126, 25, AlignRight, AlignBottom, sim.walk_mode ? "WALK" : "RUN");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 35, AlignRight, AlignBottom, sensorfake_phase_name(&sim));

    if(connected) {
        /* Pace in min/km */
        if(sim.speed_mps < 0.2f) {
            snprintf(text, sizeof(text), "Pace --");
        } else {
            uint32_t pace_sec = (uint32_t)(1000.0f / sim.speed_mps + 0.5f);
            snprintf(text, sizeof(text), "Pace %lu:%02lu/km", pace_sec / 60, pace_sec % 60);
        }
        canvas_draw_str(canvas, 2, 45, text);

        snprintf(
            text,
            sizeof(text),
            "%ld:%02ld",
            (long)(sim.session_seconds / 60),
            (long)(sim.session_seconds % 60));
        canvas_draw_str_aligned(canvas, 126, 45, AlignRight, AlignBottom, text);

        /* Distance is tracked in 1/10 m */
        uint32_t meters = sim.distance_tenths / 10;
        snprintf(text, sizeof(text), "%lu.%02lu km", meters / 1000, (meters % 1000) / 10);
        canvas_draw_str(canvas, 2, 61, text);
    } else if(app->profile) {
        snprintf(text, sizeof(text), "Name: %s", app->device_name);
        canvas_draw_str(canvas, 2, 45, text);
    } else {
        canvas_draw_str(canvas, 2, 45, "Bluetooth did not start");
    }

    elements_button_center(canvas, sim.walk_mode ? "Run" : "Walk");
}

static void sensorfake_input_callback(InputEvent* input_event, void* context) {
    SensorFakeApp* app = context;
    SensorFakeEvent event = {.type = SensorFakeEventTypeInput, .input = *input_event};
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
}

static void sensorfake_timer_callback(void* context) {
    SensorFakeApp* app = context;
    SensorFakeEvent event = {.type = SensorFakeEventTypeTick};
    furi_message_queue_put(app->queue, &event, 0);
}

/* Start a new session in the given mode */
static void sensorfake_restart_session(SensorFakeApp* app, bool walk_mode) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(walk_mode) {
        sensorfake_sim_start_walk(&app->sim);
    } else {
        sensorfake_sim_start_run(&app->sim);
    }
    const SensorFakeSim sim = app->sim;
    furi_mutex_release(app->mutex);

    if(walk_mode) {
        FURI_LOG_I(
            TAG,
            "Walk session | hr=%d duration=%ldm",
            (int)sim.walk_center,
            (long)(sim.effort_end / 60));
    } else {
        FURI_LOG_I(
            TAG,
            "Run session | start=%d tempo=%d-%d max=%d",
            (int)sim.start_hr,
            (int)sim.tempo_start,
            (int)sim.tempo_end,
            (int)sim.session_max);
    }
}

/* Once a second: advance the session and notify the connected app */
static void sensorfake_tick(SensorFakeApp* app) {
    if(!app->profile) return;

    const bool connected = sensorfake_profile_is_connected(app->profile);
    const uint32_t connection_count = sensorfake_profile_get_connection_count(app->profile);
    if(connected && connection_count != app->connection_count) {
        /* Every new connection gets a fresh session */
        FURI_LOG_I(TAG, "App connected");
        sensorfake_restart_session(app, app->sim.walk_mode);
    } else if(!connected && app->connected) {
        FURI_LOG_I(TAG, "App disconnected, advertising again");
    }
    app->connection_count = connection_count;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->connected = connected;
    furi_mutex_release(app->mutex);

    /* The session only runs while an app is listening */
    if(!connected) return;

    uint8_t hrm_packet[SENSORFAKE_HRM_PACKET_SIZE];
    uint8_t rsc_packet[SENSORFAKE_RSC_PACKET_SIZE];

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    sensorfake_sim_step(&app->sim);
    sensorfake_sim_get_hrm_packet(&app->sim, hrm_packet);
    sensorfake_sim_get_rsc_packet(&app->sim, rsc_packet);
    furi_mutex_release(app->mutex);

    sensorfake_profile_update_hrm(app->profile, hrm_packet);
    sensorfake_profile_update_rsc(app->profile, rsc_packet);
}

/* Read the Bluetooth name from the name file. The first time around there is
 * no file yet: create it with the default name, so it is easy to find and edit */
static void sensorfake_load_name(SensorFakeApp* app) {
    char text[SENSORFAKE_NAME_FILE_MAX_SIZE];
    size_t text_len = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(
           file, APP_DATA_PATH(SENSORFAKE_NAME_FILE), FSAM_READ, FSOM_OPEN_EXISTING)) {
        text_len = storage_file_read(file, text, sizeof(text));
    } else {
        storage_file_close(file);
        if(storage_file_open(
               file, APP_DATA_PATH(SENSORFAKE_NAME_FILE), FSAM_WRITE, FSOM_CREATE_NEW)) {
            storage_file_write(
                file, SENSORFAKE_NAME_DEFAULT "\n", strlen(SENSORFAKE_NAME_DEFAULT) + 1);
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    sensorfake_name_parse(text, text_len, app->device_name);
}

static void sensorfake_bt_start(SensorFakeApp* app) {
    app->bt = furi_record_open(RECORD_BT);

    bt_disconnect(app->bt);
    // Wait 2nd core to update nvm storage
    furi_delay_ms(200);

    /* Keep our pairings apart from the ones of the Flipper mobile app */
    bt_keys_storage_set_storage_path(app->bt, APP_DATA_PATH(SENSORFAKE_BT_KEYS_STORAGE_NAME));

    app->profile_params.device_name = app->device_name;
    app->profile = bt_profile_start(app->bt, sensorfake_profile, &app->profile_params);
    if(app->profile) {
        furi_hal_bt_start_advertising();
        FURI_LOG_I(TAG, "Broadcasting as %s", app->device_name);
    } else {
        FURI_LOG_E(TAG, "Failed to start BLE profile");
    }
}

static void sensorfake_bt_stop(SensorFakeApp* app) {
    bt_disconnect(app->bt);
    // Wait 2nd core to update nvm storage
    furi_delay_ms(200);

    /* Give the Flipper its normal Bluetooth back */
    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));

    furi_record_close(RECORD_BT);
}

int32_t sensorfake_app(void* p) {
    UNUSED(p);

    SensorFakeApp* app = malloc(sizeof(SensorFakeApp));
    app->queue = furi_message_queue_alloc(8, sizeof(SensorFakeEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    sensorfake_load_name(app);
    sensorfake_sim_init(&app->sim);
    sensorfake_restart_session(app, true);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, sensorfake_draw_callback, app);
    view_port_input_callback_set(app->view_port, sensorfake_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    sensorfake_bt_start(app);

    app->timer = furi_timer_alloc(sensorfake_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_ms_to_ticks(SENSORFAKE_UPDATE_INTERVAL_MS));

    SensorFakeEvent event;
    bool running = true;
    while(running) {
        furi_check(furi_message_queue_get(app->queue, &event, FuriWaitForever) == FuriStatusOk);

        if(event.type == SensorFakeEventTypeInput) {
            if(event.input.key == InputKeyBack) {
                if(event.input.type == InputTypeShort || event.input.type == InputTypeLong) {
                    running = false;
                }
            } else if(event.input.key == InputKeyOk && event.input.type == InputTypeShort) {
                /* Switch between walk and run, starting a new session */
                sensorfake_restart_session(app, !app->sim.walk_mode);
            }
        } else if(event.type == SensorFakeEventTypeTick) {
            sensorfake_tick(app);
        }

        view_port_update(app->view_port);
    }

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    /* Detach from the GUI first, so no input piles up while Bluetooth restarts */
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    sensorfake_bt_stop(app);

    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->queue);
    free(app);

    return 0;
}
