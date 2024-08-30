thirdparty/g2o.target:
	$(info "Configuring and building thirdparty/g2o ...")
	cd thirdparty/g2o \
	mkdir -p build && cd build \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
	make -j

thirdparty/g2o.clean:
	$(info "Cleaning thirdparty/g2o ...")
	cd thirdparty/g2o && rm -rf build

.PHONY: *.target *.clean
