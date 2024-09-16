# Makescript for ORB_SLAM3
# Author: Yuxuan Zhang
CMAKE:=cmake
# Default output locations
BUILD_DIR?=$(PWD)/build
INSTALL_DIR?=$(PWD)/install

CMAKE += -DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR)

all: deps core

core: core/release

core/release: CMAKE += -DCMAKE_BUILD_TYPE=Release
core/debug: CMAKE += -DCMAKE_BUILD_TYPE=Debug

core/release core/debug:
	$(info Configuring and building ORB_SLAM3 core ...)
	$(eval BUILD_DIR:=$(PWD)/build/ORB_SLAM3)
	@ mkdir -p $(BUILD_DIR)
	@ $(CMAKE) \
		-S core \
		-B $(BUILD_DIR)
	@ cd $(BUILD_DIR) && make install -j
	@ ln -sf $(BUILD_DIR)/compile_commands.json $(PWD)

clean:
	$(info Cleaning build and install directory ...)
	@ rm -rf ./build/* $(INSTALL_DIR)

# Thirdparty make scripts
include scripts/*.mk

.PHONY: core core/release core/debug clean
