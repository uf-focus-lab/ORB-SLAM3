# Default output locations
BUILD_DIR?=$(PWD)/build
INSTALL_DIR?=$(PWD)/install
CMAKE_ARGS?=-DCMAKE_BUILD_TYPE=Release
CMAKE_ARGS+=-DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR)

deps := $(shell ls deps)
deps_targets := $(patsubst %,deps/%,$(deps))

$(deps_targets):
	$(eval NAME:=$(notdir $@))
	$(eval DEP_BUILD_DIR:=$(BUILD_DIR)/$(NAME))
	$(eval CMD=cmake $(CMAKE_ARGS) -S $@ -B $(DEP_BUILD_DIR))
	$(info Configuring and building dependency: $(NAME) ...)
	$(info $(CMD))
	@ mkdir -p $(DEP_BUILD_DIR) $(INSTALL_DIR)
	@ $(CMD) && cd $(DEP_BUILD_DIR) && make install -j

deps: deps.init $(deps_targets)

deps.init:
	$(info Initializing submodules ...)
	@ git submodule update --init

.PHONY: deps.init $(deps_targets)
