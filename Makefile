#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

rwildcard = $(foreach d, $(wildcard $1*), \
            $(filter $(subst *, %, $2), $d) \
            $(call rwildcard, $d/, $2))

define unique =
  $(eval seen :=)
  $(foreach _,$1,$(if $(filter $_,${seen}),,$(eval seen += $_)))
  ${seen}
endef
#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   files as if they were data files.
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCE		:=	source
DATA		:=	data
INCLUDES	:=	source
GRAPHICS	:=	gfx
ROMFS		:=	romfs
LIBRARY := library
GFXBUILD	:=	$(ROMFS)/gfx
#---------------------------------------------------------------------------------
APP_VER					:= 82
APP_TITLE				:= ThirdTube
APP_DESCRIPTION				:= A YouTube client for the new 3DS
APP_AUTHOR				:= windows_server_2003
PRODUCT_CODE				:= CTR-TYT
UNIQUE_ID				:= 0xBF74D

BANNER_AUDIO				:= resource/banner.wav
BANNER_IMAGE				:= resource/banner.png
ICON        				:= resource/icon.png
RSF_PATH				:= resource/app.rsf

#---------------------------------------------------------------------------------
#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:= -Wall -Wextra -Wno-unused -Wno-psabi -O2 -mword-relocations \
		-fomit-frame-pointer -ffunction-sections -fdata-sections \
		$(ARCH)

CFLAGS	+=	$(INCLUDE) -DARM11 -D__3DS__ -DCURL_STATICLIB

CXXFLAGS	:= $(CFLAGS) -fno-exceptions -std=gnu++14

ASFLAGS	:= $(ARCH)
LDFLAGS	=	-specs=3dsx.specs $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lavfilter -lswresample -lavformat -lswscale -lavcodec -lavutil -lcurl -lbrotli -lnghttp2 -lmbedtls -lmbedx509 -lmbedcrypto -lz  -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS := library/FFmpeg library/libctru library/libbrotli library/libcurl library/nghttp2
LIBDIRS_DEVKITPRO := libctru portlibs/3ds

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:= $(CURDIR)/$(SOURCE)

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(patsubst $(SOURCE)/%, %, $(call rwildcard, $(SOURCE), *.c))
CPPFILES	:=	$(patsubst $(SOURCE)/%, %, $(call rwildcard, $(SOURCE), *.cpp))
SFILES		:=	$(patsubst $(SOURCE)/%, %, $(call rwildcard, $(SOURCE), *.s))
PICAFILES	:=	$(patsubst $(SOURCE)/%, %, $(call rwildcard, $(SOURCE), *.v.pica))
SHLISTFILES	:=	$(patsubst $(SOURCE)/%, %, $(call rwildcard, $(SOURCE), *.shlist))
GFXFILES	:=	$(call rwildcard, $(GRAPHICS), *.t3s)
BINFILES	:=	$(call rwildcard, $(DATA), *.*)



#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
ifeq ($(GFXBUILD),$(BUILD))
#---------------------------------------------------------------------------------
export T3XFILES :=  $(GFXFILES:.t3s=.t3x)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
export ROMFS_T3XFILES	:=	$(patsubst %.t3s, $(GFXBUILD)/%.t3x, $(GFXFILES))
export T3XHFILES		:=	$(patsubst %.t3s, $(BUILD)/%.h, $(GFXFILES))
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES 	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OBJDIRS			:=	$(call unique, $(patsubst %, $(BUILD)/%, $(dir $(OFILES_SOURCES))))

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(addsuffix .o,$(T3XFILES))

export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=_shbin.h) \
			$(addsuffix .h,$(subst .,_,$(BINFILES))) \
			$(GFXFILES:.t3s=.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(CURDIR)/$(dir)/include) \
			$(foreach dir,$(LIBDIRS_DEVKITPRO),-I$(DEVKITPRO)/$(dir)/include) \
			-I$(CURDIR)/$(LIBRARY) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(CURDIR)/$(dir)/lib) $(foreach dir,$(LIBDIRS_DEVKITPRO),-L$(DEVKITPRO)/$(dir)/lib)

export _3DSXDEPS	:=	$(if $(NO_SMDH),,$(OUTPUT).smdh)

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

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

#---------------------------------------------------------------------------------
MAKEROM      ?= resource/makerom
MAKEROM_WIN      ?= resource/makerom.exe
MAKEROM_ARGS := -elf "$(OUTPUT).elf" -rsf "$(RSF_PATH)" -banner "$(BUILD)/banner.bnr" -icon "$(BUILD)/icon.icn" -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(PRODUCT_CODE)" -DAPP_UNIQUE_ID="$(UNIQUE_ID)"

ifneq ($(strip $(LOGO)),)
	MAKEROM_ARGS	+=	 -logo "$(LOGO)"
endif
ifneq ($(strip $(ROMFS)),)
	MAKEROM_ARGS	+=	 -DAPP_ROMFS="$(ROMFS)"
endif

BANNERTOOL   ?= resource/bannertool
BANNERTOOL_WIN   ?= resource/bannertool.exe

ifeq ($(suffix $(BANNER_IMAGE)),.cgfx)
	BANNER_IMAGE_ARG := -ci
else
	BANNER_IMAGE_ARG := -i
endif

ifeq ($(suffix $(BANNER_AUDIO)),.cwav)
	BANNER_AUDIO_ARG := -ca
else
	BANNER_AUDIO_ARG := -a
endif
#

#---------------------------------------------------------------------------------

all: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES) $(OBJDIRS)
	@echo Building 3dsx...
	@$(MAKE) -j10 -s --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@echo
	@echo Building cia...
	@$(BANNERTOOL) makebanner $(BANNER_IMAGE_ARG) $(BANNER_IMAGE) $(BANNER_AUDIO_ARG) $(BANNER_AUDIO) -o $(BUILD)/banner.bnr
	@$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p $(APP_AUTHOR) -i $(APP_ICON) -o $(BUILD)/icon.icn
	@$(MAKEROM) -f cia -o $(OUTPUT).cia -target t -exefslogo $(MAKEROM_ARGS) -ver $(APP_VER)

all_win: $(BUILD) $(GFXBUILD) $(DEPSDIR) $(ROMFS_T3XFILES) $(T3XHFILES) $(OBJDIRS)
	@echo Building 3dsx...
	@$(MAKE) -j10 -s --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@echo
	@echo Building cia...
	@$(BANNERTOOL_WIN) makebanner $(BANNER_IMAGE_ARG) $(BANNER_IMAGE) $(BANNER_AUDIO_ARG) $(BANNER_AUDIO) -o $(BUILD)/banner.bnr
	@$(BANNERTOOL_WIN) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p $(APP_AUTHOR) -i $(APP_ICON) -o $(BUILD)/icon.icn
	@$(MAKEROM_WIN) -f cia -o $(OUTPUT).cia -target t -exefslogo $(MAKEROM_ARGS) -ver $(APP_VER)

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

ifneq ($(DEPSDIR),$(BUILD))
$(DEPSDIR):
	@mkdir -p $@
endif

$(OBJDIRS)	:
	@mkdir -p $@

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

#---------------------------------------------------------------------------------
$(GFXBUILD)/%.t3x	$(BUILD)/%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $(BUILD)/$*.h -d $(DEPSDIR)/$*.d -o $(GFXBUILD)/$*.t3x

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf	:	$(OFILES)


#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
.PRECIOUS	:	%.t3x
#---------------------------------------------------------------------------------
%.t3x.o	%_t3x.h :	%.t3x
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DEPSFILE := $(DEPSDIR)/$*.shbin.d)
	echo "$(CURBIN).o: $< $1" > $(DEPSFILE)
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
endef

%.shbin.o %_shbin.h : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x	%.h	:	%.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $*.d -o $*.t3x

-include $(call rwildcard, $(DEPSDIR), *.d)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
