# Makescript for ORB_SLAM3
# Author: Yuxuan Zhang
CC:=clang
CXX:=clang++
# Default output locations
BUILD_DIR?=$(PWD)/build
INSTALL_DIR?=$(PWD)/install

all: deps core assets/ORBvoc.txt

core:
	$(info Configuring and building ORB_SLAM3 core ...)
	$(eval BUILD_DIR:=$(PWD)/build/ORB_SLAM3)
	@ mkdir -p $(BUILD_DIR)
	@ cmake \
		-DCMAKE_BUILD_TYPE=Release \
	  	-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
	  	-S $@ \
		-B $(BUILD_DIR)
	@ cd $(BUILD_DIR) && make install -j
	@ ln -sf $(BUILD_DIR)/compile_commands.json $(PWD)

assets/ORBvoc.txt: assets/ORBvoc.txt.tar.gz
	$(info Uncompressing vocabulary ...)
	@ cd assets && tar -xf ORBvoc.txt.tar.gz

clean:
	$(info Cleaning vocabulary ...)
	@ rm -rf assets/ORBvoc.txt
	$(info Cleaning build and install directory ...)
	@ rm -rf ./build/* $(INSTALL_DIR)

# Thirdparty make scripts
include scripts/*.mk

.PHONY: core clean
