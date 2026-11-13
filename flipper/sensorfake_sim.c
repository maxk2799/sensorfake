#include "sensorfake_sim.h"

#include <string.h>

static float clampf(float v, float lo, float hi) {
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

static float smoothstep(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/* Same contract as Arduino's random(lo, hi): lo inclusive, hi exclusive */
static int32_t rnd(int32_t lo, int32_t hi) {
    if(hi <= lo) return lo;
    return lo + (int32_t)(sensorfake_sim_rand() % (uint32_t)(hi - lo));
}

static float hr_to_speed(float bpm) {
    if(bpm < 100.0f) return clampf(1.50f + (bpm - 88.0f) * 0.045f, 0.0f, 2.05f);
    if(bpm < 110.0f) return 2.05f + (bpm - 100.0f) * 0.017f;
    if(bpm < 145.0f) return 2.22f + (bpm - 110.0f) * 0.0137f;
    if(bpm < 170.0f) return 2.70f + (bpm - 145.0f) * 0.0232f;
    return 3.28f + (bpm - 170.0f) * 0.0193f;
}

static float hr_to_walk_speed(float bpm) {
    return clampf(1.38f + (bpm - 118.0f) * 0.022f, 1.30f, 1.80f);
}

static bool in_walk_steady(const SensorFakeSim* sim) {
    return sim->walk_mode && sim->session_seconds > sim->warmup_end &&
           sim->session_seconds <= sim->effort_end;
}

/* Short triangular bump on top of the baseline: 40% rise, 60% fall */
static float spike_step(SensorFakeSim* sim) {
    float lift = 0.0f;
    if(sim->spike_len > 0) {
        sim->spike_age++;
        float st = (float)sim->spike_age / (float)sim->spike_len;
        if(st < 0.4f)
            lift = sim->spike_amp * (st / 0.4f);
        else
            lift = sim->spike_amp * (1.0f - ((st - 0.4f) / 0.6f));
        if(sim->spike_age >= sim->spike_len) {
            sim->spike_len = 0;
            sim->spike_age = 0;
            sim->spike_amp = 0.0f;
        }
    }
    return lift;
}

static void reset_session_state(SensorFakeSim* sim) {
    sim->hr = sim->start_hr;
    sim->last_hr = sim->start_hr;
    sim->micro = 0.0f;
    sim->session_seconds = 0;
    sim->spike_age = 0;
    sim->spike_len = 0;
    sim->spike_amp = 0.0f;
    sim->same_streak = 0;
    sim->last_broadcast = (uint8_t)(sim->start_hr + 0.5f);
    sim->broadcast_hr = sim->last_broadcast;
    sim->distance_tenths = 0;
    sim->cadence = 0;
    sim->running = false;
}

void sensorfake_sim_init(SensorFakeSim* sim) {
    memset(sim, 0, sizeof(SensorFakeSim));
    sim->hr = 100.0f;
    sim->last_hr = 100.0f;
    sim->start_hr = 100.0f;
    sim->warmup_peak = 145.0f;
    sim->tempo_start = 168.0f;
    sim->tempo_end = 174.0f;
    sim->session_max = 182.0f;
    sim->warmup_end = 360;
    sim->build_end = 600;
    sim->effort_end = 1800;
    sim->peak_at = 1200;
    sim->peak_len = 10;
    sim->last_broadcast = 100;
    sim->broadcast_hr = 100;
    sim->speed_mps = 2.1f;
    sim->last_speed = 2.1f;
    sim->walk_center = 125.0f;
}

void sensorfake_sim_start_run(SensorFakeSim* sim) {
    sim->walk_mode = false;

    sim->start_hr = rnd(90, 111);
    sim->warmup_peak = rnd(140, 152);
    sim->tempo_start = rnd(168, 171);
    sim->tempo_end = rnd(172, 176);
    sim->session_max = rnd(180, 185);

    sim->warmup_end = rnd(300, 481);
    sim->build_end = sim->warmup_end + rnd(180, 331);
    sim->effort_end = sim->build_end + rnd(900, 1801);
    sim->peak_at = sim->build_end + rnd(180, (sim->effort_end - sim->build_end) - 90);
    sim->peak_len = rnd(10, 18);

    reset_session_state(sim);
    sim->spike_cooldown = rnd(45, 100);
    sim->speed_mps = hr_to_speed(sim->start_hr);
    sim->last_speed = sim->speed_mps;
}

void sensorfake_sim_start_walk(SensorFakeSim* sim) {
    sim->walk_mode = true;

    sim->start_hr = clampf(sim->last_hr, 90.0f, 140.0f);
    sim->walk_center = rnd(123, 128);
    sim->walk_drift = 0.0f;
    sim->session_max = 132.0f;
    sim->warmup_peak = sim->walk_center;

    sim->warmup_end = rnd(120, 241);
    sim->build_end = sim->warmup_end;
    sim->effort_end = rnd(2340, 2521);
    sim->peak_at = sim->effort_end + 1000;
    sim->peak_len = 0;

    reset_session_state(sim);
    sim->spike_cooldown = rnd(40, 90);
    sim->speed_mps = hr_to_walk_speed(sim->start_hr);
    sim->last_speed = sim->speed_mps;
}

void sensorfake_sim_step(SensorFakeSim* sim) {
    sim->session_seconds++;

    const int32_t seconds = sim->session_seconds;
    float baseline = sim->start_hr;
    bool in_peak = false;

    if(sim->walk_mode) {
        if(seconds <= sim->warmup_end) {
            float t = (float)seconds / (float)sim->warmup_end;
            baseline = sim->start_hr + (sim->walk_center - sim->start_hr) * smoothstep(t);
        } else if(seconds <= sim->effort_end) {
            sim->walk_drift += rnd(-8, 9) / 100.0f;
            sim->walk_drift = clampf(sim->walk_drift, -4.0f, 4.0f);
            baseline = sim->walk_center + sim->walk_drift;

            if(sim->spike_cooldown > 0) sim->spike_cooldown--;
            if(sim->spike_len == 0 && sim->spike_cooldown == 0 && rnd(0, 100) < 3) {
                sim->spike_amp = rnd(2, 5);
                sim->spike_len = rnd(12, 25);
                sim->spike_age = 0;
                sim->spike_cooldown = sim->spike_len + rnd(60, 160);
            }
            baseline += spike_step(sim);
            baseline = clampf(baseline, 120.0f, 130.0f);
        } else {
            int32_t recovered = seconds - sim->effort_end;
            if(recovered < 75)
                baseline = sim->last_hr - (0.40f + rnd(0, 16) / 100.0f);
            else if(recovered < 180)
                baseline = sim->last_hr - (0.18f + rnd(0, 10) / 100.0f);
            else
                baseline = sim->last_hr - (0.08f + rnd(0, 6) / 100.0f);
            if(baseline < 88.0f) baseline = 88.0f + rnd(0, 4);
        }
    } else if(seconds <= sim->warmup_end) {
        float t = (float)seconds / (float)sim->warmup_end;
        baseline = sim->start_hr + (sim->warmup_peak - sim->start_hr) * smoothstep(t);
    } else if(seconds <= sim->build_end) {
        float t = (float)(seconds - sim->warmup_end) / (float)(sim->build_end - sim->warmup_end);
        baseline = sim->warmup_peak + (sim->tempo_start - sim->warmup_peak) * smoothstep(t);
    } else if(seconds <= sim->effort_end) {
        float tempo_len = (float)(sim->effort_end - sim->build_end);
        float t = (float)(seconds - sim->build_end) / tempo_len;
        baseline = sim->tempo_start + (sim->tempo_end - sim->tempo_start) * t;

        if(seconds >= sim->peak_at && seconds < sim->peak_at + sim->peak_len) {
            in_peak = true;
            float pt = (float)(seconds - sim->peak_at) / (float)sim->peak_len;
            float peak_lift = sim->session_max - baseline;
            if(pt < 0.4f)
                baseline += peak_lift * (pt / 0.4f);
            else
                baseline += peak_lift * (1.0f - ((pt - 0.4f) / 0.6f));
        }

        if(!in_peak) {
            if(sim->spike_cooldown > 0) sim->spike_cooldown--;
            if(sim->spike_len == 0 && sim->spike_cooldown == 0 && rnd(0, 100) < 4) {
                sim->spike_amp = rnd(4, 9);
                sim->spike_len = rnd(10, 21);
                sim->spike_age = 0;
                sim->spike_cooldown = sim->spike_len + rnd(50, 140);
            }
            baseline += spike_step(sim);
        }
    } else {
        int32_t recovered = seconds - sim->effort_end;
        if(recovered < 75)
            baseline = sim->last_hr - (0.55f + rnd(0, 20) / 100.0f);
        else if(recovered < 180)
            baseline = sim->last_hr - (0.22f + rnd(0, 12) / 100.0f);
        else
            baseline = sim->last_hr - (0.10f + rnd(0, 8) / 100.0f);
        if(baseline < 88.0f) baseline = 88.0f + rnd(0, 4);
    }

    sim->micro += rnd(-12, 13) / 10.0f;
    sim->micro = clampf(sim->micro, sim->walk_mode ? -2.0f : -3.0f, sim->walk_mode ? 2.0f : 3.0f);

    float target = baseline + sim->micro;
    if(seconds > sim->effort_end) {
        target = baseline + clampf(sim->micro, -1.2f, 0.4f);
        if(target > sim->last_hr - 0.08f && sim->last_hr > 92.0f) target = sim->last_hr - 0.12f;
    }
    if(in_walk_steady(sim)) {
        target = clampf(target, 120.0f, 130.0f);
    } else if(!in_peak) {
        target = clampf(target, 80.0f, sim->session_max - 4.0f);
    } else {
        target = clampf(target, 80.0f, sim->session_max);
    }

    float max_step = in_peak ? 2.5f : 1.6f;
    if(seconds > sim->effort_end) max_step = 0.9f;
    if(sim->walk_mode && seconds <= sim->effort_end) max_step = 0.8f;
    float delta = target - sim->last_hr;
    if(delta > max_step) target = sim->last_hr + max_step;
    if(delta < -max_step) target = sim->last_hr - max_step;

    sim->hr = target;
    sim->last_hr = sim->hr;

    /* Never broadcast the same value twice in a row */
    uint8_t broadcast_hr = (uint8_t)(sim->hr + 0.5f);
    if(broadcast_hr == sim->last_broadcast) {
        sim->same_streak++;
        if(sim->same_streak >= 1) {
            int32_t dir = (sim->micro >= 0.0f) ? 1 : -1;
            int32_t next = (int32_t)broadcast_hr + dir;
            if(next < 80 || next > (int32_t)sim->session_max || next == (int32_t)broadcast_hr) {
                next = (int32_t)broadcast_hr - dir;
            }
            next = (int32_t)clampf((float)next, 80.0f, sim->session_max);
            if(in_walk_steady(sim)) {
                next = (int32_t)clampf((float)next, 120.0f, 130.0f);
            }
            if(next == (int32_t)sim->last_broadcast) {
                next = (int32_t)sim->last_broadcast + (dir == 1 ? -1 : 1);
            }
            broadcast_hr = (uint8_t)clampf((float)next, 80.0f, sim->session_max);
            sim->same_streak = 0;
        }
    } else {
        sim->same_streak = 0;
    }
    if(in_walk_steady(sim)) {
        broadcast_hr = (uint8_t)clampf((float)broadcast_hr, 120.0f, 130.0f);
    }
    sim->last_broadcast = broadcast_hr;
    sim->broadcast_hr = broadcast_hr;

    float target_speed = 0.0f;
    float max_speed_step = 0.07f;
    if(seconds <= sim->effort_end) {
        if(sim->walk_mode) {
            target_speed = hr_to_walk_speed((float)broadcast_hr);
            max_speed_step = 0.04f;
        } else {
            target_speed = hr_to_speed((float)broadcast_hr);
            max_speed_step = 0.08f;
        }
    } else {
        target_speed = 0.0f;
        max_speed_step = 0.30f;
    }
    float sdelta = target_speed - sim->last_speed;
    if(sdelta > max_speed_step) target_speed = sim->last_speed + max_speed_step;
    if(sdelta < -max_speed_step) target_speed = sim->last_speed - max_speed_step;
    if(seconds > sim->effort_end && target_speed < 0.25f) target_speed = 0.0f;
    sim->speed_mps = clampf(target_speed, 0.0f, 4.2f);
    sim->last_speed = sim->speed_mps;
    if(sim->speed_mps > 0.2f) sim->distance_tenths += (uint32_t)(sim->speed_mps * 10.0f + 0.5f);

    sim->cadence = 0;
    if(sim->speed_mps > 0.5f) {
        if(sim->walk_mode) {
            sim->cadence =
                (uint8_t)clampf(52.0f + sim->speed_mps * 7.0f + rnd(-1, 2), 50.0f, 68.0f);
        } else {
            sim->cadence =
                (uint8_t)clampf(74.0f + sim->speed_mps * 6.2f + rnd(-1, 2), 70.0f, 96.0f);
        }
    }
    sim->running = sim->speed_mps >= 2.05f;
}

SensorFakePhase sensorfake_sim_get_phase(const SensorFakeSim* sim) {
    if(sim->session_seconds <= sim->warmup_end) return SensorFakePhaseWarmup;
    if(sim->session_seconds <= sim->build_end) return SensorFakePhaseBuild;
    if(sim->session_seconds <= sim->effort_end) return SensorFakePhaseEffort;
    return SensorFakePhaseCooldown;
}

void sensorfake_sim_get_hrm_packet(const SensorFakeSim* sim, uint8_t* packet) {
    /* Flags: 8-bit heart rate, sensor contact supported and detected */
    packet[0] = 0x06;
    packet[1] = sim->broadcast_hr;
}

void sensorfake_sim_get_rsc_packet(const SensorFakeSim* sim, uint8_t* packet) {
    uint16_t spd256 = (uint16_t)(sim->speed_mps * 256.0f + 0.5f);
    /* Flags: total distance present, plus walking/running status */
    uint8_t flags = 0x02;
    if(sim->running) flags |= 0x04;

    packet[0] = flags;
    packet[1] = (uint8_t)(spd256 & 0xFF);
    packet[2] = (uint8_t)(spd256 >> 8);
    packet[3] = sim->cadence;
    packet[4] = (uint8_t)(sim->distance_tenths & 0xFF);
    packet[5] = (uint8_t)((sim->distance_tenths >> 8) & 0xFF);
    packet[6] = (uint8_t)((sim->distance_tenths >> 16) & 0xFF);
    packet[7] = (uint8_t)((sim->distance_tenths >> 24) & 0xFF);
}
