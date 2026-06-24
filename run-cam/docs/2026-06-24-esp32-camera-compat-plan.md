# ESP32 摄像头兼容性判定 实现计划

> **For agentic workers:** 用 superpowers:subagent-driven-development 或 executing-plans 按任务执行。本项目无单测框架,每个任务的验证关口是"`make` 干净编译通过";真机功能测试是最后一步,由主会话执行。

**Goal:** 给 run-cam 加"功能 11:判断 USB 摄像头能否用于鱼缸 ESP32",纯 libusb 判定。

**Architecture:** 扩展 `usb_desc` 从 USB 描述符多捕获(设备速度 + VS 流接口端点 + 格式/帧);
新增 `esp32_compat` 模块用纯函数套用 ESP32 规则产出分项报告;`main.c` 改为运行时目标(自动检测/手动指定)并加命令 11。

**Tech Stack:** C(gnu11)、libusb-1.0、readline。构建:`make`(`build/uvc-tool`)。

## Global Constraints

- 设计依据 spec:`run-cam/docs/2026-06-24-esp32-camera-compat-design.md`(所有判据出处见 spec §1.1/§6)。
- 目标常量出处必须以注释标注(对应 fish-tank-esp32 的 `CONFIG_FISHTANK_UVC_*` 与 `hub.c:344-351`)。
- 判定核心 `esp32_compat_check()` 必须是纯函数:输入 `const` 结构体,输出 report,**不做任何 I/O**。
- 不依赖 V4L2 / `/dev/video` / uvcvideo。
- 代码风格随现有文件(中文注释、`LOG_I/LOG_D/LOG_W/LOG_E`、4 空格缩进、`.clang-format`)。

---

### Task 1: 扩展 `usb_desc` 数据结构与描述符捕获

**Files:**
- Modify: `run-cam/src/usb_desc.h`(加结构体,扩展 `usb_desc_info_t`)
- Modify: `run-cam/src/usb_desc.c`(端点循环捕获 + `parse_vs_descriptor` 捕获格式/帧 + 设备速度)

**Interfaces — Produces(后续任务依赖):**
- `usb_desc_info_t` 新增字段:`int usb_speed; vs_stream_info_t vs; int format_count; desc_format_t formats[16];`
- 新结构体:`vs_altsetting_t`、`vs_stream_info_t`、`desc_frame_t`、`desc_format_t`(定义见下)。

- [ ] **Step 1: 在 `usb_desc.h` 加结构体定义**(放在 `xu_info_t` 之后、`usb_desc_info_t` 之前)

```c
/* 一个视频流(VS)altsetting 的端点摘要 */
typedef struct {
    uint8_t  alt_setting;   /* bAlternateSetting */
    uint8_t  num_endpoints; /* 该 alt 端点数 —— UVC 要求 streaming alt 恰好 1 */
    uint8_t  ep_address;    /* 第一个端点地址 */
    uint8_t  transfer_type; /* bmAttributes&0x3: 0控制 1等时isoc 2批量bulk 3中断 */
    uint16_t mps;           /* wMaxPacketSize 基础值(低 11 位) */
    uint8_t  mult;          /* 高带宽乘子(HS isoc;每微帧 mult+1 个事务) */
} vs_altsetting_t;

typedef struct {
    int             present;          /* 是否找到 VS(0x0e/0x02)接口 */
    uint8_t         interface_number;
    int             alt_count;
    vs_altsetting_t alts[16];
} vs_stream_info_t;

/* 一种格式下的一个分辨率(含帧率与最大码率) */
typedef struct {
    uint16_t width, height;
    uint32_t dwMaxBitRate;  /* bps,来自 FRAME 描述符,用于带宽估算 */
    int      interval_count;
    double   fps[16];       /* 由 dwFrameInterval(100ns)换算: 1e7/interval */
} desc_frame_t;

typedef struct {
    int          is_mjpeg;  /* MJPEG=1,未压缩=0 */
    char         fourcc[5]; /* "MJPG" 或未压缩 GUID 前 4 字符 */
    int          frame_count;
    desc_frame_t frames[32];
} desc_format_t;
```

- [ ] **Step 2: 扩展 `usb_desc_info_t`**(`usb_desc.h`)

```c
typedef struct {
    int              xu_count;
    xu_info_t        xus[MAX_XU_COUNT];
    int              usb_speed;     /* libusb_get_device_speed() 结果 */
    vs_stream_info_t vs;            /* 新增:流接口端点 */
    int              format_count;  /* 新增 */
    desc_format_t    formats[16];   /* 新增:从 VS FORMAT/FRAME 描述符捕获 */
} usb_desc_info_t;
```

- [ ] **Step 3: `parse_vs_descriptor` 改为接收 `info` 并捕获格式/帧**(`usb_desc.c`)

把函数签名从 `static void parse_vs_descriptor(const unsigned char *buf, int len)`
改为 `static void parse_vs_descriptor(const unsigned char *buf, int len, usb_desc_info_t *info)`,
并更新 `usb_desc.c:722` 处调用为 `parse_vs_descriptor(p, bLen, info);`。

在 `UVC_VS_FORMAT_MJPEG` 分支末尾(现有 LOG 之后)新增:开一个新格式条目。
```c
        if (info && info->format_count < 16) {
            desc_format_t *f = &info->formats[info->format_count++];
            f->is_mjpeg = 1;
            memcpy(f->fourcc, "MJPG", 5);
            f->frame_count = 0;
        }
```
在 `UVC_VS_FORMAT_UNCOMPRESSED` 分支末尾新增(用已解析的 `fmt_guid` 前 4 字符):
```c
        if (info && info->format_count < 16) {
            desc_format_t *f = &info->formats[info->format_count++];
            f->is_mjpeg = 0;
            memcpy(f->fourcc, &buf[5], 4); /* GUID 前 4 字节是可读 fourcc, 如 YUY2 */
            f->fourcc[4] = '\0';
            f->frame_count = 0;
        }
```
在合并的 `UVC_VS_FRAME_MJPEG / UVC_VS_FRAME_UNCOMPRESSED` 分支末尾(现有 LOG 之后),
把该帧追加到"当前格式"(即最后一个格式条目):
```c
        if (info && info->format_count > 0) {
            desc_format_t *f = &info->formats[info->format_count - 1];
            if (f->frame_count < 32) {
                desc_frame_t *fr = &f->frames[f->frame_count++];
                fr->width  = wWidth;
                fr->height = wHeight;
                fr->dwMaxBitRate   = dwMaxBitRate;
                fr->interval_count = 0;
                if (bFrameIntervalType == 0) {
                    /* 连续区间: 记录 min/max 两个端点的 fps(若已解析) */
                    if (len >= 38) {
                        uint32_t dwMinFI = buf[26] | (buf[27]<<8) | (buf[28]<<16) | (buf[29]<<24);
                        uint32_t dwMaxFI = buf[30] | (buf[31]<<8) | (buf[32]<<16) | (buf[33]<<24);
                        if (dwMaxFI) fr->fps[fr->interval_count++] = 1e7 / dwMaxFI; /* 最低 fps */
                        if (dwMinFI) fr->fps[fr->interval_count++] = 1e7 / dwMinFI; /* 最高 fps */
                    }
                } else {
                    for (int n = 0; n < bFrameIntervalType && fr->interval_count < 16; n++) {
                        int off = 26 + n * 4;
                        if (off + 3 < len) {
                            uint32_t fi = buf[off] | (buf[off+1]<<8) | (buf[off+2]<<16) | (buf[off+3]<<24);
                            if (fi) fr->fps[fr->interval_count++] = 1e7 / fi;
                        }
                    }
                }
            }
        }
```

- [ ] **Step 4: 端点循环捕获 VS altsetting 端点**(`usb_desc.c`,接口/altsetting 遍历内)

在 `altsetting` 处理处,当 `altsetting->bInterfaceSubClass == 2`(VS)时,记录该 alt 的端点。
最简洁做法:在端点打印循环(`usb_desc.c:734-746`)之前/之后,加入:
```c
            /* 捕获视频流接口的 altsetting 端点信息(供 ESP32 兼容判定) */
            if (info && altsetting->bInterfaceSubClass == 2) {
                info->vs.present = 1;
                info->vs.interface_number = altsetting->bInterfaceNumber;
                if (altsetting->bNumEndpoints > 0 && info->vs.alt_count < 16) {
                    const struct libusb_endpoint_descriptor *e0 = &altsetting->endpoint[0];
                    vs_altsetting_t *a = &info->vs.alts[info->vs.alt_count++];
                    a->alt_setting   = altsetting->bAlternateSetting;
                    a->num_endpoints = altsetting->bNumEndpoints;
                    a->ep_address    = e0->bEndpointAddress;
                    a->transfer_type = e0->bmAttributes & 0x03;
                    a->mps  = e0->wMaxPacketSize & 0x07FF;
                    a->mult = (e0->wMaxPacketSize >> 11) & 0x03;
                }
            }
```

- [ ] **Step 5: 捕获设备速度**(`usb_desc.c`,`usb_desc_dump` 找到 `target` 后)

在拿到 `target` 之后(`libusb_get_device_descriptor(target, &dev_desc)` 附近)加入:
```c
    if (info) info->usb_speed = libusb_get_device_speed(target);
```
(`libusb_get_device_speed` 返回 `enum libusb_speed`:`LIBUSB_SPEED_LOW/FULL/HIGH/SUPER/SUPER_PLUS`。)

- [ ] **Step 6: 编译验证**

Run: `cd /home/mhpsy/code/temp/camera_usb_get/run-cam && make clean && make 2>&1 | tail -20`
Expected: 干净编译,生成 `build/uvc-tool`,无 error/warning。

- [ ] **Step 7: 提交**

```bash
cd /home/mhpsy/code/temp/camera_usb_get
git add run-cam/src/usb_desc.h run-cam/src/usb_desc.c
git commit -m "feat(run-cam): capture device speed, VS endpoints and formats from USB descriptors"
```

---

### Task 2: 摄像头自动检测函数

**Files:**
- Modify: `run-cam/src/usb_desc.h`(声明 `usb_find_cameras`)
- Modify: `run-cam/src/usb_desc.c`(实现)

**Interfaces — Produces:**
- `typedef struct { uint16_t vid, pid; char product[64]; int is_uvc; } usb_cam_cand_t;`
- `int usb_find_cameras(usb_cam_cand_t *out, int max);` 返回找到的候选数(≤max),
  扫描所有 USB 设备,凡含 `bInterfaceClass==LIBUSB_CLASS_VIDEO(0x0e)` 接口的标记 `is_uvc=1`。

- [ ] **Step 1: 在 `usb_desc.h` 声明**

```c
typedef struct {
    uint16_t vid, pid;
    char     product[64];
    int      is_uvc;   /* 含 Video(0x0e)接口 */
} usb_cam_cand_t;

/* 扫描连接的 USB 设备,返回 UVC 摄像头候选(写入 out,最多 max 个),返回个数 */
int usb_find_cameras(usb_cam_cand_t *out, int max);
```

- [ ] **Step 2: 在 `usb_desc.c` 实现**

```c
int usb_find_cameras(usb_cam_cand_t *out, int max)
{
    libusb_context *ctx = NULL;
    libusb_device **list = NULL;
    int found = 0;
    if (libusb_init(&ctx) < 0) return 0;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    for (ssize_t i = 0; i < cnt && found < max; i++) {
        struct libusb_device_descriptor dd;
        if (libusb_get_device_descriptor(list[i], &dd) < 0) continue;
        struct libusb_config_descriptor *cfg = NULL;
        if (libusb_get_active_config_descriptor(list[i], &cfg) < 0) continue;
        int is_uvc = 0;
        for (int n = 0; n < cfg->bNumInterfaces && !is_uvc; n++)
            for (int a = 0; a < cfg->interface[n].num_altsetting; a++)
                if (cfg->interface[n].altsetting[a].bInterfaceClass == LIBUSB_CLASS_VIDEO) { is_uvc = 1; break; }
        libusb_free_config_descriptor(cfg);
        if (!is_uvc) continue; /* 只自动收 UVC 摄像头;非 UVC 设备靠手动指定 VID:PID */
        usb_cam_cand_t *c = &out[found++];
        c->vid = dd.idVendor; c->pid = dd.idProduct; c->is_uvc = 1; c->product[0] = '\0';
        libusb_device_handle *h = NULL;
        if (libusb_open(list[i], &h) == 0) {
            if (dd.iProduct)
                libusb_get_string_descriptor_ascii(h, dd.iProduct, (unsigned char *)c->product, sizeof(c->product));
            libusb_close(h);
        }
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return found;
}
```

- [ ] **Step 3: 编译验证**

Run: `cd /home/mhpsy/code/temp/camera_usb_get/run-cam && make 2>&1 | tail -10`
Expected: 干净编译。

- [ ] **Step 4: 提交**

```bash
cd /home/mhpsy/code/temp/camera_usb_get
git add run-cam/src/usb_desc.h run-cam/src/usb_desc.c
git commit -m "feat(run-cam): add usb_find_cameras() to auto-detect connected UVC cameras"
```

---

### Task 3: `esp32_compat` 判定模块(纯函数 + 报告)

**Files:**
- Create: `run-cam/src/esp32_compat.h`
- Create: `run-cam/src/esp32_compat.c`

**Interfaces — Produces:**
- `esp32_compat_target_t`、`esp32_compat_report_t`(定义见下)。
- `void esp32_compat_check(const usb_desc_info_t *desc, const esp32_compat_target_t *t, esp32_compat_report_t *out);`(纯函数)
- `void esp32_compat_print_report(const esp32_compat_report_t *r, uint16_t vid, uint16_t pid, const char *product);`

- [ ] **Step 1: 写 `esp32_compat.h`**

```c
#ifndef ESP32_COMPAT_H
#define ESP32_COMPAT_H

#include <stdint.h>
#include "usb_desc.h"

typedef enum { ESP32_FMT_MJPEG, ESP32_FMT_YUYV } esp32_fmt_t;

typedef struct {
    esp32_fmt_t fmt;
    uint16_t    width, height, fps;
    int         hub_is_high_speed;
} esp32_compat_target_t;

/* 默认目标 = fish-tank-esp32 当前配置(出处见注释) */
static const esp32_compat_target_t ESP32_DEFAULT_TARGET = {
    .fmt = ESP32_FMT_MJPEG,  /* CONFIG_FISHTANK_UVC_FMT_MJPEG=y */
    .width = 640,            /* CONFIG_FISHTANK_UVC_DEFAULT_WIDTH */
    .height = 480,           /* CONFIG_FISHTANK_UVC_DEFAULT_HEIGHT */
    .fps = 30,               /* CONFIG_FISHTANK_UVC_DEFAULT_FPS */
    .hub_is_high_speed = 1,  /* 高速 hub;ESP-IDF 5.5.2 无 TT(hub.c:344-351) */
};

typedef enum { CHK_PASS, CHK_FAIL, CHK_WARN, CHK_INFO } chk_status_t;
typedef enum { VERDICT_PASS, VERDICT_CONDITIONAL, VERDICT_FAIL } verdict_t;

typedef struct {
    chk_status_t status;
    char         label[24];
    char         detail[200];
} compat_line_t;

typedef struct {
    compat_line_t lines[12];
    int           line_count;
    char          combos[512]; /* 目标格式下的可用分辨率@帧率列举 */
    verdict_t     verdict;
    char          summary[200];
} esp32_compat_report_t;

void esp32_compat_check(const usb_desc_info_t *desc, const esp32_compat_target_t *t, esp32_compat_report_t *out);
void esp32_compat_print_report(const esp32_compat_report_t *r, uint16_t vid, uint16_t pid, const char *product);

#endif
```

- [ ] **Step 2: 写 `esp32_compat.c`**(完整逻辑)

```c
/*
 * esp32_compat.c - 判断 USB 摄像头能否用于鱼缸 ESP32(usb_host_uvc 2.5.1, 经高速 hub)
 * 判据出处见 docs/2026-06-24-esp32-camera-compat-design.md §6。
 * esp32_compat_check() 为纯函数(无 I/O),便于单测。
 */
#include "esp32_compat.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

static compat_line_t *add_line(esp32_compat_report_t *r, chk_status_t st, const char *label)
{
    compat_line_t *l = &r->lines[r->line_count++];
    l->status = st;
    snprintf(l->label, sizeof(l->label), "%s", label);
    l->detail[0] = '\0';
    return l;
}

static const char *speed_name(int s)
{
    switch (s) {
    case LIBUSB_SPEED_LOW:        return "Low-Speed (1.5Mbps)";
    case LIBUSB_SPEED_FULL:       return "Full-Speed (12Mbps)";
    case LIBUSB_SPEED_HIGH:       return "High-Speed (480Mbps)";
    case LIBUSB_SPEED_SUPER:      return "SuperSpeed (5Gbps)";
    case LIBUSB_SPEED_SUPER_PLUS: return "SuperSpeed+ (10Gbps)";
    default:                      return "Unknown";
    }
}

void esp32_compat_check(const usb_desc_info_t *desc, const esp32_compat_target_t *t, esp32_compat_report_t *out)
{
    memset(out, 0, sizeof(*out));
    int fatal = 0;      /* 致命否决 */
    int conditional = 0; /* 改配置可用 */

    /* 约束 0: 是否 UVC 视频类设备 */
    if (desc->vs.present) {
        compat_line_t *l = add_line(out, CHK_PASS, "UVC 设备");
        snprintf(l->detail, sizeof(l->detail), "是(VS 流接口 #%u)", desc->vs.interface_number);
    } else {
        compat_line_t *l = add_line(out, CHK_FAIL, "UVC 设备");
        snprintf(l->detail, sizeof(l->detail), "否 — 无 Video(0x0e)流接口,ESP32 usb_host_uvc 无法驱动");
        fatal = 1;
    }

    /* 约束 1: 设备速度 / TT */
    {
        int s = desc->usb_speed;
        int hs_ok = (s == LIBUSB_SPEED_HIGH || s == LIBUSB_SPEED_SUPER || s == LIBUSB_SPEED_SUPER_PLUS);
        if (!t->hub_is_high_speed) {
            compat_line_t *l = add_line(out, CHK_INFO, "设备速度");
            snprintf(l->detail, sizeof(l->detail), "%s(目标非高速 hub,速度不否决)", speed_name(s));
        } else if (hs_ok) {
            compat_line_t *l = add_line(out, CHK_PASS, "设备速度");
            snprintf(l->detail, sizeof(l->detail), "%s — 可过高速 hub", speed_name(s));
        } else if (s == LIBUSB_SPEED_FULL || s == LIBUSB_SPEED_LOW) {
            compat_line_t *l = add_line(out, CHK_FAIL, "设备速度");
            snprintf(l->detail, sizeof(l->detail), "%s — 过高速 hub 会被 ESP-IDF 拒绝(无 TT)", speed_name(s));
            fatal = 1;
        } else {
            compat_line_t *l = add_line(out, CHK_WARN, "设备速度");
            snprintf(l->detail, sizeof(l->detail), "未知 — 请把摄像头插在 USB2.0/3.0 口直连重试");
        }
    }

    /* 约束 2: UVC 单端点 + isoc/bulk */
    if (desc->vs.present) {
        int bad = 0, isoc = 0, bulk = 0;
        for (int i = 0; i < desc->vs.alt_count; i++) {
            if (desc->vs.alts[i].num_endpoints != 1) bad = 1;
            if (desc->vs.alts[i].transfer_type == 1) isoc = 1;
            if (desc->vs.alts[i].transfer_type == 2) bulk = 1;
        }
        const char *kind = isoc && bulk ? "isoc+bulk" : isoc ? "isoc" : bulk ? "bulk" : "无端点";
        if (bad) {
            compat_line_t *l = add_line(out, CHK_FAIL, "UVC 端点");
            snprintf(l->detail, sizeof(l->detail), "某 altsetting 端点数!=1,不符合 UVC 组件要求");
            fatal = 1;
        } else {
            compat_line_t *l = add_line(out, CHK_PASS, "UVC 端点");
            snprintf(l->detail, sizeof(l->detail), "%d 个 altsetting,每个 1 端点(%s)", desc->vs.alt_count, kind);
        }
    }

    /* 约束 3 & 4: 格式 + 分辨率列举/高亮 */
    int want_mjpeg = (t->fmt == ESP32_FMT_MJPEG);
    const desc_format_t *tgt = NULL, *yuyv = NULL;
    for (int i = 0; i < desc->format_count; i++) {
        if (want_mjpeg && desc->formats[i].is_mjpeg) tgt = &desc->formats[i];
        if (!want_mjpeg && !desc->formats[i].is_mjpeg) tgt = &desc->formats[i];
        if (!desc->formats[i].is_mjpeg) yuyv = &desc->formats[i];
    }
    if (tgt) {
        compat_line_t *l = add_line(out, CHK_PASS, "格式");
        snprintf(l->detail, sizeof(l->detail), "支持目标格式 %s", want_mjpeg ? "MJPEG" : "YUYV");
    } else if (want_mjpeg && yuyv) {
        compat_line_t *l = add_line(out, CHK_WARN, "格式");
        snprintf(l->detail, sizeof(l->detail), "不支持 MJPEG,仅 YUYV — 需改 Kconfig 且 YUYV 带宽大");
        conditional = 1;
        tgt = yuyv; /* 仍列举其分辨率 */
    } else {
        compat_line_t *l = add_line(out, CHK_FAIL, "格式");
        snprintf(l->detail, sizeof(l->detail), "无目标格式可用");
        fatal = 1;
    }

    /* 列举目标格式分辨率,高亮默认,顺便找默认是否在列 */
    int default_found = 0;
    double tgt_bitrate = 0;
    if (tgt) {
        int n = 0;
        for (int f = 0; f < tgt->frame_count; f++) {
            const desc_frame_t *fr = &tgt->frames[f];
            double maxfps = 0;
            for (int k = 0; k < fr->interval_count; k++) if (fr->fps[k] > maxfps) maxfps = fr->fps[k];
            int is_def = (fr->width == t->width && fr->height == t->height);
            if (is_def) {
                default_found = 1;
                tgt_bitrate = fr->dwMaxBitRate;
            }
            char one[80];
            snprintf(one, sizeof(one), "%s%ux%u@%.0f%s", n ? ", " : "",
                     fr->width, fr->height, maxfps, is_def ? " ←默认" : "");
            if (strlen(out->combos) + strlen(one) < sizeof(out->combos) - 1) strcat(out->combos, one);
            n++;
        }
    }
    if (default_found) {
        compat_line_t *l = add_line(out, CHK_PASS, "默认分辨率");
        snprintf(l->detail, sizeof(l->detail), "%ux%u@%u 直接可用", t->width, t->height, t->fps);
    } else if (tgt && tgt->frame_count > 0) {
        compat_line_t *l = add_line(out, CHK_WARN, "默认分辨率");
        snprintf(l->detail, sizeof(l->detail), "%ux%u 不在列,但有其他组合可用(改 Kconfig)", t->width, t->height);
        conditional = 1;
    } else if (tgt) {
        compat_line_t *l = add_line(out, CHK_FAIL, "默认分辨率");
        snprintf(l->detail, sizeof(l->detail), "目标格式下无任何分辨率");
        fatal = 1;
    }

    /* 约束 5: 带宽估算(不否决) */
    {
        uint32_t max_mps_cap = 0;
        for (int i = 0; i < desc->vs.alt_count; i++) {
            if (desc->vs.alts[i].transfer_type != 1) continue; /* 只看 isoc */
            uint32_t cap = (uint32_t)desc->vs.alts[i].mps * (desc->vs.alts[i].mult + 1);
            if (cap > max_mps_cap) max_mps_cap = cap;
        }
        if (tgt_bitrate > 0 && max_mps_cap > 0) {
            double cap_bps = (double)max_mps_cap * 8000.0 * 8.0; /* 字节/微帧 ×8000微帧/s ×8 bit */
            chk_status_t st = (tgt_bitrate <= cap_bps) ? CHK_INFO : CHK_WARN;
            compat_line_t *l = add_line(out, st, "带宽(估算)");
            snprintf(l->detail, sizeof(l->detail), "默认约 %.0fMbps vs 单端点 isoc 上限约 %.0fMbps%s",
                     tgt_bitrate / 1e6, cap_bps / 1e6, (st == CHK_WARN) ? " ⚠可能不足" : " OK");
        }
    }

    /* 总判定 */
    if (fatal) {
        out->verdict = VERDICT_FAIL;
        snprintf(out->summary, sizeof(out->summary), "不可用 (FAIL)");
    } else if (conditional) {
        out->verdict = VERDICT_CONDITIONAL;
        snprintf(out->summary, sizeof(out->summary), "改配置可用 (CONDITIONAL) — 需调整 ESP32 的 Kconfig");
    } else {
        out->verdict = VERDICT_PASS;
        snprintf(out->summary, sizeof(out->summary), "可用 (PASS)");
    }
}

void esp32_compat_print_report(const esp32_compat_report_t *r, uint16_t vid, uint16_t pid, const char *product)
{
    static const char *mark[] = { "✓", "✗", "⚠", "~" }; /* PASS FAIL WARN INFO */
    printf("\n═══ ESP32(鱼缸)兼容性判定 ═══\n");
    printf("目标: MJPEG 640×480@30, 过 USB hub, ESP-IDF 5.5.2(无 TT)\n");
    printf("设备: %04x:%04x  \"%s\"\n\n", vid, pid, product ? product : "");
    for (int i = 0; i < r->line_count; i++)
        printf("  [%s] %-12s: %s\n", mark[r->lines[i].status], r->lines[i].label, r->lines[i].detail);
    if (r->combos[0])
        printf("      可用组合: %s\n", r->combos);
    const char *icon = r->verdict == VERDICT_PASS ? "✅" : r->verdict == VERDICT_CONDITIONAL ? "🟡" : "❌";
    printf("\n总判定: %s %s\n", icon, r->summary);
    printf("提示: 速度读数依赖把摄像头插在 Linux 的 USB2.0/3.0 口直连(勿经全速 hub)\n");
}
```

- [ ] **Step 3: 编译验证**

Run: `cd /home/mhpsy/code/temp/camera_usb_get/run-cam && make 2>&1 | tail -15`
Expected: 干净编译(esp32_compat.c 被 wildcard 自动纳入)。

- [ ] **Step 4: 提交**

```bash
cd /home/mhpsy/code/temp/camera_usb_get
git add run-cam/src/esp32_compat.h run-cam/src/esp32_compat.c
git commit -m "feat(run-cam): add esp32_compat module (pure compatibility check + report)"
```

---

### Task 4: `main.c` 集成(运行时目标 + 命令 11 + 菜单)

**Files:**
- Modify: `run-cam/src/main.c`
- Modify: `run-cam/Makefile`(加 esp32_compat 依赖行 + main.o 依赖加 esp32_compat.h)

**Interfaces — Consumes:** Task 2 的 `usb_find_cameras`、Task 3 的 `esp32_compat_*`、`ESP32_DEFAULT_TARGET`。

- [ ] **Step 1: 引入头文件与运行时目标全局**(`main.c`)

在 `#include "ffplay_ctrl.h"` 后加 `#include "esp32_compat.h"`。
把 `#define USB_VID 0x0bda` / `#define USB_PID 0x5846` 保留为**回退默认**,并新增全局:
```c
static uint16_t g_target_vid = USB_VID;
static uint16_t g_target_pid = USB_PID;
```
把 `cmd_usb_descriptors()` 里的 `usb_desc_dump(USB_VID, USB_PID, ...)` 改为 `usb_desc_dump(g_target_vid, g_target_pid, ...)`;
把 `main()` 里 `find_capture_device(USB_VID, USB_PID, ...)` 改为 `find_capture_device(g_target_vid, g_target_pid, ...)`;
日志里的 VID/PID 同样改用全局。

- [ ] **Step 2: 启动时自动检测目标摄像头**(`main.c` `main()`,日志初始化之后、`find_capture_device` 之前)

```c
    /* 自动检测连接的 UVC 摄像头作为默认目标 */
    {
        usb_cam_cand_t cands[8];
        int nc = usb_find_cameras(cands, 8);
        if (nc == 1) {
            g_target_vid = cands[0].vid; g_target_pid = cands[0].pid;
            LOG_I("自动检测到 UVC 摄像头: %04x:%04x \"%s\"", cands[0].vid, cands[0].pid, cands[0].product);
        } else if (nc > 1) {
            LOG_I("检测到 %d 个 UVC 摄像头,默认用第一个(命令 11 可改选)", nc);
            g_target_vid = cands[0].vid; g_target_pid = cands[0].pid;
        } else {
            LOG_W("未自动检测到 UVC 摄像头,回退默认 %04x:%04x(命令 11 可手动指定)", g_target_vid, g_target_pid);
        }
    }
```

- [ ] **Step 3: 实现 `cmd_esp32_compat()`**(`main.c`,放在 `cmd_ffplay_start` 附近)

```c
/* ===== 功能11: ESP32 兼容性判定 ===== */
static void cmd_esp32_compat(void)
{
    /* 选目标: 自动检测; 多个或想换则让用户选/手动输入 */
    usb_cam_cand_t cands[8];
    int nc = usb_find_cameras(cands, 8);
    printf("\n  检测到 %d 个 UVC 摄像头:\n", nc);
    for (int i = 0; i < nc; i++)
        printf("    [%d] %04x:%04x \"%s\"\n", i + 1, cands[i].vid, cands[i].pid, cands[i].product);
    printf("    [m] 手动输入 VID:PID(评估非 UVC 设备也用这个)\n");

    char *in = readline("\n  选择编号或 m(直接回车=当前目标): ");
    if (in && in[0] == 'm') {
        free(in);
        in = readline("  输入 VID:PID(十六进制, 如 2ce3:3828): ");
        unsigned v = 0, p = 0;
        if (in && sscanf(in, "%x:%x", &v, &p) == 2) { g_target_vid = v; g_target_pid = p; }
        else { printf("  格式错误。\n"); free(in); return; }
    } else if (in && in[0] >= '1' && in[0] <= '9') {
        int idx = atoi(in);
        if (idx >= 1 && idx <= nc) { g_target_vid = cands[idx - 1].vid; g_target_pid = cands[idx - 1].pid; }
    }
    free(in);

    printf("  目标: %04x:%04x\n", g_target_vid, g_target_pid);

    /* 解析描述符(顺带捕获速度/端点/格式) */
    usb_desc_info_t info;
    if (usb_desc_dump(g_target_vid, g_target_pid, &info) != 0) {
        printf("\n  读取 USB 描述符失败(设备未连接或无权限, 可 sudo 重试)。\n");
        return;
    }

    /* 取产品名用于展示 */
    char product[64] = "";
    usb_cam_cand_t cc[8];
    int n2 = usb_find_cameras(cc, 8);
    for (int i = 0; i < n2; i++)
        if (cc[i].vid == g_target_vid && cc[i].pid == g_target_pid) { snprintf(product, sizeof(product), "%s", cc[i].product); break; }

    esp32_compat_report_t rep;
    esp32_compat_check(&info, &ESP32_DEFAULT_TARGET, &rep);
    esp32_compat_print_report(&rep, g_target_vid, g_target_pid, product);
}
```

- [ ] **Step 4: 菜单加第 11 项 + 命令分发**(`main.c`)

在 `print_menu()` 的视频预览块后、退出项前加一行:
```c
    printf("║  [ESP32 适配]                                               ║\n");
    printf("║   11  - 判断该摄像头能否用于鱼缸 ESP32                      ║\n");
```
在 `commands[]` 数组加 `"11"`。
在主循环命令分发里,`else if (strcmp(cmd, "10") == 0)` 之后加:
```c
        } else if (strcmp(cmd, "11") == 0) {
            cmd_esp32_compat();
```

- [ ] **Step 5: 更新 Makefile 依赖**

在 `Makefile` 末尾加:
```make
$(BUILDDIR)/esp32_compat.o: $(SRCDIR)/esp32_compat.c $(SRCDIR)/esp32_compat.h $(SRCDIR)/usb_desc.h $(SRCDIR)/logger.h
```
并把 `main.o` 依赖行末尾追加 `$(SRCDIR)/esp32_compat.h`。

- [ ] **Step 6: 编译 + 冒烟验证**

Run: `cd /home/mhpsy/code/temp/camera_usb_get/run-cam && make clean && make 2>&1 | tail -15`
Expected: 干净编译。
Run: `printf 'help\n0\n' | ./build/uvc-tool 2>/dev/null | grep -A1 "ESP32 适配"`
Expected: 看到菜单第 11 项。

- [ ] **Step 7: 提交**

```bash
cd /home/mhpsy/code/temp/camera_usb_get
git add run-cam/src/main.c run-cam/Makefile
git commit -m "feat(run-cam): add command 11 (ESP32 camera compatibility check) with runtime camera target"
```

---

### Task 5: 真机测试(主会话执行,需摄像头在线)

- [ ] FAIL 路径:命令 11 → `m` → 输入 `2ce3:3828`(本机厂商私有 useepluscam)→ 应得"UVC 设备: 否 → FAIL"。
- [ ] PASS 路径:插入标准 UVC 摄像头 → 命令 11 选它 → 核对各项与总判定。
- [ ] 核对报告文案、可用组合列举、默认高亮是否正确。

## Self-Review

- 覆盖:spec §3(目标选择)→Task2+Task4;§4(结构)→Task1;§5(目标常量)→Task3;§6(判据)→Task3;§7(交互)→Task4;§8(报告)→Task3;§9(错误)→Task3/4;§10(测试)→Task5。无遗漏。
- 占位符:无 TODO/TBD,关键代码均给出。
- 类型一致:`usb_desc_info_t` 新字段、`usb_cam_cand_t`、`esp32_compat_*` 在各任务间签名一致。
