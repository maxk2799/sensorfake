#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Heart-rate / pace session simulator. Straight port of the logic in
 * sensorfake.ino, with no hardware dependencies so it can be built on a PC. */

#define SENSORFAKE_HRM_PACKET_SIZE 2
#define SENSORFAKE_RSC_PACKET_SIZE 8

typedef enum {
    SensorFakePhaseWarmup,
    SensorFakePhaseBuild,
    SensorFakePhaseEffort,
    SensorFakePhaseCooldown,
} SensorFakePhase;

typedef struct {
    float hr;
    float last_hr;
    float start_hr;
    float warmup_peak;
    float tempo_start;
    float tempo_end;
    float session_max;
    float micro;
    int32_t session_seconds;
    int32_t warmup_end;
    int32_t build_end;
    int32_t effort_end;
    int32_t peak_at;
    int32_t peak_len;
    int32_t spike_age;
    int32_t spike_len;
    float spike_amp;
    int32_t spike_cooldown;
    int32_t same_streak;
    uint8_t last_broadcast;
    float speed_mps;
    float last_speed;
    uint32_t distance_tenths;
    bool walk_mode;
    float walk_center;
    float walk_drift;

    /* Results of the last step */
    uint8_t broadcast_hr;
    uint8_t cadence;
    bool running;
} SensorFakeSim;

/* Random source, provided by the platform (hardware RNG on the Flipper). */
uint32_t sensorfake_sim_rand(void);

void sensorfake_sim_init(SensorFakeSim* sim);

/* Start a new tempo-run session */
void sensorfake_sim_start_run(SensorFakeSim* sim);

/* Start a new walk session */
void sensorfake_sim_start_walk(SensorFakeSim* sim);

/* Advance the session by one second */
void sensorfake_sim_step(SensorFakeSim* sim);

SensorFakePhase sensorfake_sim_get_phase(const SensorFakeSim* sim);

/* Heart Rate Measurement (0x2A37) payload */
void sensorfake_sim_get_hrm_packet(const SensorFakeSim* sim, uint8_t* packet);

/* RSC Measurement (0x2A53) payload */
void sensorfake_sim_get_rsc_packet(const SensorFakeSim* sim, uint8_t* packet);

#ifdef __cplusplus
}
#endif
