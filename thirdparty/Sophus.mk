thirdparty/Sophus.target:
	$(info "Configuring and building thirdparty/Sophus ...")
	cd thirdparty/Sophus \
	mkdir -p build && cd build \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
	make -j

thirdparty/Sophus.clean:
	$(info "Cleaning thirdparty/Sophus ...")
	cd thirdparty/Sophus && rm -rf build

.PHONY: *.target *.clean
