# USB摄像头UVC探测工具 - Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -std=gnu11

# 目录结构
SRCDIR = src
BUILDDIR = build
TARGET = $(BUILDDIR)/uvc-tool

# 平台检测
UNAME_S := $(shell uname -s)

# Linux 专属源: V4L2 采集 / UVC 扩展单元 / ffplay 预览
# 这三者依赖 linux/videodev2.h 等内核接口, 在 macOS 上不编译。
LINUX_ONLY_SRCS = $(addprefix $(SRCDIR)/, v4l2_cap.c xu_ctrl.c ffplay_ctrl.c)
ALL_SRCS = $(wildcard $(SRCDIR)/*.c)

ifeq ($(UNAME_S),Darwin)
    # === macOS: 仅构建跨平台部分(命令 1 USB描述符 / 命令 11 ESP32兼容) ===
    # readline 与 libusb 为 keg-only, 需显式给出 Homebrew 前缀路径。
    SRCS = $(filter-out $(LINUX_ONLY_SRCS), $(ALL_SRCS))
    BREW_READLINE := $(shell brew --prefix readline 2>/dev/null)
    BREW_LIBUSB := $(shell brew --prefix libusb 2>/dev/null)
    CFLAGS += -I$(BREW_READLINE)/include -I$(BREW_LIBUSB)/include/libusb-1.0
    LDFLAGS = -L$(BREW_READLINE)/lib -L$(BREW_LIBUSB)/lib -lreadline -lusb-1.0
else
    # === Linux: 全量构建 ===
    SRCS = $(ALL_SRCS)
    CFLAGS += $(shell pkg-config --cflags libusb-1.0)
    LDFLAGS = -lreadline -lusb-1.0 -lpthread
endif

OBJS = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))

.PHONY: all clean

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

clean:
	rm -rf $(BUILDDIR) cam.log

# 依赖关系
$(BUILDDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/logger.h $(SRCDIR)/usb_desc.h $(SRCDIR)/esp32_compat.h
$(BUILDDIR)/logger.o: $(SRCDIR)/logger.c $(SRCDIR)/logger.h
$(BUILDDIR)/usb_desc.o: $(SRCDIR)/usb_desc.c $(SRCDIR)/usb_desc.h $(SRCDIR)/logger.h
$(BUILDDIR)/esp32_compat.o: $(SRCDIR)/esp32_compat.c $(SRCDIR)/esp32_compat.h $(SRCDIR)/usb_desc.h $(SRCDIR)/logger.h
# 以下为 Linux 专属目标 (macOS 构建不会触发)
$(BUILDDIR)/v4l2_cap.o: $(SRCDIR)/v4l2_cap.c $(SRCDIR)/v4l2_cap.h $(SRCDIR)/logger.h
$(BUILDDIR)/xu_ctrl.o: $(SRCDIR)/xu_ctrl.c $(SRCDIR)/xu_ctrl.h $(SRCDIR)/logger.h
$(BUILDDIR)/ffplay_ctrl.o: $(SRCDIR)/ffplay_ctrl.c $(SRCDIR)/ffplay_ctrl.h $(SRCDIR)/logger.h
