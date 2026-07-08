#---------------------------------------------------------------------------------
# Binding of Isaac 3DS - Makefile
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>/devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

APP_TITLE       := Binding of Isaac 3DS
APP_DESCRIPTION := A simplified port of The Binding of Isaac
APP_AUTHOR      := homebrew

TARGET      := binding_of_isaac_3ds
BUILD       := build
SOURCES     := source
DATA        :=
INCLUDES    := include
ROMFS       := romfs

LIBDIRS     := $(CTRULIB)

#---------------------------------------------------------------------------------
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS   := -g -Wall -O2 -mword-relocations \
	    -ffunction-sections \
	$(ARCH)

CFLAGS   += $(INCLUDE) -D__3DS__

ifneq ($(strip $(ROMFS)),)
	export ROMFS_DIR := $(CURDIR)/$(ROMFS)
	CFLAGS += -DROMFS
endif

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS     := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
	$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
	$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
	          -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
	        export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
	        ifneq (,$(findstring icon.png,$(icons)))
	                export APP_ICON := $(TOPDIR)/icon.png
	        endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(APP_ICON)),)
	export APP_ICON := $(DEVKITPRO)/libctru/default_icon.png
endif

export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS := --smdh=$(OUTPUT).smdh

ifneq ($(ROMFS_DIR),)
	export _3DSXFLAGS += --romfs=$(ROMFS_DIR)
endif

#---------------------------------------------------------------------------------
# CIA build pipeline (for installable .cia on real 3DS hardware / FBI / Citra)
#---------------------------------------------------------------------------------
# Tool paths - prefer system install, fall back to project-local copies
ifeq ($(OS),Windows_NT)
MAKEROM     ?= $(TOPDIR)/cia_tools/makerom-win/makerom.exe
BANNERTOOL  ?= $(TOPDIR)/cia_tools/bannertool-1.2.0/windows-x86_64/bannertool.exe
else
MAKEROM     ?= $(shell which makerom 2>/dev/null || echo $(TOPDIR)/cia_tools/makerom)
BANNERTOOL  ?= $(shell which bannertool 2>/dev/null || echo $(TOPDIR)/cia_tools/bannertool)
endif

# CIA build inputs / outputs
RSF_FILE        := $(TOPDIR)/app.rsf
BANNER_PNG      := $(TOPDIR)/cia_assets/banner.png
BANNER_AUDIO    := $(TOPDIR)/cia_assets/audio.wav
CIA_ICON_PNG    := $(TOPDIR)/cia_assets/icon.png
BANNER_BNR      := $(TOPDIR)/cia_assets/banner.bnr
CIA_ICON_ICN    := $(TOPDIR)/cia_assets/icon.icn
CIA_OUTPUT      := $(TOPDIR)/$(TARGET).cia

# CIA metadata
CIA_LONG_TITLE  := Binding of Isaac 3DS
CIA_SHORT_TITLE := Isaac 3DS
CIA_PUBLISHER   := homebrew

.PHONY: $(BUILD) clean all cia cia-clean

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# --- CIA target ------------------------------------------------------------
# Builds: elf -> smdh -> banner.bnr -> icon.icn -> .cia
# Uses the RSF that pins SystemMode=64MB / Legacy for O3DS compatibility.
cia: $(CIA_OUTPUT)

$(CIA_OUTPUT): $(BUILD) $(RSF_FILE) $(BANNER_PNG) $(BANNER_AUDIO) $(CIA_ICON_PNG)
	@echo "=== Building CIA for original 3DS (O3DS-safe) ==="
	@if [ ! -f "$(MAKEROM)" ]; then \
	        echo "ERROR: makerom not found at '$(MAKEROM)'"; exit 1; fi
	@if [ ! -f "$(BANNERTOOL)" ]; then \
	        echo "ERROR: bannertool not found at '$(BANNERTOOL)'"; exit 1; fi
	@echo "[bannertool] generating banner.bnr ..."
	@$(BANNERTOOL) makebanner \
	        -i "$(BANNER_PNG)" \
	        -a "$(BANNER_AUDIO)" \
	        -o "$(BANNER_BNR)" >/dev/null
	@echo "[bannertool] generating icon.icn ..."
	@$(BANNERTOOL) makesmdh \
	        -s "$(CIA_SHORT_TITLE)" \
	        -l "$(CIA_LONG_TITLE)" \
	        -p "$(CIA_PUBLISHER)" \
	        -i "$(CIA_ICON_PNG)" \
	        -o "$(CIA_ICON_ICN)" >/dev/null
	@echo "[makerom]   packing CIA (romfs from $(ROMFS_DIR)) ..."
	@# NOTE: no '-exefslogo' here.  Including it on modern CFW (Luma3DS) with
	@# a Logo:Homebrew RSF produces a CIA whose ExeFS logo region confuses
	@# the HOME Menu loader -> "Translation - Page" fault.  sm64-port and
	@# other proven homebrew CIAs omit it; we follow the same recipe.
	@$(MAKEROM) -f cia \
	        -o "$(CIA_OUTPUT)" \
	        -rsf "$(RSF_FILE)" \
	        -target t \
	        -elf "$(OUTPUT).elf" \
	        -icon "$(CIA_ICON_ICN)" \
	        -banner "$(BANNER_BNR)" \
	        -DAPP_ROMFS="$(ROMFS_DIR)"
	@echo "=== CIA built: $(CIA_OUTPUT) ==="
	@ls -lh "$(CIA_OUTPUT)"

cia-clean:
	@echo cia-clean ...
	@rm -f "$(BANNER_BNR)" "$(CIA_ICON_ICN)" "$(CIA_OUTPUT)"

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).cia
	@rm -f "$(BANNER_BNR)" "$(CIA_ICON_ICN)"

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all

all: $(OUTPUT).3dsx

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf: $(OFILES)

$(OUTPUT).smdh: $(TOPDIR)/Makefile
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

%.o: %.c
	@echo $(notdir $<)
	$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@ $(ERROR_FILTER)

%.o: %.s
	@echo $(notdir $<)
	$(AS) -MMD -MP -MF $(DEPSDIR)/$*.d -x assembler-with-cpp $(ASFLAGS) -c $< -o $@

-include $(DEPENDS)

endif
