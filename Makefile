# Makescript for ORB_SLAM3
# Author: Yuxuan Zhang
CC:=clang
CXX:=clang++
BUILD_DIR:=build/ORB_SLAM3

ORB_SLAM3: vocabulary/ORBvoc.txt
	$(info Configuring and building ORB_SLAM3 ...)
	@ mkdir -p $(BUILD_DIR)
	@ cmake \
		-DCMAKE_BUILD_TYPE=Release \
	  	-S . \
		-B $(BUILD_DIR)
	@ cd $(BUILD_DIR) && make -j

vocabulary/ORBvoc.txt: vocabulary/ORBvoc.txt.tar.gz
	$(info Uncompressing vocabulary ...)
	@ cd vocabulary && tar -xf ORBvoc.txt.tar.gz

clean:
	$(info Cleaning vocabulary ...)
	@ cd vocabulary && rm -rf ORBvoc.txt
	$(info Cleaning ORB_SLAM3 ...)
	@ rm -rf $(BUILD_DIR) lib/libORB_SLAM3.so bin

# Thirdparty make scripts
include scripts/*.mk

.PHONY: *.build clean *.clean init
