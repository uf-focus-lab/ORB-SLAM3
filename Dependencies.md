##List of Known Dependencies
###ORB-SLAM3 v1.0

In this document we list all the pieces of code included  by ORB-SLAM3 and linked libraries which are not property of the authors of ORB-SLAM3.

### External Libraries

| Package Name (Debian/Ubuntu) | Purpose |
|:----------------------------:|:--------|
| `libssl-dev`                 | MD5 Checksum |
| `libopencv-dev`              | Image Processing, FAST Feature Extraction |
| `libepoxy-dev`               | TBD |
| `libeigen3-dev`              | Linear Algebra Acceleration |
| `libopengl-dev`              | Visualization (Pangolin) |
| `libboost-system-dev`        | Utility Functions and Tools |
| `libboost-serialization-dev` | Serialization and De-serialization |

### Code in **src** and **include** folders

* *ORBextractor.cc*.
This is a modified version of orb.cpp of OpenCV library. The original code is BSD licensed.

* *PnPsolver.h, PnPsolver.cc*.
This is a modified version of the epnp.h and epnp.cc of Vincent Lepetit. 
This code can be found in popular BSD licensed computer vision libraries as [OpenCV](https://github.com/Itseez/opencv/blob/master/modules/calib3d/src/epnp.cpp) and [OpenGV](https://github.com/laurentkneip/opengv/blob/master/src/absolute_pose/modules/Epnp.cpp). The original code is FreeBSD.

* *MLPnPsolver.h, MLPnPsolver.cc*.
This is a modified version of the MLPnP of Steffen Urban from [here](https://github.com/urbste/opengv). 
The original code is BSD licensed.

* Function *ORBmatcher::DescriptorDistance* in *ORBmatcher.cc*.
The code is from: http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel.
The code is in the public domain.

### Submodules under [`deps`](./deps/) directory.

1. [**DBoW2**](https://github.com/dorian3d/DBoW2)

    DBoW2 is an improved version of the DBow library, an open source C++ library for indexing and converting images into a bag-of-word representation.

    This project is distributed under a [modified version](https://github.com/dorian3d/DBoW2/blob/master/LICENSE.txt) of the BSD license.

1. [**g2o**](https://github.com/RainerKuemmerle/g2o)

    `g2o` is an open-source C++ framework for optimizing graph-based nonlinear error functions

    This project is distributed under the BSD License.

1. [**Sophus**](https://github.com/strasdat/Sophus)

    `Sophus` is a c++ implementation of Lie groups commonly used for 2d and 3d geometric problems.

    This project is distributed under the BSD License.

1. [**Pangolin**](https://github.com/stevenlovegrove/Pangolin).

    `Pangolin` is a set of lightweight and portable utility libraries for prototyping 3D, numeric or video based programs and algorithms.

    This project is distributed under the MIT License.
