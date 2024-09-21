#pragma once

namespace ORB_SLAM3 {

typedef enum {

  USE_IMU = 0x80,
  CAMERA_MASK = 0x0F,

  MONOCULAR = 0x01,
  STEREO = 0x02,
  RGB_D = 0x03,

  IMU_MONOCULAR = USE_IMU | MONOCULAR,
  IMU_STEREO = USE_IMU | STEREO,
  IMU_RGB_D = USE_IMU | RGB_D

} SensorType;

} // namespace ORB_SLAM3
