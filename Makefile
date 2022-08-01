CC=clang

ROOT:=$(shell pwd)
FLAGS=-fPIC
FLAGS+=-Ivendor/nvafx/include
FLAGS+=-Lvendor/nvafx/lib -l"nv_audiofx" -Wl,-rpath,'$(ROOT)/vendor/nvafx/lib'
FLAGS+=-Lvendor/cuda/lib -l"cudart" -Wl,-rpath,'$(ROOT)/vendor/cuda/lib'
FLAGS+=$(shell pkg-config --cflags --libs libpipewire-0.3)

SRCS:=src/main.c
HDRS:=src/circlebuf.h
OBJS:=build/pw-nvidia-denoiser

$(OBJS): ${SRCS} ${HDRS}
	mkdir -p build
	$(CC) -g ${SRCS} -o $@  $(FLAGS)

run: $(OBJS)
	./build/pw-nvidia-denoiser ./vendor/models/sm_86/denoiser_48k.trtpkg

debug: $(OBJS)
	gdb ./build/pw-nvidia-denoiser

clean:
	rm -rf build
