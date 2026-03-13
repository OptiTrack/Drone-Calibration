/*******************************************************************************
 * offboard_square.h
 *
 * Offboard square flight program for the Starling 2 Max / VOXL 2.
 *
 * This module commands the drone to:
 *   1. Enter and hold at the home position after arming in offboard mode.
 *   2. Fly a 6 ft × 6 ft (1.8288 m × 1.8288 m) square at cruise altitude.
 *   3. Return to the home position and descend to land.
 *
 * Usage
 * -----
 *  Call offboard_square_init() to start the background thread.
 *  Call offboard_square_stop(1) to stop it and wait for the thread to exit.
 *
 * The thread automatically handles re-entry into offboard mode if PX4 drops
 * out; it waits at the home setpoint until armed + offboard is restored.
 *
 * Coordinate frame
 * ----------------
 *  All positions use MAV_FRAME_LOCAL_NED (x = North, y = East, z = Down).
 *  The home origin (0, 0) is wherever the drone is when it first arms in
 *  offboard mode.  Altitude is configured via SQUARE_FLIGHT_ALT_M in
 *  offboard_square.c.
 ******************************************************************************/

#ifndef OFFBOARD_SQUARE_H
#define OFFBOARD_SQUARE_H

/**
 * @brief  Start the offboard square mission thread.
 *
 * Spawns a background pthread that pre-warms the setpoint stream, waits for
 * the autopilot to be armed in offboard mode, runs the square trajectory, and
 * then descends to land.
 *
 * @return  0 on success, -1 on failure.
 */
int offboard_square_init(void);

/**
 * @brief  Stop the offboard square mission thread.
 *
 * @param blocking  If non-zero, block until the thread has fully exited.
 * @return  0 on success.
 */
int offboard_square_stop(int blocking);

/**
 * @brief  Enable verbose debug output (trajectory printouts to stdout).
 *
 * @param debug  Non-zero to enable.
 */
void offboard_square_en_print_debug(int debug);

#endif /* OFFBOARD_SQUARE_H */
