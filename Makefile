TARGET := 27Menu
INT    := build
OUT    := out

TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
GH_SDK    := $(GOLDHEN_SDK)

LIBS := -lSceLibcInternal -lGoldHEN_Hook -lkernel -lSceSysmodule \
        -lSceSysutil -lScePad -lSceGnm -lSceVideoOut -lScePthread

CFLAGS := -D__FINAL__=0 --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c \
          -O2 -Wall -Wno-unused-function -isysroot $(TOOLCHAIN) \
          -isystem $(TOOLCHAIN)/include -I$(GH_SDK)/include

LDFLAGS := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x -e _init --eh-frame-hdr \
           -L$(TOOLCHAIN)/lib -L$(GH_SDK) $(LIBS)

all: $(OUT)/$(TARGET).prx

$(OUT)/$(TARGET).prx: main.c
	@mkdir -p $(INT) $(OUT)
	clang $(CFLAGS) -o $(INT)/main.o main.c
	ld.lld $(GH_SDK)/build/crtprx.o $(INT)/main.o -o $(INT)/$(TARGET).elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/linux/create-fself -in=$(INT)/$(TARGET).elf \
		-out=$(INT)/$(TARGET).oelf --lib=$@ --paid 0x3800000000000011

clean:
	rm -rf $(INT) $(OUT)

.PHONY: all clean
