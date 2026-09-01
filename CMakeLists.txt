cmake_minimum_required(VERSION 3.16)
project(app VERSION 1.0 LANGUAGES CXX)

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
    ${CMAKE_SOURCE_DIR}/third_party/qcustomplot
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::Concurrent
    Eigen3::Eigen
)

include(CTest)
if(BUILD_TESTING)
    add_executable(format_tests
        tests/test_new_formats.cpp
        src/core/dataset.cpp
        src/core/aimodel.cpp
        src/core/rawrecord.cpp)
    target_include_directories(format_tests PRIVATE include)
    add_test(NAME format_tests COMMAND format_tests)

    add_executable(protocol_tests
        tests/test_transport.cpp
        src/core/transportprotocol.cpp
        src/core/applicationprotocol.cpp
        src/core/ecgadcprotocol.cpp)
    target_include_directories(protocol_tests PRIVATE include)
    add_test(NAME protocol_tests COMMAND protocol_tests)
endif()
