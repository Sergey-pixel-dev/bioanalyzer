cmake_minimum_required(VERSION 3.16)
project(biosignal_analyzer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

# One static library per functional module makes inter-module coupling
# visible in target_link_libraries() and matches the architecture described
# in the design docs (see Output/Расчётно-пояснительная записка.typ, гл. 2).

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Concurrent)
find_package(Eigen3 REQUIRED NO_MODULE)

# Vendored QCustomPlot as its own target so both the monitoring module and
# any future consumer can link it without duplicating source lookups.
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
bsa_module(device)
target_link_libraries(device PUBLIC exchange datahub Qt6::Widgets Qt6::Concurrent)

# Notifications only needs Qt Widgets (for QMessageBox / QLabel).
bsa_module(notifications)
target_link_libraries(notifications PUBLIC Qt6::Widgets)

# Monitoring: DSP + visualization page. Consumes DataHub via device sessions.
bsa_module(monitoring)
target_link_libraries(monitoring PUBLIC
    datahub device notifications qcustomplot Eigen3::Eigen
    Qt6::Widgets Qt6::PrintSupport)

# AI: data collection, training, inference. Depends on device sessions for
# stream ownership and on DataHub for sample delivery.
bsa_module(ai)
target_link_libraries(ai PUBLIC
    datahub device notifications
    Qt6::Widgets Qt6::Concurrent)

# Settings: theme and (later) application-scope preferences.
bsa_module(settings)
target_link_libraries(settings PUBLIC Qt6::Widgets)

# Navigation binds everything together (main window + presentation model
# + application context).
bsa_module(navigation)
target_link_libraries(navigation PUBLIC
    monitoring ai device notifications settings Qt6::Widgets)

file(GLOB UI_FILES CONFIGURE_DEPENDS ui/*.ui)

add_executable(${PROJECT_NAME} src/main.cpp ${UI_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE navigation)

# Runtime descriptors are data files consumed from the application directory.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/tasks
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/tasks $<TARGET_FILE_DIR:${PROJECT_NAME}>/tasks)

# Python worker changes often and doesn't need a C++ rebuild: link, don't copy.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_SOURCE_DIR}/ai_worker $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker)
