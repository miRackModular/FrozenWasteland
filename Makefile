V1_COMPAT=1
RACK_DIR ?= ../..

# FLAGS will be passed to both the C and C++ compiler
FLAGS += \
	-I$(DEP_OUT)/include \
	-I./src/ui \
	-I./src/dsp-delay \
	-I./src/dsp-filter/utils -I./src/dsp-filter/filters -I./src/dsp-filter/third-party/falco	


# Add .cpp and .c files to the build
SOURCES += $(wildcard src/*.cpp src/old/*.cpp src/filters/*.cpp src/dsp-noise/*.cpp src/dsp-filter/*.cpp  src/stmlib/*.cc)
SOURCES := $(filter-out src/BPMLFOPhaseExpander.cpp,$(SOURCES))
SOURCES := $(filter-out src/PNChordExpander.cpp,$(SOURCES))
SOURCES := $(filter-out src/QARGrooveExpander.cpp,$(SOURCES))
SOURCES := $(filter-out src/QARProbabilityExpander.cpp,$(SOURCES))
SOURCES := $(filter-out src/SeedsOfChangeCVExpander.cpp,$(SOURCES))
SOURCES := $(filter-out src/SeedsOfChangeGateExpander.cpp,$(SOURCES))
SOURCES := $(filter-out src/VoxInhumanaExpander.cpp,$(SOURCES))

# Add files to the ZIP package when running `make dist`
# The compiled plugin is automatically added.
DISTRIBUTABLES += $(wildcard LICENSE*) res

# Static libs
libsamplerate = $(DEP_OUT)/lib/libsamplerate.a
OBJECTS += $(libsamplerate)

DEPS += $(libsamplerate)

# Include the VCV plugin Makefile framework
include $(RACK_DIR)/plugin.mk

$(libsamplerate):
	cd dep && $(WGET) http://www.mega-nerd.com/SRC/libsamplerate-0.1.9.tar.gz
	cd dep && $(UNTAR) libsamplerate-0.1.9.tar.gz
	cd dep/libsamplerate-0.1.9 && $(CONFIGURE)
	cd dep/libsamplerate-0.1.9/src && $(MAKE)
	cd dep/libsamplerate-0.1.9/src && $(MAKE) clean
	cd dep/libsamplerate-0.1.9/src && $(MAKE) install
