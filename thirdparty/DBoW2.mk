thirdparty/DBoW2.target:
	$(info "Configuring and building thirdparty/DBoW2 ...")
	cd thirdparty/DBoW2 \
	mkdir -p build && cd build \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
	make -j

thirdparty/DBoW2.clean:
	$(info "Cleaning thirdparty/DBoW2 ...")
	cd thirdparty/DBoW2 && rm -rf build

.PHONY: *.target *.clean
