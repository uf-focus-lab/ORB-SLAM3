ProjectHome=$(pwd)

ORB_SLAM3: *.target
	$(info "Configuring and building ORB_SLAM3 ...")
	cd ${ProjectHome} && mkdir -p build && cd build && \
	cmake .. -DCMAKE_BUILD_TYPE=Release && \
	make

ORB_SLAM3.clean:
	$(info "Cleaning ORB_SLAM3 ...")
	cd ${ProjectHome}/build && rm -rf *

vocabulary.target:
	$(info "Uncompress vocabulary ...")
	cd ${ProjectHome}/Vocabulary && \
	test -f ORBvoc.txt && echo "ORBvoc.txt already extracted" || tar -xf ORBvoc.txt.tar.gz

vocabulary.clean:
	$(info "Cleaning vocabulary ...")
	cd ${ProjectHome}/Vocabulary && rm -rf ORBvoc.txt

# Thirdparty make scripts
include ${thirdparty}/*.mk

clean: *.clean

init:
	git submodule update --init --recursive --remote

.PHONY: *.target clean *.clean init
