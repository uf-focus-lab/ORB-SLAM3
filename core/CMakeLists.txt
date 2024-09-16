cmake_minimum_required(VERSION 3.5)
project(ORB_SLAM3)

# Set the build type. Options are:
LIST(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../scripts)
include(compiler)

# FindOpenGL prefers GLVND by default when available.
# Run "cmake --help-policy CMP0072" for policy details.
cmake_policy(SET CMP0072 NEW)

# System package manager managed packages
find_package(OpenCV 4.4 REQUIRED)
find_package(Eigen3 3.1.0 REQUIRED)

# Packages under "../install" directory
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
find_package(DBoW2 REQUIRED PATHS ${CMAKE_INSTALL_PREFIX})
find_package(g2o REQUIRED PATHS ${CMAKE_INSTALL_PREFIX})
find_package(Pangolin REQUIRED PATHS ${CMAKE_INSTALL_PREFIX})
find_package(Sophus REQUIRED PATHS ${CMAKE_INSTALL_PREFIX})

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${Pangolin_INCLUDE_DIRS}
    ${DBoW2_INCLUDE_DIRS}
)

link_libraries(
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

FILE(GLOB SOURCES src/*.cc src/**/*.cc)
FILE(GLOB HEADERS include/*.h include/**/*.h)
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Generate configuration file
configure_file(
  ${PROJECT_SOURCE_DIR}/../scripts/config.cmake.in
  "${PROJECT_BINARY_DIR}/ORB_SLAM3Config.cmake" @ONLY
)

# Install the library
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_PREFIX}/include/ORB_SLAM3)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ORB_SLAM3Config.cmake"
  DESTINATION ${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME})
