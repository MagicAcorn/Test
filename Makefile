#---------------------------------------------------------------------------------
# Hearthvale - GameCube build (devkitPPC + libogc)
#   make            -> hearthvale.dol (GameCube; runs on Wii via Nintendont/Swiss/Dolphin)
#   make wii        -> hearthvale_wii.dol (Wii homebrew channel build, same game)
#   make assets     -> rebuild data/assets.pak (needs python3 + numpy + pillow)
#   make pc         -> PC test harness (gxemu) in pc/
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

ifeq ($(PLATFORM),wii)
include $(DEVKITPPC)/wii_rules
TARGET		:=	hearthvale_wii
BUILD		:=	build_wii
else
include $(DEVKITPPC)/gamecube_rules
TARGET		:=	hearthvale
BUILD		:=	build
endif

SOURCES		:=	source source/core source/gfx source/game source/audio source/ui source/platform/gc
DATA		:=	data
INCLUDES	:=	source

CFLAGS		=	-O2 -Wall -Wno-missing-braces $(MACHDEP) $(INCLUDE) -ffast-math -fno-strict-aliasing
CXXFLAGS	=	$(CFLAGS) -std=gnu++17 -fno-exceptions -fno-rtti
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS		:=	-laesnd -logc -lm
LIBDIRS		:=

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.pak)))

export LD	:=	$(CXX)
export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) -I$(LIBOGC_INC)
export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean wii assets pc run-pc

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

wii:
	@$(MAKE) --no-print-directory PLATFORM=wii

assets:
	python3 tools/build_assets.py

pc:
	@$(MAKE) --no-print-directory -C pc

clean:
	@echo clean ...
	@rm -fr build build_wii hearthvale.elf hearthvale.dol hearthvale_wii.elf hearthvale_wii.dol *.map
	@$(MAKE) --no-print-directory -C pc clean

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES) : $(HFILES)

%.pak.o %_pak.h : %.pak
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

endif
