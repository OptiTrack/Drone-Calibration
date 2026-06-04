cmake_minimum_required(VERSION 3.20)

project(QtDroneUI VERSION 1.0 LANGUAGES CXX)

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suppress Qt Vulkan feature warnings (we use OpenGL instead)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "Suppress developer warnings" FORCE)
set(QT_FEATURE_vulkan OFF CACHE BOOL "Disable Vulkan" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan TRUE)
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders TRUE)

# Find required Qt modules 
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS 
    Core 
    Widgets 
    OpenGL 
    OpenGLWidgets
    Network 
    Multimedia 
    MultimediaWidgets
    LinguistTools
)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS 
    Core 
    Widgets 
    OpenGL 
    OpenGLWidgets
    Network 
    Multimedia 
    MultimediaWidgets
    LinguistTools
)

# Find OpenGL
find_package(OpenGL REQUIRED)



# Header files
set(HEADERS
    src/mainwindow.h
    src/widgets/dashboardwidget.h
    src/widgets/camerafeedwidget.h
    src/widgets/pathplannerwidget.h
    src/widgets/recordedpathswidget.h
    src/widgets/recordedvideoswidget.h
    src/widgets/dronestatuswidget.h
    src/controllers/dronecontroller.h
    src/models/waypoint.h
    src/models/trajectory.h
    src/models/flightpath.h
    src/models/recording.h
    src/network/voxlconnection.h
    src/network/voxlmapperclient.h
    src/utils/settings.h
    src/utils/flightlogger.h
    src/utils/volumemanager.h
    src/utils/voxlmappaths.h
    src/utils/pathfitter.h
)

# Source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/widgets/dashboardwidget.cpp
    src/widgets/camerafeedwidget.cpp
    src/widgets/pathplannerwidget.cpp
    src/widgets/recordedpathswidget.cpp
    src/widgets/recordedvideoswidget.cpp
    src/widgets/dronestatuswidget.cpp
    src/controllers/dronecontroller.cpp
    src/models/waypoint.cpp
    src/models/trajectory.cpp
    src/models/flightpath.cpp
    src/models/recording.cpp
    src/network/voxlconnection.cpp
    src/network/voxlmapperclient.cpp
    src/utils/settings.cpp
    src/utils/flightlogger.cpp
    src/utils/volumemanager.cpp
    src/utils/voxlmappaths.cpp
    src/utils/pathfitter.cpp
)

# UI files
set(UI_FILES
    src/widgets/dashboardwidget.ui
    src/widgets/dronestatuswidget.ui
    src/widgets/recordedpathswidget.ui
    src/widgets/recordedvideoswidget.ui
)

set(PROJECT_SOURCES
    ${HEADERS}
    ${SOURCES}
    ${UI_FILES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(QtDroneUI
        MANUAL_FINALIZATION
        WIN32
        ${PROJECT_SOURCES}
    )
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    add_executable(QtDroneUI WIN32 ${PROJECT_SOURCES})
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

# Link libraries (skip Qt6EntryPoint for MinGW compatibility)
target_link_libraries(QtDroneUI PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::OpenGL
    Qt${QT_VERSION_MAJOR}::OpenGLWidgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::MultimediaWidgets
    ${OPENGL_LIBRARIES}
)

# Workaround for MinGW linking issue with Qt6 EntryPoint
if(MINGW AND ${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    # Don't link Qt6EntryPoint - we provide our own WinMain
    set_target_properties(QtDroneUI PROPERTIES QT_NO_ENTRYPOINT ON)
endif()

# Set target properties
set_target_properties(QtDroneUI PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.optitrakc.qtdroneui
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
)

# Include directories
target_include_directories(QtDroneUI PRIVATE src)

# Define the source directory path for finding the paths folder during development
target_compile_definitions(QtDroneUI PRIVATE 
    SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_finalize_executable(QtDroneUI)
endif()