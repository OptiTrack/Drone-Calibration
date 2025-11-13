cmake_minimum_required(VERSION 3.16)

project(sports-data-metrics-client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui OpenGL OpenGLWidgets Widgets PrintSupport)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        src/rendering/glwidget.cpp
        src/rendering/glwidget.h
        src/rendering/mesh.cpp
        src/rendering/mesh.h
        src/rendering/meshGenerator.cpp
        src/rendering/meshGenerator.h
        src/connection/connection_controller.cpp
        src/connection/connection_controller.h
        src/connection/natnet_connection.cpp
        src/connection/natnet_connection.h
        src/connection/natnet/NatNetCAPI.h
        src/connection/natnet/NatNetCLient.h
        src/connection/natnet/NatNetTypes.h
        src/data/frame_data.h
        src/data/metrics_data.h
        src/data/data_processor.cpp
        src/data/data_processor.h
        src/data/skeleton_metrics.cpp
        src/data/skeleton_metrics.h
        src/data/rigid_body_metrics.cpp
        src/data/rigid_body_metrics.h
        src/data/replay_controller.cpp
        src/data/replay_controller.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(sports-data-metrics-client
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        resource.qrc
        src/controllers/toggles.cpp
        src/controllers/toggles.h
        src/controllers/metricscontroller.h
        src/controllers/metricscontroller.cpp
        src/controllers/streamingcontroller.h
        src/controllers/streamingcontroller.cpp
        src/controllers/metricsmanager.h
        src/controllers/metricsmanager.cpp
        src/controllers/uifactory.h
        src/controllers/uifactory.cpp
        src/config/sports.json
        src/utils/fileutils.cpp
        src/utils/fileutils.h
        src/widgets/graphwidget.cpp
        src/widgets/graphwidget.h
        src/controllers/reportgenerator.h 
        src/controllers/reportgenerator.cpp
        src/controllers/settings.h
        src/utils/uiutils.h
        src/utils/uiutils.h src/utils/uiutils.cpp
        src/controllers/configurecontroller.h
        src/controllers/configurecontroller.cpp
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET sports-data-metrics-client APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(sports-data-metrics-client SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(sports-data-metrics-client
            ${PROJECT_SOURCES}
        )
    endif()
endif()


target_include_directories(sports-data-metrics-client
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/data
        ${PROJECT_SOURCE_DIR}/src/connection
        ${PROJECT_SOURCE_DIR}/src/connection/natnet
        ${PROJECT_SOURCE_DIR}/src/rendering
        ${PROJECT_SOURCE_DIR}/src/controllers
)

link_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Set modules to link against
target_link_libraries(sports-data-metrics-client
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
            Qt${QT_VERSION_MAJOR}::OpenGLWidgets
            Qt${QT_VERSION_MAJOR}::Gui
            Qt${QT_VERSION_MAJOR}::OpenGL
            Qt${QT_VERSION_MAJOR}::PrintSupport
            ${CMAKE_CURRENT_SOURCE_DIR}/src/connection/natnet/NatNetLib.lib
            OpenGL32.lib
)


# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.sports-data-metrics-client)
endif()
set_target_properties(sports-data-metrics-client PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS sports-data-metrics-client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(sports-data-metrics-client)
endif()

set(TEXTURE_FILES
    "src/rendering/assets/texture/cube.png"
)

qt6_add_resources(SHADER_RES src/rendering/shaders.qrc)
target_sources(sports-data-metrics-client PRIVATE ${SHADER_RES})
