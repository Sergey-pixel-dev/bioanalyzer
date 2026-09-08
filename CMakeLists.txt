cmake_minimum_required(VERSION 3.16)
project(biosignal_analyzer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/ui)

# QCustomPlot needs Gui/Widgets/PrintSupport; the FFT/DSP layer uses Eigen.
find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Concurrent)
find_package(Eigen3 REQUIRED NO_MODULE)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS include/*.h)
file(GLOB_RECURSE UI_FILES CONFIGURE_DEPENDS ui/*.ui)

# Vendored QCustomPlot (single .h/.cpp pair under third_party/).
set(QCUSTOMPLOT_SOURCES
    ${CMAKE_SOURCE_DIR}/third_party/qcustomplot/qcustomplot.cpp)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${QCUSTOMPLOT_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/qcustomplot
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Concurrent
    Eigen3::Eigen
)

# Runtime descriptors are data files consumed from the application directory.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/tasks $<TARGET_FILE_DIR:${PROJECT_NAME}>/tasks)

# Python worker changes often and doesn't need a C++ rebuild: link, don't copy.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_SOURCE_DIR}/ai_worker $<TARGET_FILE_DIR:${PROJECT_NAME}>/ai_worker)
