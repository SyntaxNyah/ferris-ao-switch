#---------------------------------------------------------------------------------
# ferris-ao-switch — Nintendo Switch homebrew AO2 client
# devkitPro NX + SDL2 portlibs
#
# Prerequisites (run once):
#   dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_image \
#                  switch-sdl2_ttf switch-sdl2_mixer switch-sdl2_net \
#                  switch-libwebp switch-mbedtls
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Source /etc/profile.d/devkit-env.sh or re-open the dkp shell.")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD   is the directory where object files & temporary files are placed
# SOURCES is a list of directories containing source code
# DATA    is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS   is the directory containing data to be packed into RomFS
#
# APP_TITLE, APP_AUTHOR, APP_VERSION, APP_ICON — metadata for the NRO
#---------------------------------------------------------------------------------
TARGET   := ferris-ao-switch
BUILD    := build
SOURCES  := src \
            src/net \
            src/protocol \
            src/state \
            src/assets \
            src/audio \
            src/render \
            src/ui \
            src/ui/screens \
            src/input
DATA     := data
INCLUDES := include src
ROMFS    := romfs

APP_TITLE   := Ferris-AO
APP_AUTHOR  := SyntaxNyah
APP_VERSION := 1.0.0
APP_ICON    := icon.jpg

#---------------------------------------------------------------------------------
# CPU / ABI
#---------------------------------------------------------------------------------
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
CFLAGS   := -Wall -Wextra -O2 -ffunction-sections -pipe \
            $(ARCH) \
            $(DEFINES)

CFLAGS   += $(INCLUDE) -D__SWITCH__

CXXFLAGS := $(CFLAGS) \
            -std=c++17 \
            -fno-exceptions \
            -fno-rtti \
            -DAO_TLS

ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -pipe -Wl,-Map,$(notdir $*.map)

#---------------------------------------------------------------------------------
# Libraries
# Order matters: higher-level libs before lower-level ones
#---------------------------------------------------------------------------------
LIBS := -lSDL2_mixer -lSDL2_ttf -lSDL2_image -lSDL2_net -lSDL2 \
        -lopusfile -lopus -lvorbisidec -logg \
        -lfreetype -lharfbuzz -lbz2 -lpng -lwebpdemux -lwebp -lz \
        -lmbedtls -lmbedx509 -lmbedcrypto \
        -lEGL -lGLESv2 -lglapi -ldrm_nouveau \
        -ljpeg -lmodplug -lmpg123 \
        -lnx -lm

#---------------------------------------------------------------------------------
# List of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX) $(PORTLIBS)/SDL2

#---------------------------------------------------------------------------------
# No need to edit below this line
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD := $(CXX)

export OFILES_BIN   := $(addsuffix .o,$(BINFILES))
export OFILES_SRC   := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES       := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN   := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export BUILD_EXEFS_SRC := $(TOPDIR)/exefs_src

export NROFLAGS += --icon=$(CURDIR)/$(APP_ICON)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

ifneq ($(ROMFS),)
    export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).pfs0 $(TARGET).nso $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
