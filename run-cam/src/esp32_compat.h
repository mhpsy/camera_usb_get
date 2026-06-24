/*
 * esp32_compat.h - 判断 USB 摄像头能否用于鱼缸 ESP32
 *
 * 目标平台: fish-tank-esp32 (ESP32-P4 + espressif/usb_host_uvc 2.5.1),
 * 摄像头经 USB hub 连接。判据出处见 docs/2026-06-24-esp32-camera-compat-design.md。
 *
 * esp32_compat_check() 为纯函数(无 I/O),便于喂构造数据做单测。
 */

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
    .fmt               = ESP32_FMT_MJPEG, /* CONFIG_FISHTANK_UVC_FMT_MJPEG=y */
    .width             = 640,             /* CONFIG_FISHTANK_UVC_DEFAULT_WIDTH */
    .height            = 480,             /* CONFIG_FISHTANK_UVC_DEFAULT_HEIGHT */
    .fps               = 30,              /* CONFIG_FISHTANK_UVC_DEFAULT_FPS */
    .hub_is_high_speed = 1,               /* 高速 hub;ESP-IDF 5.5.2 无 TT(hub.c:344-351) */
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

/* 纯判定: 输入描述符信息 + 目标配置, 输出分项报告 + 总判定 */
void esp32_compat_check(const usb_desc_info_t *desc, const esp32_compat_target_t *t, esp32_compat_report_t *out);

/* 打印报告(分项 ✓/✗/⚠ + 可用组合 + 总判定) */
void esp32_compat_print_report(const esp32_compat_report_t *r, uint16_t vid, uint16_t pid, const char *product);

#endif /* ESP32_COMPAT_H */
