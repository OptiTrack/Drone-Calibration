cmake_minimum_required(VERSION 3.20)

project(QtDroneUI VERSION 1.0 LANGUAGES CXX)

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/widgets/camerafeedwidget.h
    src/widgets/pathplannerwidget.h
    src/widgets/recordedpathswidget.h
    src/widgets/recordedvideoswidget.h
    src/widgets/dronestatuswidget.h
    src/controllers/dronecontroller.h
    src/models/waypoint.h
    src/models/flightpath.h
    src/models/recording.h
    src/network/voxlconnection.h
    src/utils/settings.h
)

# Source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/widgets/camerafeedwidget.cpp
    src/widgets/pathplannerwidget.cpp
    src/widgets/recordedpathswidget.cpp
    src/widgets/recordedvideoswidget.cpp
    src/widgets/dronestatuswidget.cpp
    src/controllers/dronecontroller.cpp
    src/models/waypoint.cpp
    src/models/flightpath.cpp
    src/models/recording.cpp
    src/network/voxlconnection.cpp
    src/utils/settings.cpp
)

set(PROJECT_SOURCES
    ${HEADERS}
    ${SOURCES}
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(QtDroneUI
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    add_executable(QtDroneUI ${PROJECT_SOURCES})
    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

# Link libraries
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

# Set target properties
set_target_properties(QtDroneUI PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.optitrakc.qtdroneui
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Include directories
target_include_directories(QtDroneUI PRIVATE src)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_finalize_executable(QtDroneUI)
endif()