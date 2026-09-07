#---------------------------------------------------------------------------------
# MusicPlayer2 for Nintendo Switch (homebrew)
#
# 基于 devkitPro 官方 switch 应用模板。构建产物是 MusicPlayer2.nro。
# 需要先安装：devkitA64 + libnx + switch-sdl2 / switch-sdl2_ttf /
#             switch-sdl2_image / switch-sdl2_mixer 及其依赖（见 README.md）。
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error 请先设置 DEVKITPRO 环境变量，例如 export DEVKITPRO=/opt/devkitpro)
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# 应用元信息（写进 NRO 头，Switch 主菜单里显示的就是这些）
#---------------------------------------------------------------------------------
APP_TITLE   := MusicPlayer2
APP_AUTHOR  := zhongyang219 / Switch port
APP_VERSION := 1.0.0

TARGET      := MusicPlayer2
BUILD       := build
SOURCES     := source source/core source/audio source/ui source/input source/net
DATA        := data
INCLUDES    := source
ROMFS       := romfs

# 图标：放一个 256x256 的 JPEG 到 icon.jpg 即可，缺省时用 libnx 自带的默认图标
ifneq ($(wildcard $(TOPDIR)/icon.jpg),)
    APP_ICON := $(TOPDIR)/icon.jpg
endif

#---------------------------------------------------------------------------------
# 编译选项
#---------------------------------------------------------------------------------
ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -Wextra -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS  += $(INCLUDE) -D__SWITCH__

# 刻意保留异常和 RTTI（devkitPro 模板默认关掉）：核心层用了 std::map / std::string /
# std::mutex，关掉异常后这些容器的分配失败会直接 abort，出问题时更难定位
CXXFLAGS := $(CFLAGS) -std=gnu++17

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# 链接顺序很重要：SDL2 的各扩展库排在 SDL2 之前，编解码库再排在扩展库之后。
# 这份列表来自 `pkg-config --static --libs SDL2_mixer SDL2_ttf SDL2_image libcurl`，
# 不是照着常见写法猜的，几个容易踩的点：
#   - devkitPro 的 SDL2_mixer 用 libvorbisidec（Tremor 整数解码），没有 libvorbis/libvorbisfile
#   - SDL2_ttf 2.22 依赖 harfbuzz
#   - 是 libpng16，不是 libpng
#   - curl 依赖 mbedtls 提供 TLS，三个 mbed* 库按 tls -> x509 -> crypto 排列
LIBS := -lSDL2_mixer -lSDL2_ttf -lSDL2_image -lSDL2 \
        -lopusfile -lopus -lvorbisidec -logg -lmpg123 -lmodplug \
        -lwebp -ljpeg -lharfbuzz -lfreetype -lpng16 -lbz2 \
        -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz \
        -lEGL -lglapi -ldrm_nouveau \
        -lnx -lm -lstdc++ -lpthread

LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# 以下为模板样板代码，一般不需要改动
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

# 第三方头文件用 -isystem 引入：libnx 和 SDL 的头在 -Wextra 下会刷屏，
# 而那些警告我们既改不了也不关心。自己的代码仍然是 -I，警告照常报出来。
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-isystem $(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
    icons := $(wildcard *.jpg)
    ifneq (,$(findstring $(TARGET).jpg,$(icons)))
        export APP_ICON := $(TOPDIR)/$(TARGET).jpg
    else
        ifneq (,$(findstring icon.jpg,$(icons)))
            export APP_ICON := $(TOPDIR)/icon.jpg
        endif
    endif
else
    export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
    export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
    export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
    export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

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
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all : $(OUTPUT).nro

$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf : $(OFILES)

$(OFILES_SRC) : $(HFILES_BIN)

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
