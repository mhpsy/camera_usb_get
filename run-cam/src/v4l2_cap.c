/*
 * v4l2_cap.c - V4L2 能力枚举实现
 *
 * === V4L2 API 概述 ===
 *
 * V4L2 是 Linux 内核提供的视频设备抽象层。应用程序通过 ioctl() 系统调用
 * 与驱动通信。主要的 ioctl 命令有：
 *
 * 查询类:
 *   VIDIOC_QUERYCAP     - 查询设备基本能力（驱动名、卡名、能力标志）
 *   VIDIOC_ENUM_FMT     - 枚举支持的像素格式（MJPEG, YUYV...）
 *   VIDIOC_ENUM_FRAMESIZES  - 枚举某格式下支持的分辨率
 *   VIDIOC_ENUM_FRAMEINTERVALS - 枚举某分辨率下支持的帧率
 *   VIDIOC_QUERYCTRL    - 查询控制项信息（范围、步长、默认值）
 *   VIDIOC_QUERYMENU    - 查询菜单类型控制项的选项
 *
 * 控制类:
 *   VIDIOC_G_CTRL       - 获取控制项当前值
 *   VIDIOC_S_CTRL       - 设置控制项的值
 *   VIDIOC_G_FMT        - 获取当前格式
 *   VIDIOC_S_FMT        - 设置格式（分辨率、像素格式）
 *   VIDIOC_G_PARM       - 获取流参数（帧率等）
 *   VIDIOC_S_PARM       - 设置流参数
 */

#include "v4l2_cap.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

/* 安全的ioctl调用，自动处理EINTR中断 */
static int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

/* 将fourcc转为可读字符串 */
static void fourcc_to_str(uint32_t fourcc, char *buf)
{
    buf[0] = (fourcc >> 0) & 0xFF;
    buf[1] = (fourcc >> 8) & 0xFF;
    buf[2] = (fourcc >> 16) & 0xFF;
    buf[3] = (fourcc >> 24) & 0xFF;
    buf[4] = '\0';
}

/* 将V4L2控制类型转为可读字符串 */
static const char *ctrl_type_name(int type)
{
    switch (type) {
    case V4L2_CTRL_TYPE_INTEGER:      return "整数(int)";
    case V4L2_CTRL_TYPE_BOOLEAN:      return "布尔(bool)";
    case V4L2_CTRL_TYPE_MENU:         return "菜单(menu)";
    case V4L2_CTRL_TYPE_BUTTON:       return "按钮(button)";
    case V4L2_CTRL_TYPE_INTEGER64:    return "64位整数";
    case V4L2_CTRL_TYPE_CTRL_CLASS:   return "控制类别";
    case V4L2_CTRL_TYPE_STRING:       return "字符串";
    case V4L2_CTRL_TYPE_BITMASK:      return "位掩码";
    case V4L2_CTRL_TYPE_INTEGER_MENU: return "整数菜单";
    default:                          return "未知";
    }
}

/* 将V4L2能力标志解析为字符串 */
static void print_capabilities(uint32_t caps)
{
    LOG_I("  设备能力标志 (capabilities=0x%08x):", caps);
    if (caps & V4L2_CAP_VIDEO_CAPTURE)     LOG_I("    [✓] VIDEO_CAPTURE    (视频捕获, 单平面)");
    if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) LOG_I("    [✓] VIDEO_CAPTURE_MPLANE (视频捕获, 多平面)");
    if (caps & V4L2_CAP_VIDEO_OUTPUT)      LOG_I("    [✓] VIDEO_OUTPUT     (视频输出)");
    if (caps & V4L2_CAP_STREAMING)         LOG_I("    [✓] STREAMING        (支持流式I/O, 即mmap/userptr)");
    if (caps & V4L2_CAP_READWRITE)         LOG_I("    [✓] READWRITE        (支持read/write I/O)");
    if (caps & V4L2_CAP_META_CAPTURE)      LOG_I("    [✓] META_CAPTURE     (元数据捕获)");
    if (caps & V4L2_CAP_EXT_PIX_FORMAT)    LOG_I("    [✓] EXT_PIX_FORMAT   (扩展像素格式)");
    if (caps & V4L2_CAP_DEVICE_CAPS)       LOG_I("    [✓] DEVICE_CAPS      (有设备级能力)");
}

int v4l2_enumerate_all(const char *dev_path, v4l2_cap_info_t *info)
{
    memset(info, 0, sizeof(*info));

    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_E("无法打开设备 %s: %s", dev_path, strerror(errno));
        return -1;
    }

    LOG_I("成功打开设备: %s (fd=%d)", dev_path, fd);

    /* ===== 1. 查询基本能力 (VIDIOC_QUERYCAP) ===== */
    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_E("VIDIOC_QUERYCAP 失败: %s", strerror(errno));
        close(fd);
        return -1;
    }

    LOG_I("");
    LOG_I("╔══════════════════════════════════════════════════════════════╗");
    LOG_I("║               V4L2 设备能力 (QUERYCAP)                      ║");
    LOG_I("╚══════════════════════════════════════════════════════════════╝");
    LOG_I("  驱动名(driver)   = %s", cap.driver);
    LOG_I("  设备名(card)     = %s", cap.card);
    LOG_I("  总线信息(bus)    = %s", cap.bus_info);
    LOG_I("  驱动版本         = %u.%u.%u",
          (cap.version >> 16) & 0xFF,
          (cap.version >> 8) & 0xFF,
          cap.version & 0xFF);

    print_capabilities(cap.capabilities);

    /* 如果设备报告了 DEVICE_CAPS，也打印设备级能力 */
    if (cap.capabilities & V4L2_CAP_DEVICE_CAPS) {
        LOG_I("  设备级能力 (device_caps=0x%08x):", cap.device_caps);
        print_capabilities(cap.device_caps);
    }

    strncpy(info->driver, (char *)cap.driver, sizeof(info->driver) - 1);
    strncpy(info->card, (char *)cap.card, sizeof(info->card) - 1);
    strncpy(info->bus_info, (char *)cap.bus_info, sizeof(info->bus_info) - 1);
    info->capabilities = cap.capabilities;

    /* ===== 2. 枚举所有格式 ===== */
    LOG_I("");
    LOG_I("╔══════════════════════════════════════════════════════════════╗");
    LOG_I("║                 枚举视频格式+分辨率+帧率                      ║");
    LOG_I("╚══════════════════════════════════════════════════════════════╝");

    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        char fcc[5];
        fourcc_to_str(fmtdesc.pixelformat, fcc);

        LOG_I("");
        LOG_I("  格式 #%u: '%s' (%s)", fmtdesc.index, fcc, fmtdesc.description);
        LOG_I("    flags=0x%x %s", fmtdesc.flags,
              (fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED) ? "[压缩格式]" : "[未压缩]");

        if (info->format_count < 16) {
            format_info_t *fi = &info->formats[info->format_count];
            fi->pixelformat = fmtdesc.pixelformat;
            strncpy(fi->description, (char *)fmtdesc.description, sizeof(fi->description) - 1);

            /* 枚举该格式下所有分辨率 */
            struct v4l2_frmsizeenum frmsize;
            memset(&frmsize, 0, sizeof(frmsize));
            frmsize.pixel_format = fmtdesc.pixelformat;

            while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
                if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    uint32_t w = frmsize.discrete.width;
                    uint32_t h = frmsize.discrete.height;

                    LOG_I("    分辨率: %ux%u", w, h);

                    if (fi->frame_count < 32) {
                        frame_size_t *fs = &fi->frames[fi->frame_count];
                        fs->width = w;
                        fs->height = h;

                        /* 枚举该分辨率下所有帧率 */
                        struct v4l2_frmivalenum frmival;
                        memset(&frmival, 0, sizeof(frmival));
                        frmival.pixel_format = fmtdesc.pixelformat;
                        frmival.width = w;
                        frmival.height = h;

                        while (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) == 0) {
                            if (frmival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                                double fps = (double)frmival.discrete.denominator /
                                             frmival.discrete.numerator;
                                LOG_I("      帧率: %u/%u (%.1f fps)",
                                      frmival.discrete.denominator,
                                      frmival.discrete.numerator, fps);

                                if (fs->interval_count < 16) {
                                    fs->intervals[fs->interval_count].numerator =
                                        frmival.discrete.numerator;
                                    fs->intervals[fs->interval_count].denominator =
                                        frmival.discrete.denominator;
                                    fs->interval_count++;
                                }
                            }
                            frmival.index++;
                        }
                        fi->frame_count++;
                    }
                } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
                    LOG_I("    分辨率: 步进范围 %u-%u x %u-%u (步进 %u x %u)",
                          frmsize.stepwise.min_width, frmsize.stepwise.max_width,
                          frmsize.stepwise.min_height, frmsize.stepwise.max_height,
                          frmsize.stepwise.step_width, frmsize.stepwise.step_height);
                }
                frmsize.index++;
            }
            info->format_count++;
        }

        fmtdesc.index++;
    }

    /* ===== 3. 枚举所有控制项 ===== */
    LOG_I("");
    LOG_I("╔══════════════════════════════════════════════════════════════╗");
    LOG_I("║                   枚举所有控制项                             ║");
    LOG_I("╚══════════════════════════════════════════════════════════════╝");

    /*
     * V4L2控制项ID按组划分：
     * 0x00980900 ~ : 用户控制 (亮度、对比度等基础图像参数)
     * 0x009A0900 ~ : 摄像头控制 (曝光、自动对焦等摄像头特有参数)
     * 0x009B0900 ~ : FM调制器控制
     * 0x009C0900 ~ : 闪光灯控制
     * 0x009D0900 ~ : JPEG压缩控制
     * 0x009E0900 ~ : 图像源控制
     * 0x009F0900 ~ : 图像处理控制
     *
     * 我们使用 V4L2_CTRL_FLAG_NEXT_CTRL 标志来遍历所有控制项，
     * 包括标准控制和私有控制。
     */
    struct v4l2_queryctrl qctrl;
    memset(&qctrl, 0, sizeof(qctrl));
    qctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (xioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
        /* 跳过禁用的控制项 */
        if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
            qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
            continue;
        }

        /* 控制类别头（分组标记，不是实际控制项） */
        if (qctrl.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
            LOG_I("");
            LOG_I("  ── %s ──", qctrl.name);
            qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
            continue;
        }

        /* 获取当前值 */
        struct v4l2_control ctrl;
        ctrl.id = qctrl.id;
        ctrl.value = 0;
        xioctl(fd, VIDIOC_G_CTRL, &ctrl);

        LOG_I("  [0x%08x] %-30s", qctrl.id, qctrl.name);
        LOG_I("    类型: %s | 范围: [%d ~ %d] 步长=%d | 默认=%d | 当前=%d",
              ctrl_type_name(qctrl.type),
              qctrl.minimum, qctrl.maximum, qctrl.step,
              qctrl.default_value, ctrl.value);

        /* 标志信息 */
        if (qctrl.flags) {
            LOG_I("    标志: 0x%x%s%s%s%s",
                  qctrl.flags,
                  (qctrl.flags & V4L2_CTRL_FLAG_INACTIVE) ? " [禁用:依赖其他控制]" : "",
                  (qctrl.flags & V4L2_CTRL_FLAG_READ_ONLY) ? " [只读]" : "",
                  (qctrl.flags & V4L2_CTRL_FLAG_VOLATILE) ? " [易变:值随时可能改变]" : "",
                  (qctrl.flags & V4L2_CTRL_FLAG_WRITE_ONLY) ? " [只写]" : "");
        }

        /* 如果是菜单类型，列出所有选项 */
        if (qctrl.type == V4L2_CTRL_TYPE_MENU) {
            struct v4l2_querymenu qmenu;
            memset(&qmenu, 0, sizeof(qmenu));
            qmenu.id = qctrl.id;
            for (qmenu.index = (uint32_t)qctrl.minimum;
                 (int32_t)qmenu.index <= qctrl.maximum;
                 qmenu.index++) {
                if (xioctl(fd, VIDIOC_QUERYMENU, &qmenu) == 0) {
                    LOG_I("      [%u] %s%s", qmenu.index, qmenu.name,
                          (int32_t)qmenu.index == ctrl.value ? " ← 当前选择" : "");
                }
            }
        }

        /* 保存控制项信息 */
        if (info->ctrl_count < 64) {
            ctrl_info_t *ci = &info->ctrls[info->ctrl_count++];
            ci->id = qctrl.id;
            strncpy(ci->name, (char *)qctrl.name, sizeof(ci->name) - 1);
            ci->type = qctrl.type;
            ci->minimum = qctrl.minimum;
            ci->maximum = qctrl.maximum;
            ci->step = qctrl.step;
            ci->default_value = qctrl.default_value;
            ci->current_value = ctrl.value;
            ci->flags = qctrl.flags;
        }

        qctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    /* ===== 4. 当前格式信息 ===== */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        char fcc[5];
        fourcc_to_str(fmt.fmt.pix.pixelformat, fcc);

        LOG_I("");
        LOG_I("╔══════════════════════════════════════════════════════════════╗");
        LOG_I("║                   当前视频格式                               ║");
        LOG_I("╚══════════════════════════════════════════════════════════════╝");
        LOG_I("  分辨率          = %ux%u", fmt.fmt.pix.width, fmt.fmt.pix.height);
        LOG_I("  像素格式        = '%s'", fcc);
        LOG_I("  每行字节        = %u", fmt.fmt.pix.bytesperline);
        LOG_I("  图像大小        = %u 字节", fmt.fmt.pix.sizeimage);
        LOG_I("  色彩空间        = %u", fmt.fmt.pix.colorspace);
    }

    /* ===== 5. 当前帧率 ===== */
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_PARM, &parm) == 0) {
        LOG_I("  当前帧率        = %u/%u (%.1f fps)",
              parm.parm.capture.timeperframe.denominator,
              parm.parm.capture.timeperframe.numerator,
              (double)parm.parm.capture.timeperframe.denominator /
              parm.parm.capture.timeperframe.numerator);
    }

    close(fd);
    LOG_I("V4L2枚举完成");
    return 0;
}

void v4l2_print_formats(const v4l2_cap_info_t *info)
{
    LOG_I("=== 可选格式/分辨率/帧率列表 ===");
    int idx = 1;
    for (int i = 0; i < info->format_count; i++) {
        const format_info_t *fi = &info->formats[i];
        char fcc[5];
        fourcc_to_str(fi->pixelformat, fcc);

        for (int j = 0; j < fi->frame_count; j++) {
            const frame_size_t *fs = &fi->frames[j];
            for (int k = 0; k < fs->interval_count; k++) {
                double fps = (double)fs->intervals[k].denominator /
                             fs->intervals[k].numerator;
                printf("  [%2d] %s %ux%u @ %.0f fps\n",
                       idx++, fcc, fs->width, fs->height, fps);
            }
        }
    }
}

void v4l2_print_controls(const v4l2_cap_info_t *info)
{
    printf("\n=== 控制项列表 ===\n");
    for (int i = 0; i < info->ctrl_count; i++) {
        const ctrl_info_t *ci = &info->ctrls[i];
        printf("  [%2d] %-28s 当前=%-6d 范围=[%d ~ %d] 步长=%d 默认=%d  (ID=0x%08x)\n",
               i + 1, ci->name, ci->current_value,
               ci->minimum, ci->maximum, ci->step, ci->default_value, ci->id);
    }
}

int v4l2_set_control(const char *dev_path, uint32_t ctrl_id, int32_t value)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_E("打开设备失败: %s", strerror(errno));
        return -1;
    }

    struct v4l2_control ctrl;
    ctrl.id = ctrl_id;
    ctrl.value = value;

    if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        LOG_E("设置控制项 0x%08x = %d 失败: %s", ctrl_id, value, strerror(errno));
        close(fd);
        return -1;
    }

    /* 回读确认 */
    ctrl.id = ctrl_id;
    xioctl(fd, VIDIOC_G_CTRL, &ctrl);
    LOG_I("控制项 0x%08x 设置成功, 实际值=%d", ctrl_id, ctrl.value);

    close(fd);
    return 0;
}

int v4l2_get_control(const char *dev_path, uint32_t ctrl_id, int32_t *value)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_E("打开设备失败: %s", strerror(errno));
        return -1;
    }

    struct v4l2_control ctrl;
    ctrl.id = ctrl_id;

    if (xioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        LOG_E("读取控制项 0x%08x 失败: %s", ctrl_id, strerror(errno));
        close(fd);
        return -1;
    }

    *value = ctrl.value;
    close(fd);
    return 0;
}
