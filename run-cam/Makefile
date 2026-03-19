# USB摄像头UVC探测工具 - Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -std=gnu11
LDFLAGS = -lreadline -lusb-1.0 -lpthread

# pkg-config 获取头文件路径
CFLAGS += $(shell pkg-config --cflags libusb-1.0)

# 目录结构
SRCDIR = src
BUILDDIR = build

TARGET = $(BUILDDIR)/uvc-tool
SRCS = $(wildcard $(SRCDIR)/*.c)
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
$(BUILDDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/logger.h $(SRCDIR)/usb_desc.h $(SRCDIR)/v4l2_cap.h $(SRCDIR)/xu_ctrl.h $(SRCDIR)/ffplay_ctrl.h
$(BUILDDIR)/logger.o: $(SRCDIR)/logger.c $(SRCDIR)/logger.h
$(BUILDDIR)/usb_desc.o: $(SRCDIR)/usb_desc.c $(SRCDIR)/usb_desc.h $(SRCDIR)/logger.h
$(BUILDDIR)/v4l2_cap.o: $(SRCDIR)/v4l2_cap.c $(SRCDIR)/v4l2_cap.h $(SRCDIR)/logger.h
$(BUILDDIR)/xu_ctrl.o: $(SRCDIR)/xu_ctrl.c $(SRCDIR)/xu_ctrl.h $(SRCDIR)/logger.h
$(BUILDDIR)/ffplay_ctrl.o: $(SRCDIR)/ffplay_ctrl.c $(SRCDIR)/ffplay_ctrl.h $(SRCDIR)/logger.h
