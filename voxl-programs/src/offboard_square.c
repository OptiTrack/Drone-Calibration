/*******************************************************************************
 * offboard_square.c
 *
 * Offboard square flight program for the Starling 2 Max / VOXL 2.
 * Modeled after offboard_figure_eight.c from voxl-vision-hub.
 *
 * Copyright notice: This program is original work for the Drone-Calibration
 * project and is not derived from ModalAI code.
 *
 * Description
 * -----------
 *  A background thread feeds PX4 MAVLink SET_POSITION_TARGET_LOCAL_NED
 *  setpoints at RATE Hz.  The mission proceeds through the following phases:
 *
 *    INIT         – Send 100 home-position setpoints so PX4 accepts offboard.
 *    WAIT_OFFBOARD– Block until the autopilot is armed + in offboard mode.
 *    TAKEOFF_HOLD – Hold the target altitude for SQUARE_TAKEOFF_HOLD_S s
 *                   so the drone climbs to the cruise altitude.
 *    FLY_SIDE_0   – Home (0,0) → Corner 1 (SIDE_M, 0) : +North
 *    HOLD_1       – Momentary hold at Corner 1.
 *    FLY_SIDE_1   – Corner 1 (SIDE_M,0) → Corner 2 (SIDE_M,SIDE_M) : +East
 *    HOLD_2       – Momentary hold at Corner 2.
 *    FLY_SIDE_2   – Corner 2 (SIDE_M,SIDE_M) → Corner 3 (0,SIDE_M) : -North
 *    HOLD_3       – Momentary hold at Corner 3.
 *    FLY_SIDE_3   – Corner 3 (0,SIDE_M) → Home (0,0) : -East (West)
 *    LAND_HOLD    – Hold at home altitude for SQUARE_LAND_HOLD_S s.
 *    DESCEND      – Gradually lower z setpoint until on the ground.
 *    DONE         – Thread exits.
 *
 * If PX4 drops out of armed+offboard at any point, the state jumps back to
 * WAIT_OFFBOARD and resumes from the home position when offboard is restored.
 *
 * Coordinate frame (MAV_FRAME_LOCAL_NED)
 * ---------------------------------------
 *  x = North, y = East, z = Down  (negative z = above ground)
 *  The home origin is wherever the drone is when offboard mode first activates.
 *
 * Tuning parameters – edit the #defines below before compiling.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include "config_file.h"
#include "mavlink_io.h"
#include "autopilot_monitor.h"
#include "geometry.h"
#include "macros.h"
#include "offboard_square.h"
#include "misc.h"

/* ============================================================================
 * Tuning parameters
 * ========================================================================= */

/** Cruise altitude above home [m].  Stored as negative NED z. */
#define SQUARE_FLIGHT_ALT_M     1.5f
#define FLIGHT_ALTITUDE         (-(SQUARE_FLIGHT_ALT_M))  /* NED z, negative = up */

/** One side of the square in metres.  6 ft = 1.8288 m. */
#define SIDE_M                  1.8288f

/** Horizontal cruise speed while flying each side [m/s]. */
#define CRUISE_SPEED_MS         0.8f

/** Setpoint loop rate [Hz]. */
#define RATE                    30

/** Time to hold at the target altitude after arming, before flying [s]. */
#define SQUARE_TAKEOFF_HOLD_S   3.0f

/** Time to hold at each square corner [s]. */
#define SQUARE_CORNER_HOLD_S    0.5f

/** Time to hover at home before descending [s]. */
#define SQUARE_LAND_HOLD_S      1.5f

/** Vertical descent speed during the landing phase [m/s]. */
#define DESCENT_RATE_MS         0.4f

/* ============================================================================
 * Derived constants (do not edit)
 * ========================================================================= */
#define DT                      (1.0f / (float)RATE)

/** Steps to complete one side at cruise speed. */
#define STEPS_PER_SIDE          ((int)((SIDE_M / CRUISE_SPEED_MS) * (float)RATE + 0.5f))

/** Hold steps at each corner. */
#define CORNER_HOLD_STEPS       ((int)(SQUARE_CORNER_HOLD_S * (float)RATE))

/** Hold steps on takeoff. */
#define TAKEOFF_HOLD_STEPS      ((int)(SQUARE_TAKEOFF_HOLD_S * (float)RATE))

/** Hold steps before landing. */
#define LAND_HOLD_STEPS         ((int)(SQUARE_LAND_HOLD_S * (float)RATE))

/** Steps to descend from FLIGHT_ALTITUDE to ground. */
#define DESCENT_STEPS           ((int)((SQUARE_FLIGHT_ALT_M / DESCENT_RATE_MS) * (float)RATE + 0.5f))

/* ============================================================================
 * Square corners in NED local frame relative to home origin
 *   corners[n][0] = x (North), corners[n][1] = y (East)
 *
 *   D(0,S) ─── C(S,S)
 *     |              |
 *   Home(0,0) ─── B(S,0)   where S = SIDE_M
 *
 * Flight order: Home → B → C → D → Home
 * ========================================================================= */
static const float corners[4][2] = {
    { 0.0f,   0.0f   },   /* 0: Home          */
    { SIDE_M, 0.0f   },   /* 1: North         */
    { SIDE_M, SIDE_M },   /* 2: North + East  */
    { 0.0f,   SIDE_M },   /* 3: East          */
};

/* Velocity vector for each side (vx, vy) and the corresponding NED yaw. */
struct SideParams {
    float vx, vy;
    float yaw; /* radians, NED convention: 0=North, +PI/2=East */
};

static const struct SideParams sides[4] = {
    {  CRUISE_SPEED_MS,  0.0f,            0.0f       },  /* → North */
    {  0.0f,             CRUISE_SPEED_MS, (float)PI_2 },  /* → East  */
    { -CRUISE_SPEED_MS,  0.0f,            (float)PI   },  /* → South */
    {  0.0f,            -CRUISE_SPEED_MS,-(float)PI_2 },  /* → West  */
};

/* ============================================================================
 * Module state
 * ========================================================================= */
typedef enum {
    STATE_INIT = 0,
    STATE_WAIT_OFFBOARD,
    STATE_TAKEOFF_HOLD,
    STATE_FLY_SIDE_0,
    STATE_HOLD_1,
    STATE_FLY_SIDE_1,
    STATE_HOLD_2,
    STATE_FLY_SIDE_2,
    STATE_HOLD_3,
    STATE_FLY_SIDE_3,
    STATE_LAND_HOLD,
    STATE_DESCEND,
    STATE_DONE
} SquareState;

static int          running  = 0;
static int          en_debug = 0;
static pthread_t    square_thread_id;

/* Home-position setpoint: position-only hold at (0,0,FLIGHT_ALTITUDE). */
static mavlink_set_position_target_local_ned_t home_sp;

/* ============================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * Build a full position + velocity setpoint.
 * Accelerations and yaw_rate are zeroed; both pos and vel fields are active.
 */
static void _make_pv_setpoint(mavlink_set_position_target_local_ned_t *sp,
                               float x,  float y,  float z,
                               float vx, float vy, float vz,
                               float yaw)
{
    memset(sp, 0, sizeof(*sp));
    sp->time_boot_ms      = 0;
    sp->coordinate_frame  = MAV_FRAME_LOCAL_NED;
    /* type_mask = 0 → use position, velocity, acceleration, yaw, yaw_rate.
     * We explicitly zero out the fields we don't want, so they act as 0
     * feed-forward terms which is benign. */
    sp->type_mask         = POSITION_TARGET_TYPEMASK_AX_IGNORE
                          | POSITION_TARGET_TYPEMASK_AY_IGNORE
                          | POSITION_TARGET_TYPEMASK_AZ_IGNORE
                          | POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE;
    sp->target_system     = 0;           /* filled in by mavlink_io */
    sp->target_component  = AUTOPILOT_COMPID;
    sp->x   = x;   sp->y  = y;   sp->z  = z;
    sp->vx  = vx;  sp->vy = vy;  sp->vz = vz;
    sp->yaw = yaw;
}

/**
 * Build a position-only hold setpoint (velocity / accel / yaw_rate ignored).
 */
static void _make_hold_setpoint(mavlink_set_position_target_local_ned_t *sp,
                                float x, float y, float z, float yaw)
{
    memset(sp, 0, sizeof(*sp));
    sp->time_boot_ms      = 0;
    sp->coordinate_frame  = MAV_FRAME_LOCAL_NED;
    sp->type_mask         = POSITION_TARGET_TYPEMASK_VX_IGNORE
                          | POSITION_TARGET_TYPEMASK_VY_IGNORE
                          | POSITION_TARGET_TYPEMASK_VZ_IGNORE
                          | POSITION_TARGET_TYPEMASK_AX_IGNORE
                          | POSITION_TARGET_TYPEMASK_AY_IGNORE
                          | POSITION_TARGET_TYPEMASK_AZ_IGNORE
                          | POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE;
    sp->target_system     = 0;
    sp->target_component  = AUTOPILOT_COMPID;
    sp->x   = x;
    sp->y   = y;
    sp->z   = z;
    sp->yaw = yaw;
}

/** Transmit a setpoint to the autopilot. */
static inline void _send_sp(const mavlink_set_position_target_local_ned_t *sp)
{
    mavlink_set_position_target_local_ned_t buf = *sp;
    mavlink_io_send_fixed_setpoint(autopilot_monitor_get_sysid(), VOXL_COMPID, buf);
}

/** Initialise the home (origin) hold setpoint. */
static void _init_home_sp(void)
{
    _make_hold_setpoint(&home_sp, 0.0f, 0.0f, FLIGHT_ALTITUDE, 0.0f);
}

/* ============================================================================
 * Mission thread
 * ========================================================================= */
static void *_square_thread_func(__attribute__((unused)) void *arg)
{
    int64_t next_time = 0;
    int     step      = 0;
    SquareState state = STATE_INIT;

    mavlink_set_position_target_local_ned_t sp;

    _init_home_sp();

    if (en_debug) {
        printf("=== Offboard Square Mission ===\n");
        printf("Side: %.4f m (%.1f ft)  |  Alt: %.2f m  |  Speed: %.1f m/s\n",
               SIDE_M, SIDE_M / 0.3048f, SQUARE_FLIGHT_ALT_M, CRUISE_SPEED_MS);
        printf("Steps/side: %d  |  Corner hold: %d steps  |  Takeoff hold: %d steps\n",
               STEPS_PER_SIDE, CORNER_HOLD_STEPS, TAKEOFF_HOLD_STEPS);
    }

    while (running) {

        int armed_offboard = autopilot_monitor_is_armed_and_in_offboard_mode();

        switch (state) {

        /* ------------------------------------------------------------------ */
        case STATE_INIT:
            /* Pre-warm the setpoint stream before the operator arms.
             * PX4 requires several setpoints to be received before it will
             * switch into offboard mode. */
            _send_sp(&home_sp);
            ++step;
            if (step >= 100) {
                state = STATE_WAIT_OFFBOARD;
                step  = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        case STATE_WAIT_OFFBOARD:
            /* Keep sending home setpoints.  Transition once armed + offboard. */
            _send_sp(&home_sp);
            if (armed_offboard) {
                if (en_debug) printf("[SQUARE] Offboard active – beginning takeoff hold.\n");
                state = STATE_TAKEOFF_HOLD;
                step  = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        case STATE_TAKEOFF_HOLD:
            /* Hold the altitude setpoint while PX4 climbs to cruise altitude. */
            if (!armed_offboard) { state = STATE_WAIT_OFFBOARD; step = 0; break; }
            _send_sp(&home_sp);
            ++step;
            if (step >= TAKEOFF_HOLD_STEPS) {
                if (en_debug) printf("[SQUARE] Takeoff hold complete – flying side 0.\n");
                state = STATE_FLY_SIDE_0;
                step  = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        /* Sides 0-3 share the same interpolation logic.
         * When we enter a FLY_SIDE_n state, step=0.
         * Interpolation: position walks linearly from start corner to end corner.
         * Velocity is constant in the direction of travel (feed-forward). */

        case STATE_FLY_SIDE_0:
        case STATE_FLY_SIDE_1:
        case STATE_FLY_SIDE_2:
        case STATE_FLY_SIDE_3: {
            if (!armed_offboard) { state = STATE_WAIT_OFFBOARD; step = 0; break; }

            /* Map state to side index 0-3.
             * Enum layout: FLY_0=3, HOLD_1=4, FLY_1=5, HOLD_2=6, FLY_2=7,
             *              HOLD_3=8, FLY_3=9  → FLY states are 2 apart, so divide by 2. */
            int side = ((int)state - (int)STATE_FLY_SIDE_0) / 2;
            int from = side;           /* corners[from] → corners[(from+1)%4] */
            int to   = (side + 1) % 4;

            float t  = (STEPS_PER_SIDE > 1)
                       ? (float)step / (float)(STEPS_PER_SIDE - 1)
                       : 1.0f;
            /* Clamp to [0,1] */
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            float px = corners[from][0] + t * (corners[to][0] - corners[from][0]);
            float py = corners[from][1] + t * (corners[to][1] - corners[from][1]);

            /* Taper velocity at beginning and end of each side so corners are
             * clean.  Within the middle 80 % of the side fly at full cruise
             * speed; ramp in/out over the first and last 10 %. */
            float speed_scale = 1.0f;
            if (t < 0.1f)        speed_scale = t / 0.1f;
            else if (t > 0.9f)   speed_scale = (1.0f - t) / 0.1f;

            float vx = sides[side].vx * speed_scale;
            float vy = sides[side].vy * speed_scale;

            _make_pv_setpoint(&sp, px, py, FLIGHT_ALTITUDE,
                              vx, vy, 0.0f, sides[side].yaw);
            _send_sp(&sp);

            ++step;
            if (step >= STEPS_PER_SIDE) {
                if (en_debug) {
                    printf("[SQUARE] Side %d complete – holding at corner %d.\n",
                           side, to);
                }
                /* Advance to the matching HOLD state */
                state = (SquareState)((int)STATE_HOLD_1 + side * 2);
                step  = 0;
            }
            break;
        }

        /* ------------------------------------------------------------------ */
        case STATE_HOLD_1:
        case STATE_HOLD_2:
        case STATE_HOLD_3: {
            if (!armed_offboard) { state = STATE_WAIT_OFFBOARD; step = 0; break; }

            /* Determine which corner we are holding at.
             * HOLD states: HOLD_1=4, HOLD_2=6, HOLD_3=8 (spaced 2 apart)
             * corner = (state - HOLD_1)/2 + 1  →  1,2,3 */
            int corner    = ((int)state - (int)STATE_HOLD_1) / 2 + 1;
            int next_side = corner; /* next side index (1, 2, or 3) */
            /* Yaw toward the NEXT side direction while holding */
            _make_hold_setpoint(&sp,
                                corners[corner][0], corners[corner][1],
                                FLIGHT_ALTITUDE,
                                sides[next_side].yaw);
            _send_sp(&sp);
            ++step;
            if (step >= CORNER_HOLD_STEPS) {
                if (en_debug) printf("[SQUARE] Corner %d hold complete – flying side %d.\n",
                                     corner, next_side);
                /* Enum layout (2 entries per side: FLY_SIDE_n, HOLD_n):
                 *   STATE_FLY_SIDE_1=5, STATE_FLY_SIDE_2=7, STATE_FLY_SIDE_3=9
                 * Each FLY_SIDE_n is 2 apart, so: FLY_SIDE_1 + (corner-1)*2 */
                state = (SquareState)((int)STATE_FLY_SIDE_1 + (corner - 1) * 2);
                step  = 0;
            }
            break;
        }

        /* ------------------------------------------------------------------ */
        case STATE_LAND_HOLD:
            /* Square is complete; hover at home before initiating descent. */
            if (!armed_offboard) { state = STATE_WAIT_OFFBOARD; step = 0; break; }
            _send_sp(&home_sp);
            ++step;
            if (step >= LAND_HOLD_STEPS) {
                if (en_debug) printf("[SQUARE] Landing hold complete – beginning descent.\n");
                state = STATE_DESCEND;
                step  = 0;
            }
            break;

        /* ------------------------------------------------------------------ */
        case STATE_DESCEND: {
            /* Linearly raise the NED z value (less negative → closer to ground).
             * vz is positive in NED (downward). */
            if (!armed_offboard) { state = STATE_WAIT_OFFBOARD; step = 0; break; }

            float progress = (float)step / (float)(DESCENT_STEPS - 1);
            if (progress > 1.0f) progress = 1.0f;

            float current_z = FLIGHT_ALTITUDE * (1.0f - progress); /* -1.5 → 0 */
            float vz_ned    = DESCENT_RATE_MS; /* positive = downward in NED */

            /* When very close to ground, send a pure position hold at z=0
             * so the drone settles cleanly. */
            if (current_z >= -0.08f) {
                _make_hold_setpoint(&sp, 0.0f, 0.0f, 0.05f, 0.0f);
            } else {
                _make_pv_setpoint(&sp, 0.0f, 0.0f, current_z,
                                  0.0f, 0.0f, vz_ned, 0.0f);
            }
            _send_sp(&sp);

            ++step;
            if (step >= DESCENT_STEPS) {
                if (en_debug) printf("[SQUARE] Descent complete.\n");
                state = STATE_DONE;
                step  = 0;
            }
            break;
        }

        /* ------------------------------------------------------------------ */
        case STATE_DONE:
            /* Hold at ground-level home setpoint and let PX4 handle disarm. */
            _make_hold_setpoint(&sp, 0.0f, 0.0f, 0.05f, 0.0f);
            _send_sp(&sp);
            /* Exit naturally – operator or PX4 will disarm. */
            running = 0;
            break;

        default:
            break;
        }

        if (my_loop_sleep(RATE, &next_time)) {
            fprintf(stderr, "WARNING [square]: control loop fell behind\n");
        }
    } /* end while(running) */

    printf("[SQUARE] Mission thread exiting.\n");
    return NULL;
}

/* NOTE -----------------------------------------------------------------------
 * State transition arithmetic (enum values are contiguous):
 *
 *   STATE_FLY_SIDE_0=3  STATE_HOLD_1=4   STATE_FLY_SIDE_1=5
 *   STATE_HOLD_2=6      STATE_FLY_SIDE_2=7  STATE_HOLD_3=8
 *   STATE_FLY_SIDE_3=9  STATE_LAND_HOLD=10  STATE_DESCEND=11  STATE_DONE=12
 *
 * Side index from FLY_SIDE_n state:
 *   side = (state - STATE_FLY_SIDE_0) / 2  → 0,1,2,3  ✓
 *   (integer divide by 2 because FLY states are spaced 2 apart in the enum)
 *
 * FLY_SIDE_n (side=0..3) → next hold:
 *   state = STATE_HOLD_1 + side*2  →  4,6,8,10 = HOLD_1,HOLD_2,HOLD_3,LAND_HOLD ✓
 *
 * HOLD_n (corner=1..3) → next fly side:
 *   state = STATE_FLY_SIDE_1 + (corner-1)*2  →  5,7,9 = FLY_1,FLY_2,FLY_3 ✓
 * --------------------------------------------------------------------------*/

/* ============================================================================
 * Public API
 * ========================================================================= */

int offboard_square_init(void)
{
    if (running) {
        fprintf(stderr, "[SQUARE] Already running.\n");
        return -1;
    }
    running = 1;
    pipe_pthread_create(&square_thread_id, _square_thread_func,
                        NULL, OFFBOARD_THREAD_PRIORITY);
    printf("[SQUARE] Mission thread started.\n");
    return 0;
}

int offboard_square_stop(int blocking)
{
    if (running == 0) return 0;
    running = 0;
    if (blocking) {
        pthread_join(square_thread_id, NULL);
        printf("[SQUARE] Mission thread stopped.\n");
    }
    return 0;
}

void offboard_square_en_print_debug(int debug)
{
    if (debug) en_debug = 1;
}
