ProjectHome=$(pwd)
Thirdparty=$(pwd)/Thirdparty

ORB_SLAM3: *.target
	$(info "Configuring and building ORB_SLAM3 ...")
	cd ${ProjectHome} && mkdir -p build && cd build && \
	cmake .. -DCMAKE_BUILD_TYPE=Release && \
	make

ORB_SLAM3.clean:
	$(info "Cleaning ORB_SLAM3 ...")
	cd ${ProjectHome}/build && rm -rf *

Thirdparty/DBoW2.target:
	$(info "Configuring and building Thirdparty/DBoW2 ...")
	cd ${Thirdparty}/DBoW2 \
	mkdir -p build && cd build \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
	make -j

Thirdparty/DBoW2.clean:
	$(info "Cleaning Thirdparty/DBoW2 ...")
	cd ${Thirdparty}/DBoW2 && rm -rf build

Thirdparty/g2o.target:
	$(info "Configuring and building Thirdparty/g2o ...")
	cd ${Thirdparty}/g2o \
	mkdir -p build && cd build \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
	make -j

Thirdparty/g2o.clean:
	$(info "Cleaning Thirdparty/g2o ...")
	cd ${Thirdparty}/g2o && rm -rf build

Thirdparty/Sophus.target:
	$(info "Configuring and building Thirdparty/Sophus ...")
	cd ${Thirdparty}/Sophus \
	mkdir -p build && cd build \
	cmake .. -DCMAKE_BUILD_TYPE=Release \
	make -j

Thirdparty/Sophus.clean:
	$(info "Cleaning Thirdparty/Sophus ...")
	cd ${Thirdparty}/Sophus && rm -rf build

vocabulary.target:
	$(info "Uncompress vocabulary ...")
	cd ${ProjectHome}/Vocabulary && \
	test -f ORBvoc.txt && echo "ORBvoc.txt already extracted" || tar -xf ORBvoc.txt.tar.gz

vocabulary.clean:
	$(info "Cleaning vocabulary ...")
	cd ${ProjectHome}/Vocabulary && rm -rf ORBvoc.txt

clean: *.clean

init:
	git submodule update --init --recursive --remote

.PHONY: *.target clean *.clean init