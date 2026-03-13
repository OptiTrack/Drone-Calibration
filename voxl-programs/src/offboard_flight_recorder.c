/*******************************************************************************
 * offboard_flight_recorder.c
 *
 * Manual-flight recorder and autonomous-playback module for the
 * Starling 2 Max / VOXL 2.
 * Modeled after offboard_figure_eight.c from voxl-vision-hub.
 *
 * See offboard_flight_recorder.h for a full description.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#include "config_file.h"
#include "mavlink_io.h"
#include "autopilot_monitor.h"
#include "geometry.h"
#include "macros.h"
#include "offboard_flight_recorder.h"
#include "misc.h"

/* ============================================================================
 * Internal constants
 * ========================================================================= */

/** Maximum number of samples that can be buffered. */
#define MAX_SAMPLES  (RECORDER_RATE_HZ * RECORDER_MAX_MINUTES * 60)

/** Seconds to pre-warm the setpoint stream before waiting for offboard mode. */
#define PREWARM_S   3

/** Seconds to hold the starting position after entering offboard, before
 *  starting playback.  Gives the drone time to stabilise. */
#define PLAYBACK_START_HOLD_S  2

/** Seconds to hold the final position after the last sample. */
#define PLAYBACK_END_HOLD_S    2

#define PREWARM_STEPS          (PREWARM_S * RECORDER_RATE_HZ)
#define START_HOLD_STEPS       (PLAYBACK_START_HOLD_S * RECORDER_RATE_HZ)
#define END_HOLD_STEPS         (PLAYBACK_END_HOLD_S * RECORDER_RATE_HZ)

/* ============================================================================
 * Internal sample type
 * ========================================================================= */

typedef struct {
    float t;                    /* Elapsed time since recording start [s] */
    float x, y, z;             /* Position, MAV_FRAME_LOCAL_NED [m]      */
    float vx, vy, vz;          /* Velocity, NED [m/s]                    */
    float yaw;                  /* Yaw angle [rad], NED convention        */
} FlightSample;

/* ============================================================================
 * Module state
 * ========================================================================= */

/* Shared thread + state flags */
static volatile int  running_record  = 0;
static volatile int  running_playback = 0;
static pthread_t     record_thread_id;
static pthread_t     playback_thread_id;
static int           en_debug        = 0;

/* Recording buffer (static allocation – avoids malloc fragmentation) */
static FlightSample  g_buf[MAX_SAMPLES];
static int           g_buf_count = 0;
static pthread_mutex_t g_buf_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Playback buffer (loaded from file) */
static FlightSample *g_play_buf  = NULL;
static int           g_play_count = 0;

/* ============================================================================
 * Helpers
 * ========================================================================= */

/** Extract yaw from a MAVLink quaternion [w, x, y, z]. */
static float _quat_to_yaw(const float q[4])
{
    /* yaw = atan2(2*(qw*qz + qx*qy), 1 - 2*(qy^2 + qz^2)) */
    float qw = q[0], qx = q[1], qy = q[2], qz = q[3];
    return atan2f(2.0f * (qw * qz + qx * qy),
                  1.0f - 2.0f * (qy * qy + qz * qz));
}

/** Build a position + velocity setpoint (acceleration / yaw_rate ignored). */
static void _make_pv_sp(mavlink_set_position_target_local_ned_t *sp,
                         float x,  float y,  float z,
                         float vx, float vy, float vz,
                         float yaw)
{
    memset(sp, 0, sizeof(*sp));
    sp->coordinate_frame = MAV_FRAME_LOCAL_NED;
    sp->type_mask        = POSITION_TARGET_TYPEMASK_AX_IGNORE
                         | POSITION_TARGET_TYPEMASK_AY_IGNORE
                         | POSITION_TARGET_TYPEMASK_AZ_IGNORE
                         | POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE;
    sp->target_system    = 0;
    sp->target_component = AUTOPILOT_COMPID;
    sp->x   = x;  sp->y  = y;  sp->z  = z;
    sp->vx  = vx; sp->vy = vy; sp->vz = vz;
    sp->yaw = yaw;
}

/** Build a position-only (hold) setpoint. */
static void _make_hold_sp(mavlink_set_position_target_local_ned_t *sp,
                           float x, float y, float z, float yaw)
{
    memset(sp, 0, sizeof(*sp));
    sp->coordinate_frame = MAV_FRAME_LOCAL_NED;
    sp->type_mask        = POSITION_TARGET_TYPEMASK_VX_IGNORE
                         | POSITION_TARGET_TYPEMASK_VY_IGNORE
                         | POSITION_TARGET_TYPEMASK_VZ_IGNORE
                         | POSITION_TARGET_TYPEMASK_AX_IGNORE
                         | POSITION_TARGET_TYPEMASK_AY_IGNORE
                         | POSITION_TARGET_TYPEMASK_AZ_IGNORE
                         | POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE;
    sp->target_system    = 0;
    sp->target_component = AUTOPILOT_COMPID;
    sp->x   = x; sp->y = y; sp->z = z; sp->yaw = yaw;
}

static inline void _send_sp(const mavlink_set_position_target_local_ned_t *sp)
{
    mavlink_set_position_target_local_ned_t buf = *sp;
    mavlink_io_send_fixed_setpoint(autopilot_monitor_get_sysid(), VOXL_COMPID, buf);
}

/* ============================================================================
 * Recording thread
 * ========================================================================= */

static void *_record_thread_func(__attribute__((unused)) void *arg)
{
    int64_t next_time = 0;
    struct timespec ts_start, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    if (en_debug) printf("[RECORDER] Recording started. Max samples: %d\n", MAX_SAMPLES);

    while (running_record) {

        mavlink_odometry_t odom = autopilot_monitor_get_odometry();

        /* Elapsed time since recording started */
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        float elapsed = (float)(ts_now.tv_sec  - ts_start.tv_sec)
                      + (float)(ts_now.tv_nsec - ts_start.tv_nsec) * 1e-9f;

        FlightSample s;
        s.t   = elapsed;
        s.x   = odom.x;
        s.y   = odom.y;
        s.z   = odom.z;
        s.vx  = odom.vx;
        s.vy  = odom.vy;
        s.vz  = odom.vz;
        s.yaw = _quat_to_yaw(odom.q);

        pthread_mutex_lock(&g_buf_mutex);
        if (g_buf_count < MAX_SAMPLES) {
            g_buf[g_buf_count++] = s;
        } else {
            /* Buffer full – warn once then stop recording */
            fprintf(stderr, "[RECORDER] Buffer full (%d s). Stopping automatically.\n",
                    RECORDER_MAX_MINUTES * 60);
            pthread_mutex_unlock(&g_buf_mutex);
            running_record = 0;
            break;
        }
        pthread_mutex_unlock(&g_buf_mutex);

        if (my_loop_sleep(RECORDER_RATE_HZ, &next_time)) {
            fprintf(stderr, "WARNING [recorder]: record loop fell behind\n");
        }
    }

    if (en_debug) {
        printf("[RECORDER] Recording stopped. %d samples captured (%.1f s).\n",
               g_buf_count,
               g_buf_count > 0 ? g_buf[g_buf_count - 1].t : 0.0f);
    }
    return NULL;
}

/* ============================================================================
 * Recording API
 * ========================================================================= */

int offboard_recorder_start_recording(void)
{
    if (running_record) {
        fprintf(stderr, "[RECORDER] Already recording.\n");
        return -1;
    }
    if (running_playback) {
        fprintf(stderr, "[RECORDER] Playback in progress – stop it first.\n");
        return -1;
    }

    /* Reset buffer */
    pthread_mutex_lock(&g_buf_mutex);
    g_buf_count = 0;
    pthread_mutex_unlock(&g_buf_mutex);

    running_record = 1;
    pipe_pthread_create(&record_thread_id, _record_thread_func,
                        NULL, OFFBOARD_THREAD_PRIORITY);
    printf("[RECORDER] Recording started.\n");
    return 0;
}

int offboard_recorder_stop_and_save(const char *save_path)
{
    if (!running_record) {
        fprintf(stderr, "[RECORDER] Not currently recording.\n");
        return -1;
    }

    /* Stop capture thread */
    running_record = 0;
    pthread_join(record_thread_id, NULL);

    pthread_mutex_lock(&g_buf_mutex);
    int n = g_buf_count;
    pthread_mutex_unlock(&g_buf_mutex);

    if (n == 0) {
        fprintf(stderr, "[RECORDER] No samples to save.\n");
        return -1;
    }

    /* Open output file */
    FILE *fp = fopen(save_path, "w");
    if (!fp) {
        fprintf(stderr, "[RECORDER] Failed to open output file: %s\n", save_path);
        return -1;
    }

    /* ISO-8601 timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char date_str[32];
    strftime(date_str, sizeof(date_str), "%Y-%m-%dT%H:%M:%SZ", tm_info);

    /* Compute recording home (first sample position) */
    float home_x = g_buf[0].x;
    float home_y = g_buf[0].y;
    float home_z = g_buf[0].z;

    /* Write JSON header */
    fprintf(fp,
            "{\n"
            "  \"magic\"       : \"VOXL_FLIGHT_RECORDING\",\n"
            "  \"version\"     : 1,\n"
            "  \"date\"        : \"%s\",\n"
            "  \"rate_hz\"     : %d,\n"
            "  \"num_samples\" : %d,\n"
            "  \"home_x\"      : %.6f,\n"
            "  \"home_y\"      : %.6f,\n"
            "  \"home_z\"      : %.6f,\n"
            "  \"samples\"     : [\n",
            date_str, RECORDER_RATE_HZ, n,
            (double)home_x, (double)home_y, (double)home_z);

    /* Write each sample as a compact row array */
    for (int i = 0; i < n; ++i) {
        const FlightSample *s = &g_buf[i];
        char trail = (i < n - 1) ? ',' : ' ';
        fprintf(fp,
                "    [%.4f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f]%c\n",
                (double)s->t,
                (double)s->x, (double)s->y, (double)s->z,
                (double)s->vx,(double)s->vy,(double)s->vz,
                (double)s->yaw, trail);
    }

    fprintf(fp, "  ]\n}\n");
    fclose(fp);

    printf("[RECORDER] Saved %d samples to %s\n", n, save_path);
    return n;
}

int offboard_recorder_is_recording(void)  { return running_record; }
int offboard_recorder_sample_count(void)
{
    pthread_mutex_lock(&g_buf_mutex);
    int c = g_buf_count;
    pthread_mutex_unlock(&g_buf_mutex);
    return c;
}

/* ============================================================================
 * JSON parser (minimal, reads only our own recording format)
 * ========================================================================= */

/**
 * Parse a recording file into a newly allocated FlightSample array.
 *
 * @param file_path   Path to JSON recording file.
 * @param out_count   [out] Number of samples parsed.
 * @param out_home_x  [out] Home x recorded at capture time.
 * @param out_home_y  [out] Home y recorded at capture time.
 * @param out_home_z  [out] Home z recorded at capture time.
 * @return  Malloc'd array on success (caller must free), NULL on error.
 */
static FlightSample *_load_recording(const char *file_path,
                                      int   *out_count,
                                      float *out_home_x,
                                      float *out_home_y,
                                      float *out_home_z)
{
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        fprintf(stderr, "[RECORDER] Cannot open file: %s\n", file_path);
        return NULL;
    }

    /* ---- Pass 1: read header fields ---- */
    int num_samples = 0;
    float home_x = 0.0f, home_y = 0.0f, home_z = 0.0f;
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "\"num_samples\"")) {
            sscanf(line, " \"num_samples\" : %d", &num_samples);
        } else if (strstr(line, "\"home_x\"")) {
            double tmp; sscanf(line, " \"home_x\" : %lf", &tmp); home_x = (float)tmp;
        } else if (strstr(line, "\"home_y\"")) {
            double tmp; sscanf(line, " \"home_y\" : %lf", &tmp); home_y = (float)tmp;
        } else if (strstr(line, "\"home_z\"")) {
            double tmp; sscanf(line, " \"home_z\" : %lf", &tmp); home_z = (float)tmp;
        } else if (strstr(line, "\"samples\"")) {
            break; /* Rest is sample data */
        }
    }

    if (num_samples <= 0) {
        fprintf(stderr, "[RECORDER] Failed to parse num_samples from %s\n", file_path);
        fclose(fp);
        return NULL;
    }

    /* ---- Allocate output buffer ---- */
    FlightSample *buf = (FlightSample *)malloc((size_t)num_samples * sizeof(FlightSample));
    if (!buf) {
        fprintf(stderr, "[RECORDER] Out of memory for %d samples\n", num_samples);
        fclose(fp);
        return NULL;
    }

    /* ---- Pass 2: read sample rows ---- */
    int count = 0;
    while (count < num_samples && fgets(line, sizeof(line), fp)) {
        /* Each line looks like:  [t, x, y, z, vx, vy, vz, yaw], */
        const char *p = strchr(line, '[');
        if (!p) continue;

        double t, x, y, z, vx, vy, vz, yaw;
        int matched = sscanf(p,
                             "[%lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf]",
                             &t, &x, &y, &z, &vx, &vy, &vz, &yaw);
        if (matched != 8) continue;

        buf[count].t   = (float)t;
        buf[count].x   = (float)x;
        buf[count].y   = (float)y;
        buf[count].z   = (float)z;
        buf[count].vx  = (float)vx;
        buf[count].vy  = (float)vy;
        buf[count].vz  = (float)vz;
        buf[count].yaw = (float)yaw;
        ++count;
    }

    fclose(fp);

    if (count == 0) {
        fprintf(stderr, "[RECORDER] No valid samples found in %s\n", file_path);
        free(buf);
        return NULL;
    }

    if (count < num_samples) {
        fprintf(stderr, "[RECORDER] Warning: expected %d samples, got %d.\n",
                num_samples, count);
    }

    *out_count  = count;
    *out_home_x = home_x;
    *out_home_y = home_y;
    *out_home_z = home_z;
    return buf;
}

/* ============================================================================
 * Playback thread
 * ========================================================================= */

/* Home offset applied to all setpoints so the trajectory is relative to the
 * drone's actual arming position rather than the recorded origin. */
static float g_offset_x = 0.0f;
static float g_offset_y = 0.0f;
/* Note: we do not offset z – we preserve the recorded altitude profile. */

typedef enum {
    PB_STATE_PREWARM = 0,
    PB_STATE_WAIT_OFFBOARD,
    PB_STATE_START_HOLD,
    PB_STATE_PLAY,
    PB_STATE_END_HOLD,
    PB_STATE_DONE
} PlaybackState;

static void *_playback_thread_func(__attribute__((unused)) void *arg)
{
    int64_t next_time = 0;
    int     step      = 0;
    int     sample    = 0;
    PlaybackState pb_state = PB_STATE_PREWARM;

    mavlink_set_position_target_local_ned_t sp;

    if (g_play_count == 0 || g_play_buf == NULL) {
        fprintf(stderr, "[RECORDER] Playback buffer empty – aborting.\n");
        running_playback = 0;
        return NULL;
    }

    /* Build initial "current position" hold setpoint from the first sample
     * (before we know the home offset). */
    _make_hold_sp(&sp,
                  g_play_buf[0].x, g_play_buf[0].y, g_play_buf[0].z,
                  g_play_buf[0].yaw);

    if (en_debug) {
        printf("[RECORDER] Playback ready. %d samples (~%.1f s at %d Hz).\n",
               g_play_count,
               (float)g_play_count / (float)RECORDER_RATE_HZ,
               RECORDER_RATE_HZ);
    }

    while (running_playback) {

        int armed_offboard = autopilot_monitor_is_armed_and_in_offboard_mode();

        switch (pb_state) {

        /* ------------------------------------------------------------------ */
        case PB_STATE_PREWARM:
            /* Send the first sample's position to fill PX4's setpoint buffer
             * before the operator enables offboard mode. */
            _make_hold_sp(&sp,
                          g_play_buf[0].x, g_play_buf[0].y, g_play_buf[0].z,
                          g_play_buf[0].yaw);
            _send_sp(&sp);
            ++step;
            if (step >= PREWARM_STEPS) {
                pb_state = PB_STATE_WAIT_OFFBOARD;
                step     = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        case PB_STATE_WAIT_OFFBOARD:
            /* Keep sending the hold setpoint. */
            _send_sp(&sp);
            if (armed_offboard) {
                /* Compute home offset = (current odometry) - (recorded home) */
                mavlink_odometry_t odom = autopilot_monitor_get_odometry();
                g_offset_x = odom.x - g_play_buf[0].x;
                g_offset_y = odom.y - g_play_buf[0].y;
                if (en_debug) {
                    printf("[RECORDER] Offboard active. Home offset: (%.3f, %.3f) m\n",
                           (double)g_offset_x, (double)g_offset_y);
                    printf("[RECORDER] Beginning start-hold before playback.\n");
                }
                /* Update hold setpoint to the actual starting position */
                _make_hold_sp(&sp,
                              g_play_buf[0].x + g_offset_x,
                              g_play_buf[0].y + g_offset_y,
                              g_play_buf[0].z,
                              g_play_buf[0].yaw);
                pb_state = PB_STATE_START_HOLD;
                step     = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        case PB_STATE_START_HOLD:
            if (!armed_offboard) { pb_state = PB_STATE_WAIT_OFFBOARD; step = 0; break; }
            _send_sp(&sp);
            ++step;
            if (step >= START_HOLD_STEPS) {
                if (en_debug) printf("[RECORDER] Start hold complete – beginning playback.\n");
                pb_state = PB_STATE_PLAY;
                sample   = 0;
                step     = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        case PB_STATE_PLAY:
            if (!armed_offboard) {
                /* Pause at current sample; resume when offboard returns */
                if (en_debug) printf("[RECORDER] Offboard lost at sample %d – pausing.\n", sample);
                pb_state = PB_STATE_WAIT_OFFBOARD;
                step     = 0;
                break;
            }
            {
                const FlightSample *s = &g_play_buf[sample];
                _make_pv_sp(&sp,
                            s->x  + g_offset_x,
                            s->y  + g_offset_y,
                            s->z,
                            s->vx, s->vy, s->vz,
                            s->yaw);
                _send_sp(&sp);

                if (en_debug && (sample % RECORDER_RATE_HZ == 0)) {
                    printf("[RECORDER] Playback %.1f s / %.1f s (sample %d/%d)\n",
                           (double)s->t,
                           (double)g_play_buf[g_play_count - 1].t,
                           sample + 1, g_play_count);
                }

                ++sample;
                if (sample >= g_play_count) {
                    if (en_debug) printf("[RECORDER] Playback complete – holding end position.\n");
                    /* Hold at the last sample's position */
                    const FlightSample *last = &g_play_buf[g_play_count - 1];
                    _make_hold_sp(&sp,
                                  last->x + g_offset_x,
                                  last->y + g_offset_y,
                                  last->z, last->yaw);
                    pb_state = PB_STATE_END_HOLD;
                    step     = 0;
                }
            }
            break;

        /* ------------------------------------------------------------------ */
        case PB_STATE_END_HOLD:
            /* Hold at end position so the operator can regain manual control. */
            if (!armed_offboard) { pb_state = PB_STATE_WAIT_OFFBOARD; step = 0; break; }
            _send_sp(&sp);
            ++step;
            if (step >= END_HOLD_STEPS) {
                pb_state = PB_STATE_DONE;
            }
            break;

        /* ------------------------------------------------------------------ */
        case PB_STATE_DONE:
            running_playback = 0;
            break;

        default:
            break;
        }

        if (my_loop_sleep(RECORDER_RATE_HZ, &next_time)) {
            fprintf(stderr, "WARNING [recorder]: playback loop fell behind\n");
        }
    } /* end while */

    printf("[RECORDER] Playback thread exiting.\n");

    /* Free the playback buffer */
    if (g_play_buf) {
        free(g_play_buf);
        g_play_buf   = NULL;
        g_play_count = 0;
    }

    return NULL;
}

/* ============================================================================
 * Playback API
 * ========================================================================= */

int offboard_recorder_start_playback(const char *file_path)
{
    if (running_playback) {
        fprintf(stderr, "[RECORDER] Already playing back.\n");
        return -1;
    }
    if (running_record) {
        fprintf(stderr, "[RECORDER] Recording in progress – stop it first.\n");
        return -1;
    }

    /* Free any previously loaded playback buffer */
    if (g_play_buf) {
        free(g_play_buf);
        g_play_buf   = NULL;
        g_play_count = 0;
    }

    float rec_home_x = 0.0f, rec_home_y = 0.0f, rec_home_z = 0.0f;
    g_play_buf = _load_recording(file_path, &g_play_count,
                                  &rec_home_x, &rec_home_y, &rec_home_z);
    if (!g_play_buf) {
        return -1;
    }

    /* The g_offset will be computed once offboard mode is detected, using
     * the live odometry at that moment.  For now, set offset = 0 so the
     * pre-warm setpoints are sent in the recorded frame. */
    g_offset_x = 0.0f;
    g_offset_y = 0.0f;

    printf("[RECORDER] Loaded %d samples from %s\n", g_play_count, file_path);
    if (en_debug) {
        printf("           Recorded home: (%.3f, %.3f, %.3f)\n",
               (double)rec_home_x, (double)rec_home_y, (double)rec_home_z);
    }

    running_playback = 1;
    pipe_pthread_create(&playback_thread_id, _playback_thread_func,
                        NULL, OFFBOARD_THREAD_PRIORITY);
    return 0;
}

int offboard_recorder_stop(int blocking)
{
    running_record   = 0;
    running_playback = 0;

    if (blocking) {
        if (pthread_kill(record_thread_id, 0) == 0) {
            pthread_join(record_thread_id, NULL);
        }
        if (pthread_kill(playback_thread_id, 0) == 0) {
            pthread_join(playback_thread_id, NULL);
        }
    }
    return 0;
}

int offboard_recorder_is_playing(void) { return running_playback; }

void offboard_recorder_en_print_debug(int debug) { if (debug) en_debug = 1; }
