# VOXL Offboard Mission Programs

Source code for two offboard-control modules that run inside `voxl-vision-hub`
on the Starling 2 Max drone (VOXL 2 / QRB5165).  Both are modeled after the
existing `offboard_figure_eight.c` and use the same VOXL SDK APIs.

---

## Modules

### 1. `offboard_square` — Autonomous 6 ft × 6 ft Square Flight

Executes a fully autonomous mission:

```
Arm in offboard → Climb to altitude → Fly square → Return home → Descend → Land
```

| Parameter | Value | Description |
|-----------|-------|-------------|
| Side length | **1.8288 m** (6 ft) | One side of the square |
| Flight altitude | **1.5 m** | Above the home/arming point |
| Cruise speed | **0.8 m/s** | Horizontal speed per side |
| Loop rate | **30 Hz** | Setpoint frequency (same as figure-8) |
| Corner hold | **0.5 s** | Pause at each corner |
| Takeoff hold | **3.0 s** | Hover time before flying the square |
| Descent rate | **0.4 m/s** | Vertical landing speed |

#### Square geometry (MAV_FRAME_LOCAL_NED, x = North, y = East)

```
D (0, 6 ft) ─────── C (6 ft, 6 ft)
     │                        │
     │         ↑ North        │
     │                        │
Home (0, 0) ──────── B (6 ft, 0)
```

Flight order: **Home → B (North) → C (North+East) → D (East) → Home → Descend**

#### Safety behaviour
- If PX4 drops out of armed+offboard mode at **any** point during the mission,
  the module reverts to holding the home-position setpoint and waits until
  offboard mode is restored, then continues from where it left off.

---

### 2. `offboard_flight_recorder` — Record Manual Flights, Play Back Autonomously

#### Recording

While the drone is flying **manually** (any flight mode), start the recorder:

```c
offboard_recorder_start_recording();
// … fly the drone …
offboard_recorder_stop_and_save("/data/vio/recordings/flight_001.json");
```

The module captures odometry at **30 Hz**, stores
`{ timestamp, x, y, z, vx, vy, vz, yaw }` samples in RAM (up to 5 minutes),
and serialises to JSON on stop.

#### Playback

```c
offboard_recorder_start_playback("/data/vio/recordings/flight_001.json");
```

The module:
1. Loads the recording.
2. Pre-warms PX4's setpoint stream.
3. Waits for the operator to **arm the drone and enable offboard mode**.
4. Computes a **home offset** = (current position) − (recorded start position),
   so the trajectory is replayed relative to wherever you arm, not the
   original GPS coordinates.
5. Plays the recorded setpoints back at 30 Hz.
6. Holds the final position for 2 s, then exits.

If offboard mode is lost mid-playback, the module pauses at the current sample
and resumes once offboard is restored.

#### JSON recording format

```json
{
  "magic"       : "VOXL_FLIGHT_RECORDING",
  "version"     : 1,
  "date"        : "2026-03-03T10:00:00Z",
  "rate_hz"     : 30,
  "num_samples" : 900,
  "home_x"      : 0.0,
  "home_y"      : 0.0,
  "home_z"      : -1.5,
  "samples"     : [
      [t, x, y, z, vx, vy, vz, yaw],
      ...
  ]
}
```

All positions are in **MAV_FRAME_LOCAL_NED** (x = North, y = East, z = Down).

---

## Integration into voxl-vision-hub

These modules follow the exact same structure as `offboard_figure_eight.c`.
To add them to `voxl-vision-hub`:

1. **Copy** `src/offboard_square.c`, `src/offboard_square.h`,
   `src/offboard_flight_recorder.c`, and `src/offboard_flight_recorder.h`
   into `voxl-vision-hub/src/`.

2. **Add** the source files to the hub's `Makefile` or `CMakeLists.txt`:
   ```makefile
   SRCS += offboard_square.c offboard_flight_recorder.c
   ```

3. **Include** the headers in `voxl_vision_hub.c` (or wherever the other
   offboard modules are initialised):
   ```c
   #include "offboard_square.h"
   #include "offboard_flight_recorder.h"
   ```

4. **Wire up** the init/stop calls alongside the figure-8's calls, guarded by
   whatever config-file flag you add:
   ```c
   if (en_square_mission)   offboard_square_init();
   if (en_flight_recorder && playback_file[0])
       offboard_recorder_start_playback(playback_file);
   ```

---

## Building standalone (host PC, for testing stubs)

```bash
mkdir build && cd build
cmake .. -DBUILD_TEST_STUBS=ON
make
```

For a cross-compiled VOXL build, point at the VOXL SDK:

```bash
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/voxl-vision-hub/cmake/toolchain-qrb5165.cmake \
  -DVOXL_SDK_DIR=/path/to/voxl-sdk-sysroot
make
```

---

## On-drone quick-start checklist

| Step | Action |
|------|--------|
| 1 | Copy `.c`/`.h` files to VOXL via `scp` or direct copy |
| 2 | Rebuild `voxl-vision-hub` on the drone |
| 3 | Set `en_square_mission = 1` in the hub config (or add a toggle) |
| 4 | Place the drone at the desired home position |
| 5 | Start `voxl-vision-hub` |
| 6 | Arm the drone and switch to **Offboard** mode in QGroundControl |
| 7 | The drone climbs to 1.5 m, flies the square, and lands automatically |

> **Safety note:** Always have an RC transmitter ready to abort by switching
> out of offboard mode.  The module will hold position immediately when
> offboard mode is exited.

---

## Coordinate frame reference

```
NED Local Frame (MAV_FRAME_LOCAL_NED)
  x → North  (positive = forward / north)
  y → East   (positive = right / east)
  z → Down   (negative = up; FLIGHT_ALTITUDE = -1.5)

Yaw (NED convention)
  0       = North
  +PI/2   = East
  ±PI     = South
  -PI/2   = West
```

---

## File structure

```
voxl-programs/
├── CMakeLists.txt
├── README.md
└── src/
    ├── offboard_square.h               ← Public API
    ├── offboard_square.c               ← Square mission implementation
    ├── offboard_flight_recorder.h      ← Public API
    └── offboard_flight_recorder.c      ← Recorder + playback implementation
```
