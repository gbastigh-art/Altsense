print-%:; @echo $($*)

# uncomment below for build configs
# RELEASE = 1
# SANITIZE = 1

UNAME = $(shell uname -s)

ifndef $(HOST_PLATFORM)
	ifeq ($(UNAME),Darwin)
		HOST_PLATFORM = macos
		HOST_ARCH = $(shell uname -m)
	else ifeq ($(UNAME),Linux)
		HOST_PLATFORM = linux
		HOST_ARCH = x86_64 # TODO: assert
	else
		HOST_PLATFORM = windows
		HOST_ARCH = x86_64 # TODO: assert
	endif
endif

ifndef $(HOST_ARCH)
	ifeq ($(HOST_PLATFORM),macos)
		HOST_ARCH = $(shell uname -m)
	else
		HOST_ARCH = x86_64 # TODO: assert
	endif
endif

ifndef $(TARGET_PLATFORM)
	TARGET_PLATFORM = $(HOST_PLATFORM)
endif

ifndef $(TARGET_ARCH)
	TARGET_ARCH = $(HOST_ARCH)
endif

# library paths
PATH_LIB         = lib
PATH_SDL         = $(PATH_LIB)/SDL
PATH_SOLOUD      = $(PATH_LIB)/soloud
PATH_CIMGUI      = $(PATH_LIB)/cimgui
PATH_SOKOL       = $(PATH_LIB)/sokol
PATH_SOKOL_TOOLS = $(PATH_LIB)/sokol-tools-bin

CCFLAGS =
LDFLAGS =

# make C a little saner (GCC/clang specific)
CCFLAGS += -fwrapv
CCFLAGS += -fno-delete-null-pointer-checks
CCFLAGS += -fno-strict-aliasing

# openmp where enabled
LDFLAGS += -fopenmp
CCFLAGS += -fopenmp

ifeq ($(HOST_PLATFORM),macos)
	CC   = $(shell brew --prefix llvm)/bin/clang
	LD   = $(shell brew --prefix llvm)/bin/clang
	DB   = $(shell brew --prefix llvm)/bin/lldb

	CCFLAGS += -I$(shell brew --prefix llvm)/include
	LDFLAGS += -L$(shell brew --prefix llvm)/lib

	DYLIB = dylib
else ifeq ($(HOST_PLATFORM),linux)
	CC = gcc
	LD = gcc
	DB = gdb
	DYLIB = so
else
	$(error no support for windows hosts as of right now)
endif

ifeq ($(HOST_PLATFORM),macos)
	ifeq ($(HOST_ARCH),arm64)
		SHDC = $(PATH_SOKOL_TOOLS)/bin/osx_arm64/sokol-shdc
		# SHDC = lib/fips-deploy/sokol-tools/osx-ninja-debug/sokol-shdc
	else
		SHDC = $(PATH_SOKOL_TOOLS)/bin/osx/sokol-shdc
	endif
else ifeq ($(HOST_PLATFORM),linux)
	SHDC = $(PATH_SOKOL_TOOLS)/bin/linux/sokol-shdc
else
	SHDC = $(PATH_SOKOL_TOOLS)/bin/win32/sokol-shdc.exe
endif

ifeq ($(HOST_PLATFORM),macos)
	SHADER_SLANG = metal_macos
else
	SHADER_SLANG = glsl410
endif

INCFLAGS = -iquotesrc -iquoteutil

CCFLAGS += -std=gnu23

ifdef RELEASE
	CCFLAGS += -O2
	CCFLAGS += -DTARGET_RELEASE
else
	CCFLAGS += -O0 -g -fno-omit-frame-pointer
	CCFLAGS += -DRELOADHOST_CLIENT_ENABLED
	CCFLAGS += -DTARGET_DEBUG

	ifdef SANITIZE
		CCFLAGS += -DSANITIZE -fsanitize=undefined,address -fno-sanitize=function
		LDFLAGS += -fsanitize=undefined,address -fno-sanitize=function
	endif
endif

CCFLAGS += -Wall
CCFLAGS += -Wextra
CCFLAGS += -Wno-unused-parameter
CCFLAGS += -Wno-fixed-enum-extension
CCFLAGS += -Wenum-compare
CCFLAGS += -Wenum-conversion

LDFLAGS += -lm -lstdc++

ifeq ($(UNAME),Darwin)
	LDFLAGS += -framework Foundation -framework Metal -framework QuartzCore -framework AppKit
else
	CCFLAGS += -D_GNU_SOURCE
	LDFLAGS += -lGL -lasound
endif

# add defines for specific platforms, shader targets
CCFLAGS += -DTARGET_PLATFORM_$(TARGET_PLATFORM)
CCFLAGS += -DTARGET_ARCH_$(TARGET_ARCH)

LDFLAGS += -Lbin/lib
LDFLAGS += -lSDL2-2.0
LDFLAGS += -Wl,-rpath -Wl,bin/lib

CPPFLAGS = -std=c++17 -O3 -fno-rtti -fno-exceptions -fPIC

ifndef RELEASE
	CPPFLAGS += -g -fno-omit-frame-pointer
endif

BIN  	  = bin

SRC  	  = $(shell find src -name "*.c")
OBJ 	  = $(SRC:%.c=$(BIN)/%.o)

SRC_OBJC = 
OBJ_OBJC =

ifeq ($(TARGET_PLATFORM),macos)
    SRC_OBJC += $(shell find src -name "*.m")
    OBJ_OBJC += $(SRC_OBJC:%.m=$(BIN)/%.o)
    CCFLAGS += -x objective-c
endif

SRC_CPP   = $(shell find src -name "*.cpp")

# add cimgui sources
SRC_CIMGUI  = $(shell find $(PATH_CIMGUI) -name "*.cpp" -maxdepth 1)
SRC_CIMGUI += $(shell find $(PATH_CIMGUI)/imgui -name "*.cpp" -maxdepth 1)
OBJ_CIMGUI = $(SRC_CIMGUI:%.cpp=$(BIN)/%.o)

SRC_CPP += $(SRC_CIMGUI)

# for cimgui
CPPFLAGS += -DWITH_SDL2_STATIC -iquotelib/cimgui/imgui -Ilib/sdl/include

# add soloud sources
SRC_CPP   += $(shell find $(PATH_SOLOUD)/src/audiosource/wav -name "*.cpp")
SRC_CPP   += $(shell find $(PATH_SOLOUD)/src/backend/sdl2_static -name "*.cpp")
SRC_CPP   += $(shell find $(PATH_SOLOUD)/src/core -name "*.cpp")
SRC_CPP   += $(shell find $(PATH_SOLOUD)/src/filter -name "*.cpp")

# for soloud backend
CPPFLAGS += -DWITH_SDL2_STATIC -iquotelib/soloud/include -iquotelib/SDL/include

OBJ_CPP   = $(SRC_CPP:%.cpp=$(BIN)/%.o)

DEP  	  = $(SRC:%.c=$(BIN)/%.d) $(SRC_OBJC:%.m=$(BIN)/%.d) $(SRC_CPP:%.cpp=$(BIN)/%.d)

OUT  	  = $(BIN)/game
OUT_SHARED = $(BIN)/game.$(DYLIB)

UTIL 	= $(shell find util -name "*.c")
UTIL_DEP = $(UTIL:%.c=$(BIN)/%.d)
UTIL_OUT = $(UTIL:%.c=$(BIN)/%)

SRC_SHADER = $(shell find src/shader -name "*.glsl")
OUT_SHADER = $(SRC_SHADER:%.glsl=%.glsl.h)

all: dirs libs shaders build utils

$(OUT_SHADER): %.glsl.h: %.glsl
	$(SHDC) --input $^ --output $@ --slang $(SHADER_SLANG) --format=sokol_impl -r

shaders: $(OUT_SHADER)

-include $(DEP) $(UTILDEP) $(TESTDEP)

export LD_LIBRARY_PATH=bin/lib
export LIBRARY_PATH=bin/lib

$(BIN):
	$(shell mkdir -p  $@)

dirs: FORCE $(BIN)
	$(shell mkdir -p $(BIN)/src)
	$(shell mkdir -p $(BIN)/util)
	$(shell mkdir -p $(BIN)/ext)
	$(shell mkdir -p $(BIN)/lib)
	rsync -a --include '*/' --exclude '*' "lib/cimgui" "bin/lib"
	rsync -a --include '*/' --exclude '*' "lib/soloud" "bin/lib"
	rsync -a --include '*/' --exclude '*' "src" "bin"

lib-sdl:
	$(shell mkdir -p  $(BIN)/sdl)
	$(shell mkdir -p  $(BIN)/lib)
	cmake -S $(PATH_SDL) -B $(BIN)/sdl
	cd $(BIN)/sdl && $(MAKE)
	chmod +x $(BIN)/sdl/sdl2-config
	cp $(BIN)/sdl/libSDL2-2.0.$(DYLIB) $(BIN)/lib
	cp $(BIN)/lib/libSDL2-2.0.$(DYLIB) $(BIN)/lib/libSDL2.$(DYLIB)
	cp $(BIN)/lib/libSDL2-2.0.$(DYLIB) $(BIN)/lib/libSDL2-2.0.0.$(DYLIB)

# just an extra target for tests which require cimgui as a static dep, not
# required to build the game
lib-cimgui: $(OBJ_CIMGUI)
	ar r bin/libcimgui.a $^

libs: dirs lib-sdl

$(UTIL_OUT): $(BIN)/%: %.c
	$(CC) -o $@ -MMD $(CCFLAGS) $(INCFLAGS) $(LDFLAGS) $<

utils: $(UTIL_OUT)

$(OBJ): $(BIN)/%.o: %.c
	$(CC) -o $@ -MMD -c $(CCFLAGS) $(INCFLAGS) $<

$(OBJ_OBJC): $(BIN)/%.o: %.m
	$(CC) -x objective-c -o $@ -MMD -c $(CCFLAGS) $(INCFLAGS) $<

$(OBJ_CPP): $(BIN)/%.o: %.cpp
	$(CC) -o $@ -MMD -c $(CPPFLAGS) $(INCFLAGS) $<

run: build
	DYLD_LIBRARY_PATH=bin/lib $(OUT)

run-reloadable: utils build-shared
	$(BIN)/util/reloadhost $(OUT_SHARED)

debug-reloadable: utils build-shared
	ASAN_OPTIONS=detect_leaks=1 $(DB) $(BIN)/util/reloadhost -o 'run $(OUT_SHARED)'

build-shared: dirs $(OBJ) $(OBJ_OBJC) $(OBJ_CPP)
	$(LD) -shared -o $(OUT_SHARED) $(filter %.o,$^) $(LDFLAGS)

build: dirs shaders $(OBJ) $(OBJ_OBJC) $(OBJ_CPP)
	$(LD) -o $(OUT) $(filter %.o,$^) $(LDFLAGS)

sim-shaders:
	$(SHDC) \
		--input test/sim_draw.glsl \
		--output test/sim_draw.glsl.h \
		--slang metal_macos \
		--format=sokol_impl -r
	$(SHDC) \
		--input test/sim_blit.glsl \
		--output test/sim_blit.glsl.h \
		--slang metal_macos \
		--format=sokol_impl -r

sim: sim-shaders
	$(CC) -std=gnu2x \
		-x objective-c \
		-I/opt/homebrew/include/ \
		-iquotesrc \
		-iquoteutil \
		-o bin/sim \
		test/sim.c \
		-DTARGET_PLATFORM_macos \
		-DTARGET_ARCH_arm64 \
		-DUTIL_IMPL \
		-O2 \
		-g \
		-framework Foundation \
		-framework AppKit \
		-framework Metal \
		-framework MetalKit \
		-framework QuartzCore \
		-fopenmp \
		-fwrapv \
		-fno-strict-aliasing \
		-Lbin \
		-lcimgui \
		-lstdc++


clean-obj:
	rm -rf bin/src bin/util
	rm -rf $(OBJ)
	rm -rf $(OBJ_CPP)

clean-deps:
	rm -rf $(DEP) $(UTIL_DEP)

clean-libs:
	 find bin ! -name 'src' ! -name 'util' -type d -maxdepth 1 -mindepth 1 -exec rm -rf {} +

clean-shaders:
	rm -rf $(OUT_SHADER)

clean: clean-obj clean-shaders clean-libs

# TODO: very platform specific
blend-me:
	cp util/mmdl.py /Applications/Blender.app/Contents/Resources/3.4/scripts/addons

FORCE: ;
