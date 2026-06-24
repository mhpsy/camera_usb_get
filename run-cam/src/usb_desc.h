/*
 * usb_desc.h - USB 描述符解析模块
 *
 * 使用 libusb 直接读取 USB 描述符，并解析 UVC 特有的视频控制/视频流描述符。
 * 这是理解 UVC 协议的核心：了解设备通过描述符告诉主机它有哪些能力。
 */

#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

/*
 * UVC 描述符子类型 (bDescriptorSubtype)
 *
 * 在UVC协议中，标准USB接口描述符之后会跟着一系列"类特定"描述符，
 * 用 bDescriptorSubtype 区分不同含义。
 */

/* === 视频控制(Video Control, VC)接口描述符子类型 === */
#define UVC_VC_HEADER          0x01 /* VC接口头：包含UVC版本、时钟频率等全局信息 */
#define UVC_VC_INPUT_TERMINAL  0x02 /* 输入终端：数据的来源，如摄像头传感器 */
#define UVC_VC_OUTPUT_TERMINAL 0x03 /* 输出终端：数据的去处，通常是USB流 */
#define UVC_VC_SELECTOR_UNIT   0x04 /* 选择单元：可在多个输入中选择一个 */
#define UVC_VC_PROCESSING_UNIT 0x05 /* 处理单元：提供亮度/对比度/饱和度等图像处理 */
#define UVC_VC_EXTENSION_UNIT  0x06 /* 扩展单元(XU)：厂商私有功能，如LED控制、HDR等 */

/* === 视频流(Video Streaming, VS)接口描述符子类型 === */
#define UVC_VS_INPUT_HEADER        0x01 /* VS输入头：描述流的格式数量和端点 */
#define UVC_VS_OUTPUT_HEADER       0x02 /* VS输出头 */
#define UVC_VS_FORMAT_UNCOMPRESSED 0x04 /* 未压缩格式（如YUY2, NV12） */
#define UVC_VS_FRAME_UNCOMPRESSED  0x05 /* 未压缩帧（分辨率+帧率） */
#define UVC_VS_FORMAT_MJPEG        0x06 /* MJPEG格式 */
#define UVC_VS_FRAME_MJPEG         0x07 /* MJPEG帧（分辨率+帧率） */
#define UVC_VS_FORMAT_FRAME_BASED  0x10 /* 基于帧的格式（如H.264） */
#define UVC_VS_FRAME_FRAME_BASED   0x11 /* 基于帧的帧描述符 */
#define UVC_VS_COLOR_FORMAT        0x0D /* 颜色匹配描述符 */

/*
 * 存储解析后的扩展单元信息
 */
typedef struct {
    uint8_t  unit_id;      /* 单元ID，用于后续XU控制命令中标识目标 */
    uint8_t  guid[16];     /* 全局唯一标识符，标识XU的功能集 */
    uint8_t  num_controls; /* 该XU支持的控制数量 */
    uint8_t  source_id;    /* 数据源ID（上游单元） */
    uint32_t bmControls;   /* 控制位图，每一位代表一个控制是否存在 */
} xu_info_t;

#define MAX_XU_COUNT 8

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

typedef struct {
    int              xu_count;
    xu_info_t        xus[MAX_XU_COUNT];
    int              usb_speed;     /* libusb_get_device_speed() 结果 */
    vs_stream_info_t vs;            /* 新增:流接口端点 */
    int              format_count;  /* 新增 */
    desc_format_t    formats[16];   /* 新增:从 VS FORMAT/FRAME 描述符捕获 */
} usb_desc_info_t;

/*
 * 打印完整的USB描述符信息，包括：
 * - 设备描述符 (Device Descriptor)
 * - 配置描述符 (Configuration Descriptor)
 * - 接口关联描述符 (IAD)
 * - 视频控制接口描述符 (VC Interface)
 * - 视频流接口描述符 (VS Interface)
 * - 端点描述符 (Endpoint)
 *
 * vid, pid: USB 厂商ID和产品ID (传0则自动搜索UVC设备)
 * info: 输出参数，返回解析到的XU信息
 */
int usb_desc_dump(uint16_t vid, uint16_t pid, usb_desc_info_t *info);

/*
 * 用于打印 GUID 为可读字符串
 */
void usb_desc_guid_to_str(const uint8_t guid[16], char *buf, int buf_len);

/*
 * 一个 USB 摄像头候选(用于自动检测目标设备)
 */
typedef struct {
    uint16_t vid, pid;
    char     product[64];
    int      is_uvc; /* 含 Video(0x0e)接口 */
} usb_cam_cand_t;

/*
 * 扫描连接的 USB 设备,返回 UVC 摄像头候选(写入 out,最多 max 个),返回个数。
 * 只自动收含 Video(0x0e)接口的标准 UVC 设备;非 UVC 设备靠手动指定 VID:PID 评估。
 */
int usb_find_cameras(usb_cam_cand_t *out, int max);

#endif /* USB_DESC_H */
