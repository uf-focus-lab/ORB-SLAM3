thirdparty_targets := DBoW2 g2o Sophus Pangolin

thirdparty_build_targets := $(patsubst %,thirdparty/%,$(thirdparty_targets))

$(thirdparty_build_targets):
	$(eval PROJ := build/thirdparty/$(notdir $@))
	$(info Configuring and building $(PROJ) ...)
	@ mkdir -p $(PROJ) $(PWD)/lib/thirdparty
	cmake \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_INSTALL_PREFIX:PATH=$(PWD)/lib/thirdparty/$$(basename $@) \
	  -S $@ \
	  -B $(PROJ)
	@ cd $(PROJ) && make install -j

submodule_init:
	$(info Initializing submodules ...)
	@ git submodule update --init --recursive --remote

thirdparty: submodule_init $(thirdparty_build_targets)

thirdparty.clean:
	$(info Cleaning thirdparty libraries ...)
	@ rm -rf thirdparty/build thirdparty/lib

.PHONY: $(thirdparty_build_targets) submodule_init thirdparty *.clean
