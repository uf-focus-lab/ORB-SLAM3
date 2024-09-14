# Default output locations
BUILD_DIR?=$(PWD)/build
INSTALL_DIR?=$(PWD)/install

deps := $(shell ls deps)

deps_targets := $(patsubst %,deps/%,$(deps))

$(deps_targets):
	$(eval NAME:=$(notdir $@))
	$(eval DEP_BUILD_DIR:=$(BUILD_DIR)/$(NAME))
	$(info Configuring and building dependency: $(NAME) ...)
	@ mkdir -p $(DEP_BUILD_DIR) $(INSTALL_DIR)
	@ cmake \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_INSTALL_PREFIX=$(INSTALL_DIR) \
	  -S $@ \
	  -B $(DEP_BUILD_DIR)
	@ cd $(DEP_BUILD_DIR) && make install -j

deps: deps.init $(deps_targets)

deps.init:
	$(info Initializing submodules ...)
	@ git submodule update --init

.PHONY: deps.init $(deps_targets)
