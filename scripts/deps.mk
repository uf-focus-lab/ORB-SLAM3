# Default output locations
BUILD_DIR?=$(PWD)/build
INSTALL_DIR?=$(PWD)/install

deps := $(shell ls deps)

deps_targets := $(patsubst %,deps/%,$(deps))

$(deps_targets):
	$(eval NAME:=$(notdir $@))
	$(eval BUILD_DIR:=$(BUILD_DIR)/$(NAME))
	$(info Configuring and building dependency: $(NAME) ...)
	@ mkdir -p $(BUILD_DIR) $(INSTALL_DIR)
	@ cmake \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
	  -S $@ \
	  -B $(BUILD_DIR)
	@ cd $(BUILD_DIR) && make install -j

deps: deps.init $(deps_targets)

deps.init:
	$(info Initializing submodules ...)
	@ git submodule update --init --recursive --remote

.PHONY: deps.init $(deps_targets)
