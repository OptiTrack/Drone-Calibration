# Qt Drone UI - Modal AI Starling 2 Max Interface

A comprehensive Qt-based C++ user interface for controlling the Modal AI Starling 2 Max drone via VOXL 2, with support for Software in the Loop (SIL) simulation.

## Features

### 🎥 Camera System
- Live camera feed from VOXL 2
- Video recording capabilities
- Camera settings control (quality, format, framerate)
- Demo mode for testing without hardware
- Fullscreen viewing mode

### 🗺️ 3D Path Planner
- OpenGL-based 3D waypoint visualization
- Interactive waypoint placement and editing
- Real-time path preview with distance calculations
- Multiple coordinate systems (NED, ENU, Aircraft)
- Path animation and playback
- Export/import flight paths

### 📁 Flight Path Management
- Save and load custom flight paths
- Path library with metadata
- Waypoint details and editing
- Path optimization and validation
- JSON-based path storage

### 🎬 Recording Management
- Video recording library
- Playback integration
- Storage usage monitoring
- Import/export capabilities
- Thumbnail generation

### 📊 Drone Status & Control
- Real-time telemetry display
- Battery monitoring with visual indicators
- Flight mode control
- GPS status and positioning
- Attitude (roll, pitch, yaw) display
- System messages and logging

### 🚁 Flight Controls
- Arm/disarm functionality
- Takeoff and landing commands
- Return to launch (RTL)
- Emergency stop
- Manual control inputs
- Mission upload and execution

### 🔗 VOXL 2 Integration
- TCP/UDP/WebSocket communication
- MAVLink protocol support
- Real-time data streaming
- Camera control interface
- Heartbeat monitoring

## System Requirements

- Windows 10/11 or Linux
- Qt 6.0 or later
- OpenGL 3.3 support
- CMake 3.20+
- C++17 compatible compiler
- Network access to VOXL 2 (typically 192.168.1.10)

## Quick Start

### Building the Application

1. **Prerequisites**
   ```bash
   # Install Qt 6 (including OpenGL module)
   # Install CMake 3.20+
   # Install a C++17 compiler (GCC, MSVC, or Clang)
   ```

2. **Clone and Build**
   ```bash
   git clone <repository-url>
   cd qt-drone-ui
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build .
   ```

3. **Windows Quick Build**
   ```cmd
   # Run the provided build script
   build.bat
   ```

### First Run

1. Launch the application
2. The interface starts in **simulation mode** by default
3. Navigate through different sections using the sidebar:
   - **Home**: Camera feed and overview
   - **Camera Feed**: Live video and recording
   - **Path Planner**: 3D waypoint editor
   - **Recorded Paths**: Flight path library
   - **Recorded Videos**: Video library
   - **Drone Status**: Telemetry and controls

### Connecting to VOXL 2

1. Ensure your VOXL 2 is connected to the network
2. In the **Drone Status** tab, the system will attempt to connect to `192.168.1.10:14550`
3. For Software in the Loop (SIL), ensure your simulation is running on the correct port
4. Connection status is displayed in the status bar and drone status widget

## Usage Guide

### Creating Flight Paths

1. Go to **Path Planner**
2. Click **Add Waypoint** or **Ctrl+Click** in the 3D view
3. Edit waypoint positions using the spinboxes
4. **Save Path** to add to your library
5. Use **Play Path** to preview the flight route

### Recording Video

1. Go to **Camera Feed**
2. Select feed source (Demo, Live Camera, or VOXL)
3. Configure recording settings
4. Click **Start Recording**
5. Recordings are automatically saved to your Videos folder

### Flight Operations

1. Go to **Drone Status**
2. Verify connection and GPS lock
3. Select appropriate flight mode
4. **ARM** the drone when ready
5. Use **TAKEOFF** for autonomous takeoff
6. Monitor telemetry during flight
7. Use **LAND** or **RTL** to safely return

## Configuration

### VOXL 2 Connection Settings
- Default IP: `192.168.1.10`
- Default Port: `14550`
- Connection Type: TCP (configurable)
- Heartbeat Interval: 1 second
- Status Update Rate: 10 Hz

### Software in the Loop (SIL)
- SIL Host: `127.0.0.1`
- SIL Port: `14550`
- Automatic fallback to simulation mode
- Real-time telemetry simulation

## Architecture

### Core Components

- **MainWindow**: Main interface with navigation
- **CameraFeedWidget**: Video streaming and recording
- **PathPlannerWidget**: 3D OpenGL waypoint editor
- **DroneStatusWidget**: Telemetry display and controls
- **DroneController**: Communication and flight control
- **VOXLConnection**: Network interface to VOXL 2

### Data Models

- **Waypoint**: Individual waypoint with metadata
- **FlightPath**: Collection of waypoints with path info
- **Recording**: Video/photo recording metadata
- **DroneStatus**: Real-time telemetry data

### Communication

- **TCP/UDP**: Direct VOXL 2 communication
- **WebSocket**: Alternative connection method
- **HTTP REST**: Configuration and file operations
- **MAVLink**: Standard drone protocol messages

## Troubleshooting

### Connection Issues
- Verify VOXL 2 IP address and network connectivity
- Check firewall settings (ports 14550, 8080)
- Try different connection types (TCP/UDP/WebSocket)
- Enable simulation mode for offline testing

### Camera Problems
- Verify VOXL 2 camera service is running
- Check camera permissions and drivers
- Try demo mode for interface testing
- Verify network bandwidth for streaming

### Performance Issues
- Update graphics drivers for OpenGL support
- Reduce camera quality/framerate
- Close unnecessary applications
- Check system resources (CPU, memory)

## Development

### Code Structure
```
src/
├── main.cpp                 # Application entry point
├── mainwindow.{h,cpp}       # Main application window
├── widgets/                 # UI widgets
│   ├── camerafeedwidget.*
│   ├── pathplannerwidget.*
│   ├── recordedpathswidget.*
│   ├── recordedvideoswidget.*
│   └── dronestatuswidget.*
├── controllers/             # Business logic
│   └── dronecontroller.*
├── network/                 # Communication
│   └── voxlconnection.*
├── models/                  # Data structures
│   ├── waypoint.*
│   ├── flightpath.*
│   └── recording.*
└── utils/                   # Utilities
    └── settings.*
```

### Adding Features

1. Create new widgets in `src/widgets/`
2. Add to main window navigation
3. Implement data models in `src/models/`
4. Extend `DroneController` for new flight functions
5. Update `VOXLConnection` for new VOXL 2 features

## License

This project is designed for use with Modal AI Starling 2 Max drones and VOXL 2 flight computers.

## Support

For issues related to:
- **Qt Interface**: Check Qt documentation and this repository
- **VOXL 2 Integration**: Consult Modal AI documentation
- **MAVLink Protocol**: Reference MAVLink specification
- **Hardware Issues**: Contact Modal AI support

---

**Note**: This interface is designed for experienced drone operators. Always follow safety protocols and local regulations when operating drones.