# Makescript for ORB_SLAM3
# Author: Yuxuan Zhang

ORB_SLAM3: vocabulary/ORBvoc.txt
	$(info Configuring and building ORB_SLAM3 ...)
	@ mkdir -p build && cd build && \
	  cmake .. -DCMAKE_BUILD_TYPE=Release && \
	  make

vocabulary/ORBvoc.txt: vocabulary/ORBvoc.txt.tar.gz
	$(info Uncompressing vocabulary ...)
	@ cd vocabulary && tar -xf ORBvoc.txt.tar.gz

clean:
	$(info Cleaning vocabulary ...)
	@ cd vocabulary && rm -rf ORBvoc.txt
	$(info Cleaning ORB_SLAM3 ...)
	@ rm -rf build

# Thirdparty make scripts
include scripts/*.mk

.PHONY: *.build clean *.clean init
