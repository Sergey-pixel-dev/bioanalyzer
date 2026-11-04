cmake_minimum_required(VERSION 3.16)
project(biosignal_analyzer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without a build type CMake passes no optimization at all, and filtering and
# the quality checks run several times slower. Release with -O2 is the
# default; a debug build is still -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

# One static library per functional module makes inter-module coupling
# visible in target_link_libraries() and matches the architecture described
# in the design docs (see Output/Расчётно-пояснительная записка.typ, гл. 2).

# Sources are UTF-8; MSVC otherwise reads them in the system code page.
if(MSVC)
    add_compile_options(/utf-8 /Zc:__cplusplus)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Concurrent LinguistTools)
# Eigen is header-only: without its CMake package (a plain unpacked archive,
# as on Windows) the headers are taken from EIGEN3_INCLUDE_DIR.
find_package(Eigen3 QUIET NO_MODULE)
if(NOT TARGET Eigen3::Eigen)
    find_path(EIGEN3_INCLUDE_DIR Eigen/Core PATH_SUFFIXES eigen3)
    if(NOT EIGEN3_INCLUDE_DIR)
        message(FATAL_ERROR "Eigen not found: set EIGEN3_INCLUDE_DIR to the folder that contains Eigen/Core")
    endif()
    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}")
endif()

# Vendored QCustomPlot as its own target so the page forms and any future
# consumer can link it without duplicating source lookups.
add_library(qcustomplot STATIC ${CMAKE_SOURCE_DIR}/third_party/qcustomplot/qcustomplot.cpp)
target_include_directories(qcustomplot PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)

# Convenience: bsa_module(<name>) declares a STATIC library gathering the
# module's own translation units. All modules use `include/` as their public
# include root so headers keep the "<module>/<file>.h" form.
function(bsa_module NAME)
    file(GLOB SOURCES CONFIGURE_DEPENDS
         src/${NAME}/*.cpp
         include/${NAME}/*.h)
    add_library(${NAME} STATIC ${SOURCES})
    target_include_directories(${NAME} PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party)
endfunction()

# Bottom of the dependency graph: pure C++ modules with no Qt dependency.
bsa_module(exchange)
bsa_module(datahub)

# Device management pulls in the transport stack and publishes into datahub.
# The domain modules hold no forms and do not link Qt Widgets: the forms of
# every page live in navigation, which creates them and the application
# context they use.
bsa_module(device)
target_link_libraries(device PUBLIC exchange datahub Qt6::Core Qt6::Concurrent)
if(WIN32)
    # The serial ports are listed from the registry.
    target_link_libraries(device PUBLIC advapi32)
endif()

# Notifications only needs Qt Widgets (for QMessageBox / QLabel).
bsa_module(notifications)
target_link_libraries(notifications PUBLIC Qt6::Widgets)

# Monitoring: signal processing and trial quality measures, plain C++.
# Consumes the sample blocks of datahub.
bsa_module(monitoring)
target_link_libraries(monitoring PUBLIC datahub Eigen3::Eigen)

# AI: datasets, data collection, training and inference sessions, the worker
# process. Depends on device sessions for the stream parameters, on DataHub
# for sample delivery and on the monitoring quality measures and filters.
bsa_module(ai)
target_link_libraries(ai PUBLIC datahub device monitoring Qt6::Core)

# Settings: theme of the application.
bsa_module(settings)
target_link_libraries(settings PUBLIC Qt6::Widgets)

# Navigation is the user interface package: main window, presentation model,
# application context and the forms of all pages.
bsa_module(navigation)
target_link_libraries(navigation PUBLIC
    monitoring ai device notifications settings qcustomplot Qt6::Widgets)

file(GLOB UI_FILES CONFIGURE_DEPENDS ui/*.ui)

add_executable(${PROJECT_NAME} src/main.cpp ${UI_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE navigation)
if(WIN32)
    # A window application without a console; the manifest makes UTF-8 the
    # code page of narrow strings, so the Qt-free core opens files in folders
    # with Cyrillic names (Windows 10 1903 and later).
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_sources(${PROJECT_NAME} PRIVATE win/biosignal.manifest)
    else()
        target_sources(${PROJECT_NAME} PRIVATE win/biosignal.rc)
    endif()
endif()

# Translation. lupdate collects every tr() of all modules and every string of
# the forms into one table (translations/*.ts, target update_translations);
# lrelease compiles it into a .qm embedded as :/i18n/biosignal_<lang>.qm.
# The language is chosen in Settings and applied at the next start.
qt_add_translations(TARGETS ${PROJECT_NAME}
    SOURCE_TARGETS ${PROJECT_NAME} exchange datahub device monitoring ai notifications settings navigation
    TS_FILES translations/biosignal_ru.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none)

# Runtime descriptors are data files consumed from the application directory.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/tasks
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/tasks $<TARGET_FILE_DIR:${PROJECT_NAME}>/tasks)

# Python worker changes often and doesn't need a C++ rebuild: link, don't copy.
# Symbolic links on Windows need developer mode, so there it is copied.
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/ai_worker $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker)
else()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker
        COMMAND ${CMAKE_COMMAND} -E create_symlink
                ${CMAKE_SOURCE_DIR}/ai_worker $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker)
endif()
