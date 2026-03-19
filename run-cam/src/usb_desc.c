/*
 * usb_desc.c - USB 描述符解析实现
 *
 * 通过 libusb 获取设备的原始描述符数据，然后逐字节解析并打印。
 *
 * === UVC 协议架构概述 ===
 *
 * USB视频类设备由以下部分组成（自上而下的数据流）：
 *
 *   [输入终端(IT)] → [处理单元(PU)] → [扩展单元(XU)] → [输出终端(OT)]
 *        ↑                 ↑                 ↑                 ↑
 *     摄像头传感器     亮度/对比度等      厂商私有功能      USB数据流
 *
 * 每个"单元"或"终端"都有一个唯一ID(bTerminalID/bUnitID)，通过bSourceID串联。
 *
 * 设备有两种接口：
 * 1. 视频控制接口(VC) - 用于控制摄像头参数
 * 2. 视频流接口(VS)   - 用于传输实际的视频数据
 */

#include "usb_desc.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

/* ==============================
 * 辅助函数：GUID格式化
 * ============================== */

/*
 * 将16字节的GUID转为标准格式字符串
 * 格式: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
 * 注意：USB/UVC中GUID是小端序存储的
 */
void usb_desc_guid_to_str(const uint8_t guid[16], char *buf, int buf_len)
{
    snprintf(buf, buf_len,
        "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
        guid[3], guid[2], guid[1], guid[0],   /* Data1 小端 */
        guid[5], guid[4],                       /* Data2 小端 */
        guid[7], guid[6],                       /* Data3 小端 */
        guid[8], guid[9],                       /* Data4[0..1] 大端 */
        guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

/* ==============================
 * 辅助函数：终端类型名称
 * ============================== */

static const char *terminal_type_name(uint16_t wTerminalType)
{
    /*
     * UVC规范定义的终端类型：
     * 0x0100: USB Vendor Specific
     * 0x0101: USB Streaming   - 数据通过USB传输
     * 0x0200: Input Vendor Specific
     * 0x0201: Camera Sensor   - 摄像头CMOS/CCD传感器
     * 0x0202: Sequential Media - 顺序访问媒体（如VHS）
     * 0x0301: Display Vendor Specific
     * 0x0401: Composite Connector
     * 0x0402: S-Video Connector
     * 0x0403: Component Connector
     */
    switch (wTerminalType) {
    case 0x0100: return "USB Vendor Specific";
    case 0x0101: return "USB Streaming (USB数据流出口)";
    case 0x0200: return "Input Vendor Specific";
    case 0x0201: return "Camera Sensor (摄像头传感器)";
    case 0x0202: return "Sequential Media";
    case 0x0301: return "Display Vendor Specific";
    case 0x0401: return "Composite Connector";
    case 0x0402: return "S-Video Connector";
    case 0x0403: return "Component Connector";
    default:     return "Unknown";
    }
}

/* ==============================
 * 解析输入终端(IT)的控制位图
 * ============================== */

static void parse_camera_controls(uint32_t bmControls)
{
    /*
     * 输入终端(Camera)的bmControls每一位代表一个可控功能：
     * 这些功能是物理摄像头传感器层面的控制。
     */
    static const char *cam_ctrl_names[] = {
        "扫描模式(Scanning Mode)",            /* D0 */
        "自动曝光模式(Auto-Exposure Mode)",    /* D1 */
        "自动曝光优先级(Auto-Exposure Prio)",  /* D2 */
        "曝光时间绝对值(Exposure Time Abs)",   /* D3 */
        "曝光时间相对值(Exposure Time Rel)",   /* D4 */
        "聚焦绝对值(Focus Absolute)",          /* D5 */
        "聚焦相对值(Focus Relative)",          /* D6 */
        "光圈绝对值(Iris Absolute)",           /* D7 */
        "光圈相对值(Iris Relative)",           /* D8 */
        "缩放绝对值(Zoom Absolute)",           /* D9 */
        "缩放相对值(Zoom Relative)",           /* D10 */
        "平移绝对值(PanTilt Absolute)",        /* D11 */
        "平移相对值(PanTilt Relative)",        /* D12 */
        "滚转绝对值(Roll Absolute)",           /* D13 */
        "滚转相对值(Roll Relative)",           /* D14 */
        "保留(Reserved)",                      /* D15 */
        "保留(Reserved)",                      /* D16 */
        "自动聚焦(Auto Focus)",                /* D17 */
        "隐私(Privacy)",                       /* D18 */
        "聚焦简单(Focus Simple)",              /* D19 */
        "窗口(Window)",                        /* D20 */
        "感兴趣区域(Region of Interest)",      /* D21 */
    };

    LOG_I("    支持的摄像头控制 (bmControls=0x%08x):", bmControls);
    for (int i = 0; i < 22; i++) {
        if (bmControls & (1U << i)) {
            LOG_I("      [✓] D%d: %s", i, cam_ctrl_names[i]);
        }
    }
}

/* ==============================
 * 解析处理单元(PU)的控制位图
 * ============================== */

static void parse_pu_controls(uint16_t bmControls)
{
    /*
     * 处理单元(PU)的bmControls：图像处理层面的控制
     * 这些是主机可以调整的图像参数。
     */
    static const char *pu_ctrl_names[] = {
        "亮度(Brightness)",                     /* D0 */
        "对比度(Contrast)",                     /* D1 */
        "色调(Hue)",                            /* D2 */
        "饱和度(Saturation)",                   /* D3 */
        "锐度(Sharpness)",                      /* D4 */
        "伽马(Gamma)",                          /* D5 */
        "白平衡色温(WB Temperature)",           /* D6 */
        "白平衡分量(WB Component)",             /* D7 */
        "背光补偿(Backlight Compensation)",     /* D8 */
        "增益(Gain)",                           /* D9 */
        "电力线频率(Power Line Frequency)",     /* D10 */
        "自动色调(Hue Auto)",                   /* D11 */
        "自动白平衡色温(WB Temperature Auto)",  /* D12 */
        "自动白平衡分量(WB Component Auto)",    /* D13 */
        "数字倍率(Digital Multiplier)",         /* D14 */
        "数字倍率上限(Digital Multiplier Limit)", /* D15 */
    };

    LOG_I("    支持的处理控制 (bmControls=0x%04x):", bmControls);
    for (int i = 0; i < 16; i++) {
        if (bmControls & (1U << i)) {
            LOG_I("      [✓] D%d: %s", i, pu_ctrl_names[i]);
        }
    }
}

/* ==============================
 * 解析类特定VC接口描述符
 * ============================== */

static void parse_vc_descriptor(const unsigned char *buf, int len,
                                usb_desc_info_t *info)
{
    if (len < 3) return;

    uint8_t bDescriptorSubtype = buf[2];

    switch (bDescriptorSubtype) {
    case UVC_VC_HEADER: {
        /*
         * VC接口头描述符 - UVC设备的"身份证"
         * 告诉主机这个设备遵循哪个版本的UVC规范
         */
        if (len < 12) break;
        uint16_t bcdUVC = buf[3] | (buf[4] << 8);
        uint16_t wTotalLength = buf[5] | (buf[6] << 8);
        uint32_t dwClockFrequency = buf[7] | (buf[8] << 8) |
                                    (buf[9] << 16) | (buf[10] << 24);
        uint8_t bInCollection = buf[11];

        LOG_I("  [VC HEADER] UVC视频控制接口头描述符");
        LOG_I("    bcdUVC           = %d.%02d (UVC协议版本)", bcdUVC >> 8, bcdUVC & 0xff);
        LOG_I("    wTotalLength     = %u 字节 (所有VC描述符的总长度)", wTotalLength);
        LOG_I("    dwClockFrequency = %u Hz (设备时钟频率，用于时间戳)", dwClockFrequency);
        LOG_I("    bInCollection    = %u (该VC接口关联的VS接口数量)", bInCollection);
        for (int i = 0; i < bInCollection && (12 + i) < len; i++) {
            LOG_I("    baInterfaceNr[%d] = %u (关联的VS接口编号)", i, buf[12 + i]);
        }
        break;
    }

    case UVC_VC_INPUT_TERMINAL: {
        /*
         * 输入终端描述符 - 数据的来源
         * 对于摄像头，这就是CMOS传感器
         * bTerminalID 是这个终端的唯一标识，其他单元通过 bSourceID 引用它
         */
        if (len < 8) break;
        uint8_t bTerminalID = buf[3];
        uint16_t wTerminalType = buf[4] | (buf[5] << 8);
        uint8_t bAssocTerminal = buf[6];

        LOG_I("  [INPUT_TERMINAL] 输入终端描述符 (数据来源)");
        LOG_I("    bTerminalID    = %u (终端唯一ID，下游单元的bSourceID会引用此值)", bTerminalID);
        LOG_I("    wTerminalType  = 0x%04x → %s", wTerminalType, terminal_type_name(wTerminalType));
        LOG_I("    bAssocTerminal = %u (关联的输出终端ID，0表示无关联)", bAssocTerminal);

        /* 如果是摄像头类型(0x0201)，有额外的摄像头特有字段 */
        if (wTerminalType == 0x0201 && len >= 15) {
            uint16_t wObjFocalMin = buf[8] | (buf[9] << 8);
            uint16_t wObjFocalMax = buf[10] | (buf[11] << 8);
            uint16_t wOcularFocal = buf[12] | (buf[13] << 8);
            uint8_t bControlSize = buf[14];

            LOG_I("    wObjectiveFocalLengthMin = %u (最小焦距)", wObjFocalMin);
            LOG_I("    wObjectiveFocalLengthMax = %u (最大焦距)", wObjFocalMax);
            LOG_I("    wOcularFocalLength       = %u (目镜焦距)", wOcularFocal);
            LOG_I("    bControlSize             = %u (控制位图字节数)", bControlSize);

            if (bControlSize > 0 && len >= 15 + bControlSize) {
                uint32_t bmControls = 0;
                for (int i = 0; i < bControlSize && i < 4; i++)
                    bmControls |= (uint32_t)buf[15 + i] << (8 * i);
                parse_camera_controls(bmControls);
            }
        }
        break;
    }

    case UVC_VC_OUTPUT_TERMINAL: {
        /*
         * 输出终端描述符 - 数据的去处
         * 通常类型是 0x0101 (USB Streaming)，表示数据最终通过USB发送给主机
         * bSourceID 指向上游的单元或终端
         */
        if (len < 9) break;
        uint8_t bTerminalID = buf[3];
        uint16_t wTerminalType = buf[4] | (buf[5] << 8);
        uint8_t bAssocTerminal = buf[6];
        uint8_t bSourceID = buf[7];

        LOG_I("  [OUTPUT_TERMINAL] 输出终端描述符 (数据去处)");
        LOG_I("    bTerminalID    = %u", bTerminalID);
        LOG_I("    wTerminalType  = 0x%04x → %s", wTerminalType, terminal_type_name(wTerminalType));
        LOG_I("    bAssocTerminal = %u", bAssocTerminal);
        LOG_I("    bSourceID      = %u (数据来自ID=%u的单元/终端)", bSourceID, bSourceID);
        break;
    }

    case UVC_VC_SELECTOR_UNIT: {
        /*
         * 选择单元 - 在多个输入源之间切换
         * 例如设备有前置+后置摄像头时，通过选择单元切换
         */
        if (len < 6) break;
        uint8_t bUnitID = buf[3];
        uint8_t bNrInPins = buf[4];

        LOG_I("  [SELECTOR_UNIT] 选择单元描述符 (输入切换器)");
        LOG_I("    bUnitID   = %u", bUnitID);
        LOG_I("    bNrInPins = %u (可选输入源数量)", bNrInPins);
        for (int i = 0; i < bNrInPins && (5 + i) < len; i++) {
            LOG_I("    baSourceID[%d] = %u", i, buf[5 + i]);
        }
        break;
    }

    case UVC_VC_PROCESSING_UNIT: {
        /*
         * 处理单元(PU) - 图像处理引擎
         * 提供亮度、对比度、饱和度、锐度等传统图像调整功能
         * 这些就是你在视频软件里看到的"图像设置"
         */
        if (len < 10) break;
        uint8_t bUnitID = buf[3];
        uint8_t bSourceID = buf[4];
        uint16_t wMaxMultiplier = buf[5] | (buf[6] << 8);
        uint8_t bControlSize = buf[7];

        LOG_I("  [PROCESSING_UNIT] 处理单元描述符 (图像处理)");
        LOG_I("    bUnitID        = %u", bUnitID);
        LOG_I("    bSourceID      = %u (数据来自ID=%u)", bSourceID, bSourceID);
        LOG_I("    wMaxMultiplier = %u (最大数字缩放倍率 x%u.%02u)",
              wMaxMultiplier, wMaxMultiplier / 100, wMaxMultiplier % 100);
        LOG_I("    bControlSize   = %u", bControlSize);

        if (bControlSize > 0 && len >= 8 + bControlSize) {
            uint16_t bmControls = 0;
            for (int i = 0; i < bControlSize && i < 2; i++)
                bmControls |= (uint16_t)buf[8 + i] << (8 * i);
            parse_pu_controls(bmControls);
        }
        break;
    }

    case UVC_VC_EXTENSION_UNIT: {
        /*
         * 扩展单元(XU) - 厂商私有功能！
         *
         * 这是最有趣的部分：厂商可以在这里实现任何非标准功能，
         * 比如：LED控制、HDR开关、人脸检测开关、固件更新等。
         *
         * 每个XU通过GUID标识，主机需要知道GUID对应的协议才能使用。
         * 控制通过 UVC_SET_CUR/UVC_GET_CUR 等请求来读写。
         */
        if (len < 24) break;
        uint8_t bUnitID = buf[3];
        const uint8_t *guid = &buf[4];  /* 16字节GUID */
        uint8_t bNumControls = buf[20];
        uint8_t bNrInPins = buf[21];

        char guid_str[64];
        usb_desc_guid_to_str(guid, guid_str, sizeof(guid_str));

        LOG_I("  [EXTENSION_UNIT] 扩展单元描述符 (厂商私有功能!) ★");
        LOG_I("    bUnitID       = %u (XU控制命令中指定 unit=%u)", bUnitID, bUnitID);
        LOG_I("    guidExtension = %s", guid_str);
        LOG_I("    bNumControls  = %u (该XU暴露的控制数量)", bNumControls);
        LOG_I("    bNrInPins     = %u", bNrInPins);

        int offset = 22;
        for (int i = 0; i < bNrInPins && offset < len; i++, offset++) {
            LOG_I("    baSourceID[%d] = %u", i, buf[offset]);
        }

        if (offset < len) {
            uint8_t bControlSize = buf[offset++];
            uint32_t bmControls = 0;
            for (int i = 0; i < bControlSize && (offset + i) < len && i < 4; i++)
                bmControls |= (uint32_t)buf[offset + i] << (8 * i);

            LOG_I("    bControlSize  = %u", bControlSize);
            LOG_I("    bmControls    = 0x%08x", bmControls);
            LOG_I("    各控制位含义 (哪些位为1, 就表示支持第几号控制, selector从1开始):");
            for (int i = 0; i < bNumControls && i < 32; i++) {
                if (bmControls & (1U << i)) {
                    LOG_I("      [✓] 控制 #%d (selector=%d) — 可通过XU命令访问", i + 1, i + 1);
                }
            }

            /* 保存XU信息供后续使用 */
            if (info && info->xu_count < MAX_XU_COUNT) {
                xu_info_t *xu = &info->xus[info->xu_count++];
                xu->unit_id = bUnitID;
                memcpy(xu->guid, guid, 16);
                xu->num_controls = bNumControls;
                xu->bmControls = bmControls;
                xu->source_id = (bNrInPins > 0) ? buf[22] : 0;
            }
        }
        break;
    }

    default:
        LOG_D("  [VC 子类型 0x%02x] 未知VC描述符子类型 (长度=%d)", bDescriptorSubtype, len);
        break;
    }
}

/* ==============================
 * 解析类特定VS接口描述符
 * ============================== */

static void parse_vs_descriptor(const unsigned char *buf, int len)
{
    if (len < 3) return;

    uint8_t bDescriptorSubtype = buf[2];

    switch (bDescriptorSubtype) {
    case UVC_VS_INPUT_HEADER: {
        /*
         * VS输入头描述符 - 视频流的"入口"
         * 描述了有多少种格式可用，数据通过哪个端点传输
         */
        if (len < 13) break;
        uint8_t bNumFormats = buf[3];
        uint16_t wTotalLength = buf[4] | (buf[5] << 8);
        uint8_t bEndpointAddress = buf[6];
        uint8_t bTerminalLink = buf[8];
        uint8_t bStillCaptureMethod = buf[9];

        LOG_I("  [VS INPUT_HEADER] 视频流输入头");
        LOG_I("    bNumFormats         = %u (支持的视频格式数)", bNumFormats);
        LOG_I("    wTotalLength        = %u 字节", wTotalLength);
        LOG_I("    bEndpointAddress    = 0x%02x (EP %d IN, 视频数据端点)",
              bEndpointAddress, bEndpointAddress & 0x0f);
        LOG_I("    bTerminalLink       = %u (关联的输出终端ID)", bTerminalLink);
        LOG_I("    bStillCaptureMethod = %u (%s)", bStillCaptureMethod,
              bStillCaptureMethod == 0 ? "不支持静态捕获" :
              bStillCaptureMethod == 1 ? "方法1(中断视频流)" :
              bStillCaptureMethod == 2 ? "方法2(专用端点)" :
              bStillCaptureMethod == 3 ? "方法3(设备触发)" : "未知");
        break;
    }

    case UVC_VS_FORMAT_MJPEG: {
        /*
         * MJPEG格式描述符
         * MJPEG是最常见的USB摄像头压缩格式，每帧独立压缩为JPEG
         * 带宽要求低于未压缩格式，但需要解码
         */
        if (len < 11) break;
        uint8_t bFormatIndex = buf[3];
        uint8_t bNumFrameDescriptors = buf[4];
        uint8_t bFlags = buf[5];
        uint8_t bDefaultFrameIndex = buf[6];

        LOG_I("  [VS FORMAT_MJPEG] MJPEG格式描述符");
        LOG_I("    bFormatIndex         = %u (格式编号，协商时使用)", bFormatIndex);
        LOG_I("    bNumFrameDescriptors = %u (该格式下有多少种分辨率可选)", bNumFrameDescriptors);
        LOG_I("    bFlags               = 0x%02x (%s)", bFlags,
              (bFlags & 1) ? "固定大小采样" : "可变大小采样");
        LOG_I("    bDefaultFrameIndex   = %u (默认分辨率编号)", bDefaultFrameIndex);
        break;
    }

    case UVC_VS_FRAME_MJPEG:
    case UVC_VS_FRAME_UNCOMPRESSED: {
        /*
         * 帧描述符 - 描述一个具体的分辨率+帧率组合
         * 这是你在选择"640x480 30fps"时实际对应的描述符
         */
        if (len < 26) break;
        uint8_t bFrameIndex = buf[3];
        uint8_t bmCapabilities = buf[4];
        uint16_t wWidth = buf[5] | (buf[6] << 8);
        uint16_t wHeight = buf[7] | (buf[8] << 8);
        uint32_t dwMinBitRate = buf[9] | (buf[10] << 8) | (buf[11] << 16) | (buf[12] << 24);
        uint32_t dwMaxBitRate = buf[13] | (buf[14] << 8) | (buf[15] << 16) | (buf[16] << 24);
        uint32_t dwMaxVideoFrameBufferSize = buf[17] | (buf[18] << 8) | (buf[19] << 16) | (buf[20] << 24);
        uint32_t dwDefaultFrameInterval = buf[21] | (buf[22] << 8) | (buf[23] << 16) | (buf[24] << 24);
        uint8_t bFrameIntervalType = buf[25];

        const char *type_str = (bDescriptorSubtype == UVC_VS_FRAME_MJPEG) ? "MJPEG" : "Uncompressed";

        LOG_I("  [VS FRAME_%s] 帧描述符 #%u: %ux%u", type_str, bFrameIndex, wWidth, wHeight);
        LOG_I("    bmCapabilities   = 0x%02x (%s)", bmCapabilities,
              (bmCapabilities & 1) ? "支持静态图像" : "不支持静态图像");
        LOG_I("    dwMinBitRate     = %u bps (%.1f Mbps)", dwMinBitRate, dwMinBitRate / 1000000.0);
        LOG_I("    dwMaxBitRate     = %u bps (%.1f Mbps)", dwMaxBitRate, dwMaxBitRate / 1000000.0);
        LOG_I("    dwMaxVideoFrameBufferSize = %u 字节 (单帧最大缓冲)", dwMaxVideoFrameBufferSize);
        LOG_I("    dwDefaultFrameInterval    = %u (100ns单位 = %.1f fps)",
              dwDefaultFrameInterval, 10000000.0 / dwDefaultFrameInterval);

        if (bFrameIntervalType == 0) {
            /* 连续帧间隔 */
            if (len >= 38) {
                uint32_t dwMinFI = buf[26] | (buf[27] << 8) | (buf[28] << 16) | (buf[29] << 24);
                uint32_t dwMaxFI = buf[30] | (buf[31] << 8) | (buf[32] << 16) | (buf[33] << 24);
                uint32_t dwStepFI = buf[34] | (buf[35] << 8) | (buf[36] << 16) | (buf[37] << 24);
                LOG_I("    帧间隔: 连续范围 %.1f fps ~ %.1f fps, 步进=%u",
                      10000000.0 / dwMaxFI, 10000000.0 / dwMinFI, dwStepFI);
            }
        } else {
            /* 离散帧间隔 */
            LOG_I("    帧间隔: %u 个离散值:", bFrameIntervalType);
            for (int i = 0; i < bFrameIntervalType; i++) {
                int off = 26 + i * 4;
                if (off + 3 < len) {
                    uint32_t fi = buf[off] | (buf[off+1] << 8) | (buf[off+2] << 16) | (buf[off+3] << 24);
                    LOG_I("      [%d] %u (100ns) → %.1f fps", i + 1, fi, 10000000.0 / fi);
                }
            }
        }
        break;
    }

    case UVC_VS_FORMAT_UNCOMPRESSED: {
        /*
         * 未压缩格式描述符
         * 如YUY2(YUYV)、NV12等，直接传输像素数据
         * 质量最好但占用带宽最大
         */
        if (len < 27) break;
        uint8_t bFormatIndex = buf[3];
        uint8_t bNumFrameDescriptors = buf[4];
        /* GUID标识了具体的像素格式 */
        char fmt_guid[64];
        usb_desc_guid_to_str(&buf[5], fmt_guid, sizeof(fmt_guid));
        uint8_t bBitsPerPixel = buf[21];
        uint8_t bDefaultFrameIndex = buf[22];

        LOG_I("  [VS FORMAT_UNCOMPRESSED] 未压缩格式描述符");
        LOG_I("    bFormatIndex         = %u", bFormatIndex);
        LOG_I("    bNumFrameDescriptors = %u", bNumFrameDescriptors);
        LOG_I("    guidFormat           = %s", fmt_guid);
        LOG_I("    bBitsPerPixel        = %u", bBitsPerPixel);
        LOG_I("    bDefaultFrameIndex   = %u", bDefaultFrameIndex);
        break;
    }

    case UVC_VS_COLOR_FORMAT: {
        /*
         * 颜色匹配描述符 - 描述色彩空间
         */
        if (len < 6) break;
        uint8_t bColorPrimaries = buf[3];
        uint8_t bTransferCharacteristics = buf[4];
        uint8_t bMatrixCoefficients = buf[5];

        LOG_I("  [VS COLOR_FORMAT] 颜色匹配描述符");
        LOG_I("    bColorPrimaries          = %u (%s)", bColorPrimaries,
              bColorPrimaries == 1 ? "BT.709, sRGB" :
              bColorPrimaries == 2 ? "BT.470-2(M)" :
              bColorPrimaries == 3 ? "BT.470-2(B,G)" :
              bColorPrimaries == 4 ? "SMPTE 170M" :
              bColorPrimaries == 5 ? "SMPTE 240M" : "未指定");
        LOG_I("    bTransferCharacteristics = %u (%s)", bTransferCharacteristics,
              bTransferCharacteristics == 1 ? "BT.709" :
              bTransferCharacteristics == 2 ? "BT.470-2(M)" :
              bTransferCharacteristics == 3 ? "BT.470-2(B,G)" :
              bTransferCharacteristics == 4 ? "SMPTE 170M" :
              bTransferCharacteristics == 5 ? "SMPTE 240M" :
              bTransferCharacteristics == 6 ? "线性" :
              bTransferCharacteristics == 7 ? "sRGB" : "未指定");
        LOG_I("    bMatrixCoefficients      = %u (%s)", bMatrixCoefficients,
              bMatrixCoefficients == 1 ? "BT.709" :
              bMatrixCoefficients == 2 ? "FCC" :
              bMatrixCoefficients == 3 ? "BT.470-2(B,G)" :
              bMatrixCoefficients == 4 ? "SMPTE 170M (BT.601)" :
              bMatrixCoefficients == 5 ? "SMPTE 240M" : "未指定");
        break;
    }

    default:
        LOG_D("  [VS 子类型 0x%02x] 长度=%d", bDescriptorSubtype, len);
        break;
    }
}

/* ==============================
 * 主要的描述符dump函数
 * ============================== */

int usb_desc_dump(uint16_t vid, uint16_t pid, usb_desc_info_t *info)
{
    libusb_context *ctx = NULL;
    libusb_device **devlist = NULL;
    int ret;

    if (info)
        memset(info, 0, sizeof(*info));

    ret = libusb_init(&ctx);
    if (ret < 0) {
        LOG_E("libusb初始化失败: %s", libusb_error_name(ret));
        return -1;
    }

    ssize_t cnt = libusb_get_device_list(ctx, &devlist);
    if (cnt < 0) {
        LOG_E("获取USB设备列表失败: %s", libusb_error_name((int)cnt));
        libusb_exit(ctx);
        return -1;
    }

    libusb_device *target = NULL;

    /* 在设备列表中查找目标 */
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        libusb_get_device_descriptor(devlist[i], &desc);
        if (desc.idVendor == vid && desc.idProduct == pid) {
            target = devlist[i];
            break;
        }
    }

    if (!target) {
        LOG_E("未找到 VID=0x%04x PID=0x%04x 的USB设备", vid, pid);
        libusb_free_device_list(devlist, 1);
        libusb_exit(ctx);
        return -1;
    }

    /* ===== 设备描述符 ===== */
    struct libusb_device_descriptor dev_desc;
    libusb_get_device_descriptor(target, &dev_desc);

    LOG_I("╔══════════════════════════════════════════════════════════════╗");
    LOG_I("║              USB 设备描述符 (Device Descriptor)              ║");
    LOG_I("╚══════════════════════════════════════════════════════════════╝");
    LOG_I("  bLength            = %u (描述符长度)", dev_desc.bLength);
    LOG_I("  bDescriptorType    = %u (1=设备描述符)", dev_desc.bDescriptorType);
    LOG_I("  bcdUSB             = %d.%02d (USB规范版本)",
          dev_desc.bcdUSB >> 8, dev_desc.bcdUSB & 0xff);
    LOG_I("  bDeviceClass       = %u (%s)", dev_desc.bDeviceClass,
          dev_desc.bDeviceClass == 0xEF ? "Miscellaneous (使用IAD)" :
          dev_desc.bDeviceClass == 0x00 ? "由接口定义" : "其他");
    LOG_I("  bDeviceSubClass    = %u", dev_desc.bDeviceSubClass);
    LOG_I("  bDeviceProtocol    = %u (%s)", dev_desc.bDeviceProtocol,
          dev_desc.bDeviceProtocol == 1 ? "IAD (接口关联描述符)" : "其他");
    LOG_I("  bMaxPacketSize0    = %u (EP0最大包大小)", dev_desc.bMaxPacketSize0);
    LOG_I("  idVendor           = 0x%04x", dev_desc.idVendor);
    LOG_I("  idProduct          = 0x%04x", dev_desc.idProduct);
    LOG_I("  bcdDevice          = %d.%02d (设备版本)",
          dev_desc.bcdDevice >> 8, dev_desc.bcdDevice & 0xff);
    LOG_I("  bNumConfigurations = %u", dev_desc.bNumConfigurations);

    /* 获取字符串描述符 */
    libusb_device_handle *handle = NULL;
    ret = libusb_open(target, &handle);
    if (ret == 0) {
        unsigned char str_buf[256];
        if (dev_desc.iManufacturer) {
            libusb_get_string_descriptor_ascii(handle, dev_desc.iManufacturer, str_buf, sizeof(str_buf));
            LOG_I("  iManufacturer      = \"%s\"", str_buf);
        }
        if (dev_desc.iProduct) {
            libusb_get_string_descriptor_ascii(handle, dev_desc.iProduct, str_buf, sizeof(str_buf));
            LOG_I("  iProduct           = \"%s\"", str_buf);
        }
        if (dev_desc.iSerialNumber) {
            libusb_get_string_descriptor_ascii(handle, dev_desc.iSerialNumber, str_buf, sizeof(str_buf));
            LOG_I("  iSerialNumber      = \"%s\"", str_buf);
        }
    }

    /* ===== 配置描述符 + 类特定描述符 ===== */
    struct libusb_config_descriptor *config;
    ret = libusb_get_active_config_descriptor(target, &config);
    if (ret < 0) {
        LOG_E("获取配置描述符失败: %s", libusb_error_name(ret));
        if (handle) libusb_close(handle);
        libusb_free_device_list(devlist, 1);
        libusb_exit(ctx);
        return -1;
    }

    LOG_I("");
    LOG_I("╔══════════════════════════════════════════════════════════════╗");
    LOG_I("║            配置描述符 (Configuration Descriptor)            ║");
    LOG_I("╚══════════════════════════════════════════════════════════════╝");
    LOG_I("  bNumInterfaces     = %u", config->bNumInterfaces);
    LOG_I("  bConfigurationValue= %u", config->bConfigurationValue);
    LOG_I("  bmAttributes       = 0x%02x (%s)", config->bmAttributes,
          (config->bmAttributes & 0x40) ? "自供电" : "总线供电");
    LOG_I("  MaxPower           = %u mA", config->MaxPower * 2);

    /*
     * 遍历所有接口及其备选设置
     * UVC设备通常有:
     *   接口0: 视频控制(VC) - 只有1个备选设置
     *   接口1: 视频流(VS)   - 多个备选设置(不同带宽)
     */
    for (int i = 0; i < config->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &config->interface[i];

        for (int j = 0; j < iface->num_altsetting; j++) {
            const struct libusb_interface_descriptor *altsetting = &iface->altsetting[j];

            LOG_I("");
            LOG_I("┌──────────────────────────────────────────────────────────┐");
            LOG_I("│ 接口 %d, 备选设置 %d", altsetting->bInterfaceNumber, altsetting->bAlternateSetting);
            LOG_I("└──────────────────────────────────────────────────────────┘");
            LOG_I("  bInterfaceClass    = %u (%s)", altsetting->bInterfaceClass,
                  altsetting->bInterfaceClass == 14 ? "Video (视频类)" :
                  altsetting->bInterfaceClass == 1 ? "Audio (音频类)" : "其他");
            LOG_I("  bInterfaceSubClass = %u (%s)", altsetting->bInterfaceSubClass,
                  altsetting->bInterfaceSubClass == 1 ? "Video Control (视频控制)" :
                  altsetting->bInterfaceSubClass == 2 ? "Video Streaming (视频流)" : "其他");
            LOG_I("  bNumEndpoints      = %u", altsetting->bNumEndpoints);

            /*
             * 解析类特定描述符 (Class-Specific Descriptors)
             * 这些藏在 extra/extra_length 里，标准libusb不解析它们
             */
            if (altsetting->extra_length > 0) {
                LOG_I("  --- 类特定描述符 (extra_length=%d) ---", altsetting->extra_length);
                logger_hexdump(LOG_LEVEL_DEBUG, "原始额外描述符数据",
                               altsetting->extra, altsetting->extra_length);

                const unsigned char *p = altsetting->extra;
                int remaining = altsetting->extra_length;

                while (remaining >= 2) {
                    uint8_t bLen = p[0];
                    uint8_t bType = p[1];

                    if (bLen < 2 || bLen > remaining)
                        break;

                    /*
                     * bDescriptorType:
                     *   0x24 (36) = CS_INTERFACE (类特定接口描述符)
                     *   0x25 (37) = CS_ENDPOINT  (类特定端点描述符)
                     */
                    if (bType == 0x24) {  /* CS_INTERFACE */
                        if (altsetting->bInterfaceSubClass == 1) {
                            /* 视频控制接口的类特定描述符 */
                            parse_vc_descriptor(p, bLen, info);
                        } else if (altsetting->bInterfaceSubClass == 2) {
                            /* 视频流接口的类特定描述符 */
                            parse_vs_descriptor(p, bLen);
                        }
                    } else if (bType == 0x25) {
                        LOG_I("  [CS_ENDPOINT] 类特定端点描述符, 长度=%d", bLen);
                    }

                    p += bLen;
                    remaining -= bLen;
                }
            }

            /* 端点描述符 */
            for (int k = 0; k < altsetting->bNumEndpoints; k++) {
                const struct libusb_endpoint_descriptor *ep = &altsetting->endpoint[k];
                LOG_I("  [ENDPOINT] 端点描述符:");
                LOG_I("    bEndpointAddress = 0x%02x (EP %d %s)",
                      ep->bEndpointAddress,
                      ep->bEndpointAddress & 0x0f,
                      (ep->bEndpointAddress & 0x80) ? "IN(设备→主机)" : "OUT(主机→设备)");
                LOG_I("    bmAttributes     = 0x%02x (传输类型: %s)",
                      ep->bmAttributes,
                      (ep->bmAttributes & 0x03) == 0 ? "控制" :
                      (ep->bmAttributes & 0x03) == 1 ? "等时(Isochronous,实时视频)" :
                      (ep->bmAttributes & 0x03) == 2 ? "批量(Bulk)" :
                      "中断(Interrupt)");
                LOG_I("    wMaxPacketSize   = %u 字节", ep->wMaxPacketSize);
                LOG_I("    bInterval        = %u", ep->bInterval);
            }
        }
    }

    /* 输出数据流拓扑关系 */
    if (info && info->xu_count > 0) {
        LOG_I("");
        LOG_I("╔══════════════════════════════════════════════════════════════╗");
        LOG_I("║                  UVC 数据流拓扑总结                         ║");
        LOG_I("╚══════════════════════════════════════════════════════════════╝");
        LOG_I("  摄像头传感器(IT) → 处理单元(PU) → 扩展单元(XU) → USB流(OT)");
        LOG_I("");
        LOG_I("  发现 %d 个扩展单元:", info->xu_count);
        for (int i = 0; i < info->xu_count; i++) {
            char guid_str[64];
            usb_desc_guid_to_str(info->xus[i].guid, guid_str, sizeof(guid_str));
            LOG_I("    XU #%d: unit_id=%u, GUID=%s, %u个控制",
                  i + 1, info->xus[i].unit_id, guid_str, info->xus[i].num_controls);
        }
    }

    libusb_free_config_descriptor(config);
    if (handle) libusb_close(handle);
    libusb_free_device_list(devlist, 1);
    libusb_exit(ctx);

    LOG_I("USB描述符dump完成");
    return 0;
}
