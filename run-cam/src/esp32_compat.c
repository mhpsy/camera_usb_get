/*
 * esp32_compat.c - 判断 USB 摄像头能否用于鱼缸 ESP32(usb_host_uvc 2.5.1, 经高速 hub)
 *
 * 判据出处见 docs/2026-06-24-esp32-camera-compat-design.md §6:
 *   0) 是 UVC 视频类设备       (vs.present)
 *   1) 设备速度/TT             (过高速 hub 需高速设备; ESP-IDF 无 TT, hub.c:344-351)
 *   2) UVC 单端点              (每个 streaming alt 恰好 1 端点, uvc_descriptor_parsing.c:91)
 *   3) 格式                    (默认 MJPEG)
 *   4) 分辨率/帧率 列举+高亮默认
 *   5) 带宽估算                (不单独否决)
 *
 * esp32_compat_check() 为纯函数(无 I/O)。
 */

#include "esp32_compat.h"
#include "logger.h"

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>

static compat_line_t *add_line(esp32_compat_report_t *r, chk_status_t st, const char *label)
{
    compat_line_t *l = &r->lines[r->line_count++];
    l->status        = st;
    snprintf(l->label, sizeof(l->label), "%s", label);
    l->detail[0] = '\0';
    return l;
}

static const char *speed_name(int s)
{
    switch (s) {
    case LIBUSB_SPEED_LOW:
        return "Low-Speed (1.5Mbps)";
    case LIBUSB_SPEED_FULL:
        return "Full-Speed (12Mbps)";
    case LIBUSB_SPEED_HIGH:
        return "High-Speed (480Mbps)";
    case LIBUSB_SPEED_SUPER:
        return "SuperSpeed (5Gbps)";
    case LIBUSB_SPEED_SUPER_PLUS:
        return "SuperSpeed+ (10Gbps)";
    default:
        return "Unknown";
    }
}

void esp32_compat_check(const usb_desc_info_t *desc, const esp32_compat_target_t *t, esp32_compat_report_t *out)
{
    memset(out, 0, sizeof(*out));
    int fatal       = 0; /* 致命否决 */
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
        int s     = desc->usb_speed;
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
            if (desc->vs.alts[i].num_endpoints != 1)
                bad = 1;
            if (desc->vs.alts[i].transfer_type == 1)
                isoc = 1;
            if (desc->vs.alts[i].transfer_type == 2)
                bulk = 1;
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
    int                  want_mjpeg = (t->fmt == ESP32_FMT_MJPEG);
    const desc_format_t *tgt = NULL, *yuyv = NULL;
    for (int i = 0; i < desc->format_count; i++) {
        if (want_mjpeg && desc->formats[i].is_mjpeg)
            tgt = &desc->formats[i];
        if (!want_mjpeg && !desc->formats[i].is_mjpeg)
            tgt = &desc->formats[i];
        if (!desc->formats[i].is_mjpeg)
            yuyv = &desc->formats[i];
    }
    if (tgt) {
        compat_line_t *l = add_line(out, CHK_PASS, "格式");
        snprintf(l->detail, sizeof(l->detail), "支持目标格式 %s", want_mjpeg ? "MJPEG" : "YUYV");
    } else if (want_mjpeg && yuyv) {
        compat_line_t *l = add_line(out, CHK_WARN, "格式");
        snprintf(l->detail, sizeof(l->detail), "不支持 MJPEG,仅 YUYV — 需改 Kconfig 且 YUYV 带宽大");
        conditional = 1;
        tgt         = yuyv; /* 仍列举其分辨率 */
    } else {
        compat_line_t *l = add_line(out, CHK_FAIL, "格式");
        snprintf(l->detail, sizeof(l->detail), "无目标格式可用");
        fatal = 1;
    }

    /* 列举目标格式分辨率,高亮默认,顺便找默认是否在列 */
    int    default_found = 0;
    double tgt_bitrate   = 0;
    if (tgt) {
        int n = 0;
        for (int f = 0; f < tgt->frame_count; f++) {
            const desc_frame_t *fr     = &tgt->frames[f];
            double              maxfps = 0;
            for (int k = 0; k < fr->interval_count; k++)
                if (fr->fps[k] > maxfps)
                    maxfps = fr->fps[k];
            int is_def = (fr->width == t->width && fr->height == t->height);
            if (is_def) {
                default_found = 1;
                tgt_bitrate   = fr->dwMaxBitRate;
            }
            char one[80];
            snprintf(one, sizeof(one), "%s%ux%u@%.0f%s", n ? ", " : "", fr->width, fr->height, maxfps,
                     is_def ? " ←默认" : "");
            if (strlen(out->combos) + strlen(one) < sizeof(out->combos) - 1)
                strcat(out->combos, one);
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
            if (desc->vs.alts[i].transfer_type != 1)
                continue; /* 只看 isoc */
            uint32_t cap = (uint32_t)desc->vs.alts[i].mps * (desc->vs.alts[i].mult + 1);
            if (cap > max_mps_cap)
                max_mps_cap = cap;
        }
        if (tgt_bitrate > 0 && max_mps_cap > 0) {
            double       cap_bps = (double)max_mps_cap * 8000.0 * 8.0; /* 字节/微帧 ×8000微帧/s ×8 bit */
            chk_status_t st      = (tgt_bitrate <= cap_bps) ? CHK_INFO : CHK_WARN;
            compat_line_t *l     = add_line(out, st, "带宽(估算)");
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
    static const char *mark[] = {"✓", "✗", "⚠", "~"}; /* PASS FAIL WARN INFO */
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
