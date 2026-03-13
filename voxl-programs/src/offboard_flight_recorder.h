/*******************************************************************************
 * offboard_flight_recorder.h
 *
 * Manual-flight recorder and autonomous-playback module for the
 * Starling 2 Max / VOXL 2.
 *
 * Overview
 * --------
 *  RECORD MODE
 *    While the drone is being flown manually (any mode), this module samples
 *    the autopilot's local-NED odometry at RECORDER_RATE_HZ and stores
 *    timestamped { x, y, z, vx, vy, vz, yaw } snapshots in a ring buffer
 *    (up to RECORDER_MAX_MINUTES minutes).  Calling
 *    offboard_recorder_stop_and_save() serialises the buffer to a JSON file.
 *
 *  PLAYBACK MODE
 *    Given a previously saved JSON recording, this module waits for the
 *    autopilot to be armed in offboard mode and then feeds the recorded
 *    position+velocity setpoints back to PX4 at the original sample rate.
 *    If the autopilot drops out of offboard mode mid-flight the playback
 *    pauses at the current sample index and resumes when offboard is restored.
 *    At the end of the recording the module holds the final position and exits.
 *
 *  JSON RECORDING FORMAT
 *  ---------------------
 *    {
 *      "magic"       : "VOXL_FLIGHT_RECORDING",
 *      "version"     : 1,
 *      "date"        : "<ISO-8601>",
 *      "rate_hz"     : 30,
 *      "num_samples" : <N>,
 *      "samples"     : [
 *          [t, x, y, z, vx, vy, vz, yaw],
 *          ...
 *      ]
 *    }
 *
 *    All positions are in MAV_FRAME_LOCAL_NED (x=North, y=East, z=Down).
 *    At playback time the home offset stored in the JSON is evaluated against
 *    the drone's current position so the trajectory is always home-relative.
 ******************************************************************************/

#ifndef OFFBOARD_FLIGHT_RECORDER_H
#define OFFBOARD_FLIGHT_RECORDER_H

/* ============================================================================
 * Tuning – may be overridden before including this header.
 * ========================================================================= */
#ifndef RECORDER_RATE_HZ
#define RECORDER_RATE_HZ        30          /**< Sample / playback rate [Hz] */
#endif

#ifndef RECORDER_MAX_MINUTES
#define RECORDER_MAX_MINUTES    5           /**< Max recording length [min]  */
#endif

/* ============================================================================
 * Recording API
 * ========================================================================= */

/**
 * @brief  Start capturing odometry into the internal ring buffer.
 *
 * The recording thread samples the autopilot at RECORDER_RATE_HZ regardless
 * of flight mode.  You do not need to be in offboard mode to record.
 *
 * @return  0 on success, -1 if already recording or buffer could not be
 *          allocated.
 */
int offboard_recorder_start_recording(void);

/**
 * @brief  Stop the recording thread and serialise the buffer to a JSON file.
 *
 * @param save_path  Absolute or relative path for the output file, e.g.
 *                   "/data/vio/recordings/flight_001.json".  The directory
 *                   must already exist.
 * @return  Number of samples written on success, -1 on error.
 */
int offboard_recorder_stop_and_save(const char *save_path);

/**
 * @brief  Query whether a recording is currently active.
 * @return  Non-zero if recording is in progress.
 */
int offboard_recorder_is_recording(void);

/**
 * @brief  Return the number of samples captured so far in the current
 *         recording (or 0 if not recording).
 */
int offboard_recorder_sample_count(void);

/* ============================================================================
 * Playback API
 * ========================================================================= */

/**
 * @brief  Load a previously saved recording and start autonomous playback.
 *
 * This function reads @p file_path, pre-warms the PX4 setpoint stream, then
 * waits for the autopilot to be armed in offboard mode before beginning
 * playback.  The home position at the time playback begins is used as the
 * origin so the trajectory is relative (not absolute).
 *
 * @param file_path  Path to the JSON recording file.
 * @return  0 on success (thread started), -1 on file/parse error.
 */
int offboard_recorder_start_playback(const char *file_path);

/**
 * @brief  Stop playback (or recording if still in progress) immediately.
 *
 * @param blocking  Non-zero to block until the thread has fully exited.
 * @return  0 on success.
 */
int offboard_recorder_stop(int blocking);

/**
 * @brief  Query whether playback is currently active.
 * @return  Non-zero if playback is in progress.
 */
int offboard_recorder_is_playing(void);

/* ============================================================================
 * Debug
 * ========================================================================= */

/**
 * @brief  Enable verbose debug output to stdout.
 * @param debug  Non-zero to enable.
 */
void offboard_recorder_en_print_debug(int debug);

#endif /* OFFBOARD_FLIGHT_RECORDER_H */
