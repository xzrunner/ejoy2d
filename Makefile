.PHONY : mingw ej2d linux undefined

OUTSIDE:=..

CFLAGS = -g -Wall -std=gnu99 -Ilib -Ilib/render -Ilib/adapter -Ilua -D EJOY2D_OS=$(OS) -D FONT_EDGE_HASH \
-I$(OUTSIDE)/ps \
-I$(OUTSIDE)/dtex \
-I$(OUTSIDE)/gtxt \
-I$(OUTSIDE)/shaderlab \
-I$(OUTSIDE)/RapidVG \
-I$(OUTSIDE)/thirdparty/glew/include \
-I$(OUTSIDE)/thirdparty/pthread/include \
-I$(OUTSIDE)/thirdparty/cjson \
-I$(OUTSIDE)/thirdparty/lzma \
-I$(OUTSIDE)/thirdparty/freetype/include

LIBS = $(OUTSIDE)/thirdparty/pthread/libpthreadGC2.a

LDFLAGS :=

RENDER := \
lib/render/render.c \
lib/render/carray.c \
lib/render/log.c

ADAPTER := \
lib/adapter/ej_dtex.c \
lib/adapter/ej_gtxt.c \
lib/adapter/ej_ps.c \
lib/adapter/ej_rvg.c \
lib/adapter/ej_shaderlab.c \
lib/adapter/ej_utility.c \

EJOY2D := \
lib/ejoy2dgame.c \
lib/fault.c \
lib/lmatrix.c \
lib/lsprite.c \
lib/matrix.c \
lib/scissor.c \
lib/sprite.c \
lib/screen.c \
lib/spritepack.c

CJSON := $(OUTSIDE)/thirdparty/cjson/cJSON.c

LZMA := $(wildcard $(OUTSIDE)/thirdparty/lzma/*.c)

PS := $(wildcard $(OUTSIDE)/ps/*.c)
DTEX := $(wildcard $(OUTSIDE)/dtex/*.c)
GTXT := $(wildcard $(OUTSIDE)/gtxt/*.c)
SL := $(wildcard $(OUTSIDE)/shaderlab/*.c)
RVG := $(wildcard $(OUTSIDE)/RapidVG/*.c)

SRC := $(EJOY2D) $(RENDER) $(ADAPTER) $(CJSON) $(LZMA) \
$(PS) \
$(DTEX) \
$(GTXT) \
$(SL) \
$(RVG)

LUASRC := \
lua/lapi.c \
lua/lauxlib.c \
lua/lbaselib.c \
lua/lbitlib.c \
lua/lcode.c \
lua/lcorolib.c \
lua/lctype.c \
lua/ldblib.c \
lua/ldebug.c \
lua/ldo.c \
lua/ldump.c \
lua/lfunc.c \
lua/lgc.c \
lua/linit.c \
lua/liolib.c \
lua/llex.c \
lua/lmathlib.c \
lua/lmem.c \
lua/loadlib.c \
lua/lobject.c \
lua/lopcodes.c \
lua/loslib.c \
lua/lparser.c \
lua/lstate.c \
lua/lstring.c \
lua/lstrlib.c \
lua/ltable.c \
lua/ltablib.c \
lua/ltm.c \
lua/lundump.c \
lua/lutf8lib.c \
lua/lvm.c \
lua/lzio.c

CC=gcc
UNAME=$(shell uname)
SYS=$(if $(filter Linux%,$(UNAME)),linux,\
	    $(if $(filter MINGW%,$(UNAME)),mingw,\
	    $(if $(filter Darwin%,$(UNAME)),macosx,\
	        undefined\
)))

all: $(SYS)

undefined:
	@echo "I can't guess your platform, please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "      linux mingw macosx"


mingw : OS := WINDOWS
mingw : TARGET := ej2d.exe
mingw : CFLAGS += -I/usr/include -L$(OUTSIDE)/thirdparty/glew/lib -DGLEW_STATIC
mingw : LDFLAGS += -L/usr/bin -L$(OUTSIDE)/thirdparty/pthread -L$(OUTSIDE)/thirdparty/freetype -lgdi32 -lglew32s -lfreetype -lopengl32 
mingw : SRC += mingw/window.c mingw/winfw.c
mingw : LINK := $(OUTSIDE)/thirdparty/pthread/pthreadGC2.dll

mingw : $(SRC) ej2d

winlib : OS := WINDOWS
winlib : TARGET := ejoy2d.dll
winlib : CFLAGS += -I/usr/include -I/usr/local/include --shared
winlib : LDFLAGS += -L/usr/bin -lgdi32 -lglew32 -lopengl32 -L/usr/local/bin -llua53
winlib : SRC += mingw/winfont.c lib/lejoy2dcore.c

winlib : $(SRC) ej2dlib

ej2dlib :
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

linux : OS := LINUX
linux : TARGET := ej2d
linux : CFLAGS += -I/usr/include $(shell freetype-config --cflags)
linux : LDFLAGS +=  -lGLEW -lGL -lX11 -lfreetype -lm
linux : SRC += posix/window.c posix/winfw.c posix/winfont.c

linux : $(SRC) ej2d

macosx : CC := clang
macosx : OS := MACOSX
macosx : TARGET := ej2d
macosx : CFLAGS += -I/usr/include $(shell freetype-config --cflags) -D __MACOSX
macosx : LDFLAGS += -lglfw3  -framework OpenGL -lfreetype -lm -ldl
macosx : SRC += mac/example/example/window.c posix/winfw.c mac/example/example/winfont.c

macosx : $(SRC) ej2d

ej2d :
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LUASRC) $(LDFLAGS) $(LIBS)

clean :
	-rm -f ej2d.exe
	-rm -f ej2d
