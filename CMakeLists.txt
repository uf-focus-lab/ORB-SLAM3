cmake_minimum_required(VERSION 2.8)
project(ORB_SLAM3)

# Set the build type. Options are:
LIST(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/scripts)
include(compiler)

# System package manager managed packages
find_package(OpenCV 4.4 REQUIRED)
find_package(Eigen3 3.1.0 REQUIRED)
find_package(realsense2)

# Packages under "thirdparty" directory
find_package(DBoW2 REQUIRED PATHS ${PROJECT_SOURCE_DIR}/lib/thirdparty)
find_package(g2o REQUIRED PATHS ${PROJECT_SOURCE_DIR}/lib/thirdparty)
find_package(Pangolin REQUIRED PATHS ${PROJECT_SOURCE_DIR}/lib/thirdparty)
find_package(Sophus REQUIRED PATHS ${PROJECT_SOURCE_DIR}/lib/thirdparty)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/CameraModels
    ${PROJECT_SOURCE_DIR}/thirdparty/include
    ${Pangolin_INCLUDE_DIRS}
    ${DBoW2_INCLUDE_DIRS}
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)

FILE(GLOB SOURCES src/*.cc src/**/*.cc)
FILE(GLOB HEADERS include/*.h include/**/*.h)
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    -lboost_serialization
    -lcrypto
    ${OpenCV_LIBS}
    ${EIGEN3_LIBS}
    ${Pangolin_LIBRARIES}
    ${DBoW2_LIBRARIES}
    Sophus::Sophus
    g2o::core
    g2o::stuff
    g2o::opengl_helper
    g2o::g2o_cli_library
    g2o::g2o_simulator_library
    g2o::types_slam2d
    g2o::types_slam2d_addons
    g2o::types_sclam2d
    g2o::types_slam3d
    g2o::types_slam3d_addons
    g2o::types_sba
    g2o::types_icp
    g2o::types_sim3
    g2o::solver_pcg
    g2o::solver_dense
    g2o::solver_slam2d_linear
    g2o::solver_structure_only
    g2o::solver_eigen
    g2o::g2o_hierarchical_library
)

# If RealSense SDK is found the library is added and its examples compiled
if(realsense2_FOUND)
    include_directories(${PROJECT_NAME}
    ${realsense_INCLUDE_DIR}
    )
    target_link_libraries(${PROJECT_NAME}
    ${realsense2_LIBRARY}
    )
endif()

# Include Examples and Tests Targets
LIST(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/examples)
include(examples)
