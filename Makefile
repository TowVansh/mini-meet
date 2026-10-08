# Mini-Meet build. Works on Linux (gcc) and Windows (MSYS2 MinGW64 shell).
#   make            build bin/mm-server, bin/mm-stun, bin/mm
#   make test       build and run unit tests
#   make dist       (Windows) copy the needed DLLs next to the .exe files

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers

ifeq ($(OS),Windows_NT)
  EXE     := .exe
  NETLIBS := -lws2_32 -liphlpapi
  GUI     := -mconsole
else
  EXE     :=
  NETLIBS :=
  GUI     :=
endif

PKGS        := sdl2 vpx opus libavdevice libavformat libavcodec libswscale libswresample libavutil
MEDIA_CFLAGS := $(shell pkg-config --cflags $(PKGS))
MEDIA_LIBS   := $(shell pkg-config --libs $(PKGS))

COMMON := src/common/net.c src/common/util.c src/common/proto.c
STUN   := src/stun/stun.c
CLIENT := src/client/main.c src/client/netloop.c src/client/sig.c src/client/ice.c \
          src/client/media_in.c src/client/media_out.c src/client/rtp.c src/client/cc.c src/client/font.c

BINS := bin/mm-server$(EXE) bin/mm-stun$(EXE) bin/mm$(EXE)

all: $(BINS)

bin:
	mkdir -p bin

bin/mm-server$(EXE): src/server/mm_server.c $(COMMON) | bin
	$(CC) $(CFLAGS) -o $@ $^ $(NETLIBS)

bin/mm-stun$(EXE): src/stun/mm_stun.c $(STUN) $(COMMON) | bin
	$(CC) $(CFLAGS) -o $@ $^ $(NETLIBS)

bin/mm$(EXE): $(CLIENT) $(STUN) $(COMMON) src/client/*.h | bin
	$(CC) $(CFLAGS) $(MEDIA_CFLAGS) -o $@ $(CLIENT) $(STUN) $(COMMON) $(MEDIA_LIBS) $(NETLIBS) -lm $(GUI)

bin/test$(EXE): tests/test_main.c src/client/rtp.c src/client/cc.c $(STUN) $(COMMON) | bin
	$(CC) $(CFLAGS) -o $@ $^ $(NETLIBS) -lm

test: bin/test$(EXE)
	./bin/test$(EXE)

# Windows: gather the MinGW DLLs mm.exe depends on, so bin/ runs on a PC without MSYS2.
dist: all
	ldd bin/mm$(EXE) | grep -i mingw64 | awk '{print $$3}' | xargs -I{} cp -u {} bin/

clean:
	rm -rf bin

.PHONY: all test dist clean
