# 设计文档:run-cam 新功能 — 判断 USB 摄像头能否用于鱼缸 ESP32

- 日期:2026-06-24
- 项目:`run-cam`(USB 摄像头 UVC 探测工具)
- 目标平台:`fish-tank-esp32`(ESP32-P4 + `espressif/usb_host_uvc` 2.5.1,摄像头经 USB hub 连接)

## 1. 目标与背景

鱼缸 ESP32 通过 USB hub 接 USB 摄像头。要在现场换/选摄像头时,先用 `run-cam`
(在一台 Linux 机器上)把候选摄像头插上,跑一个命令,**直接判断它能否用在 ESP32 上**,
并列出可用的格式/分辨率组合。判定规则全部来自对 ESP32 侧代码的核实(见 §3 出处)。

### 1.1 ESP32 侧已核实的事实(均经源码核对)

- UVC 组件 = `espressif/usb_host_uvc` **2.5.1**;板子 = ESP32-P4(USB 高速 HS)。
- 默认请求格式 = **MJPEG 640×480@30**(`sdkconfig`:`CONFIG_FISHTANK_UVC_FMT_MJPEG=y`,
  `CONFIG_FISHTANK_UVC_DEFAULT_WIDTH=640 / HEIGHT=480 / FPS=30`);
  打开失败回退一次到同一 kDefault(`main/camera/uvc_camera.cc:85-90`)。
- UVC 流接口协商(`managed_components/espressif__usb_host_uvc/uvc_descriptor_parsing.c:66-110`):
  遍历视频流(VS)接口所有 altsetting,挑 isoc 端点 MPS ≤ 设备 dwMaxPayloadTransferSize 的最大者;
  **要求每个 streaming altsetting 恰好 1 个端点,且只取 index 0(第一个)端点**(`:91`、`:94`);
  接口必须 `bInterfaceClass==USB_CLASS_VIDEO (0x0e)`(`:55`、`:86`)。
- **过-hub 决定性约束(ESP-IDF 5.5.2 `components/usb/hub.c:344-351`)**:
  ESP-IDF USB host **没有 TT(Transaction Translator)层**。当 hub 以高速运行而接入设备不是高速时,
  hub 驱动直接禁用端口、拒绝该设备。鱼缸用的是高速 hub ⇒ **摄像头本身必须是高速(HS / USB 2.0 480Mbps)**;
  全速(FS)摄像头挂在高速 hub 后用不了。

## 2. 架构与模块划分

整个判定**纯基于 libusb(USB 描述符)**,不依赖 V4L2 / `/dev/video` / uvcvideo。
理由:① 忠实于 ESP32 实际所见(ESP32 也只读 USB 描述符,没有 V4L2);
② 可评估没有视频节点的设备(如非 UVC 的厂商私有摄像头);
③ 摆脱"uvcvideo 未加载就用不了"的脆弱依赖。

```
main.c (命令 11: cmd_esp32_compat)
  │  1) 选目标摄像头(自动检测 UVC / 手动输入 VID:PID)
  │  2) usb_desc_dump(vid,pid) → 解析并捕获 USB 描述符信息
  ▼
usb_desc.c/.h  (扩展:除 XU 外,再捕获 设备速度 + VS流接口端点 + 格式/帧 信息)
  │            usb_desc_info_t (扩展后)
  ▼
esp32_compat.c/.h  (新模块:ESP32 规则集中地,纯函数判定)
  │  esp32_compat_check(const usb_desc_info_t*, const target*) → report
  ▼
  esp32_compat_print_report(report)  → 分项报告(✓/✗/⚠)+ 总判定
```

- `usb_desc` 只负责"读设备说了什么"。
- `esp32_compat` 只负责"ESP32 的规矩"。判定核心 `esp32_compat_check()` 是**纯函数**
  (输入 const 结构体 + 目标配置,输出 report,不做任何 I/O),便于将来喂构造数据做单测。

## 3. 摄像头目标选择(替换硬编码 0bda:5846)

现状:`main.c` 用 `#define USB_VID/USB_PID` 写死 `0bda:5846`,无法评估其他摄像头。

改为运行时确定的全局目标 `g_target_vid / g_target_pid`:

1. **自动检测**:用 libusb 扫描所有 USB 设备,找出"摄像头候选"——
   含 `bInterfaceClass==Video(0x0e)` 接口的设备(标准 UVC)。
2. 命中 1 个 ⇒ 直接采用;命中多个 ⇒ 列表让用户选;命中 0 个 ⇒ 提示并允许手动输入。
3. **手动指定**:命令 11 内允许用户输入 `VID:PID`(十六进制),以便评估任意设备
   (包括非 UVC 的厂商私有摄像头,使其得到"非 UVC ⇒ 不兼容"的判定)。
4. 现有功能 1–10 改用 `g_target_vid/g_target_pid`(`usb_desc_dump`/`find_capture_device` 本就收 vid/pid 参数,主要是把常量换成全局)。
   启动时做一次自动检测填充全局;检测不到则保留可手动设定。

## 4. 数据结构扩展(`usb_desc.h`)

`usb_desc.c` 现在解析 VS 接口端点与格式/帧时只 `LOG`,不保存。新增以下捕获:

```c
/* 一个视频流 altsetting 的端点摘要 */
typedef struct {
    uint8_t  alt_setting;     /* bAlternateSetting */
    uint8_t  num_endpoints;   /* 该 alt 的端点数 —— UVC 要求 streaming alt 恰好 1 */
    uint8_t  ep_address;      /* 第一个端点地址 */
    uint8_t  transfer_type;   /* 0=控制 1=isoc 2=bulk 3=中断 */
    uint16_t mps;             /* wMaxPacketSize 基础值(低 11 位) */
    uint8_t  mult;            /* 高带宽乘子(HS isoc;每微帧 mult+1 个事务) */
} vs_altsetting_t;

typedef struct {
    int             present;            /* 是否找到 VS(0x0e/0x02)接口 */
    uint8_t         interface_number;
    int             alt_count;
    vs_altsetting_t alts[16];
} vs_stream_info_t;

/* 一种格式下的一个分辨率(含帧率与码率) */
typedef struct {
    uint16_t width, height;
    uint32_t dwMaxBitRate;          /* bps,来自 FRAME 描述符,用于带宽估算 */
    int      interval_count;
    double   fps[16];               /* 由 dwFrameInterval(100ns)换算 */
} desc_frame_t;

typedef struct {
    int          is_mjpeg;          /* MJPEG=1,未压缩(YUYV等)=0 */
    char         fourcc[5];         /* 未压缩格式的 GUID 前 4 字符;MJPEG 填 "MJPG" */
    int          frame_count;
    desc_frame_t frames[32];
} desc_format_t;

/* 挂到 usb_desc_info_t 上 */
typedef struct {
    int              xu_count;
    xu_info_t        xus[MAX_XU_COUNT];
    int              usb_speed;     /* libusb_get_device_speed() 结果 */
    vs_stream_info_t vs;            /* ← 新增:流接口端点 */
    int              format_count;  /* ← 新增 */
    desc_format_t    formats[16];   /* ← 新增:从 VS FORMAT/FRAME 描述符捕获 */
} usb_desc_info_t;
```

捕获点:`usb_desc.c` 在遍历端点处(`:734-746`)填 `vs.alts[]`;在 `parse_vs_descriptor`
的 FORMAT/FRAME 分支填 `formats[]`;在最外层调一次 `libusb_get_device_speed(target)` 填 `usb_speed`。

## 5. 目标配置常量(ESP32 的"标准答案")

ESP32 的配置在它自己的 `sdkconfig`(另一个项目),run-cam 读不到 ⇒
在 `esp32_compat.h` 硬编码成带注释常量,注明出处,便于升级时一处修改:

```c
typedef enum { ESP32_FMT_MJPEG, ESP32_FMT_YUYV } esp32_fmt_t;

typedef struct {
    esp32_fmt_t fmt;      /* 目标格式 */
    uint16_t    width, height;
    uint16_t    fps;
    int         hub_is_high_speed;  /* 鱼缸用高速 hub ⇒ 1 */
} esp32_compat_target_t;

/* 默认目标 = fish-tank-esp32 当前配置(出处见注释) */
#define ESP32_DEFAULT_TARGET (esp32_compat_target_t){ \
    .fmt = ESP32_FMT_MJPEG,   /* CONFIG_FISHTANK_UVC_FMT_MJPEG=y */ \
    .width = 640,             /* CONFIG_FISHTANK_UVC_DEFAULT_WIDTH */ \
    .height = 480,            /* CONFIG_FISHTANK_UVC_DEFAULT_HEIGHT */ \
    .fps = 30,                /* CONFIG_FISHTANK_UVC_DEFAULT_FPS */ \
    .hub_is_high_speed = 1,   /* 高速 hub;ESP-IDF 5.5.2 无 TT(hub.c:344-351) */ \
}
```

## 6. 判定规则(核心)

`esp32_compat_check()` 逐条产出 `{status: PASS/FAIL/WARN/INFO, 文案}`,最后给总判定。

| # | 约束 | 判据(数据来源) | 结果 |
|---|------|------|------|
| 0 | **是 UVC 视频类设备** | `vs.present`(存在 0x0e/0x02 流接口) | 否 → **✗ 致命**(非 UVC,ESP32 组件 `bInterfaceClass==Video` 检查失败) |
| 1 | **设备速度 / TT** | `usb_speed` | HIGH/SUPER/SUPER+ → ✓;FULL/LOW → **✗ 致命**(过高速 hub 被拒,`hub.c:344-351`);UNKNOWN → ⚠ |
| 2 | **UVC 单端点** | `vs.alts[]` 每个带端点的 alt 的 `num_endpoints` | 全部 ==1 → ✓;任一 >1 → ✗(`uvc_descriptor_parsing.c:91`)。同时报告 isoc/bulk |
| 3 | **格式** | `formats[]` 是否含目标格式(默认 MJPEG) | 含 → ✓;不含但有 YUYV → ⚠(需改 Kconfig,YUYV 带宽大);都没有 → ✗ |
| 4 | **分辨率/帧率(列举+高亮)** | 目标格式下所有 `frames[]` | 列出全部组合;默认 640×480@30 在列 → ✓;不在但有其他 → ⚠(改 Kconfig 可用) |
| 5 | **带宽(估算,不单独否决)** | 目标分辨率的 `dwMaxBitRate` vs 单 isoc 端点上限(`max(mps*(mult+1))*8000`) | 够 → ✓;不够 → ⚠;明确标注"估算,非运行时 PROBE 实测" |

**总判定**:
- 约束 0/1/2/3 任一 ✗ ⇒ **不可用(FAIL)**。
- 默认分辨率不在列、但存在其他可用组合 ⇒ **改配置可用(CONDITIONAL)**。
- 全 ✓ ⇒ **可用(PASS)**。
- 含 ⚠/UNKNOWN ⇒ 在结论后附"需人工确认"的提示。

## 7. 数据流与命令交互

命令 11 `cmd_esp32_compat()`:
1. 选目标(§3:自动检测;或用户手动输入 VID:PID)。
2. `usb_desc_dump(vid, pid, &g_desc_info)` 解析描述符(此调用顺带填充 §4 的全部新字段)。
3. `esp32_compat_check(&g_desc_info, &target, &report)`。
4. `esp32_compat_print_report(&report)`。

不依赖功能 1/2 是否先跑过;命令 11 自洽。

## 8. 输出报告样例

PASS:
```
═══ ESP32(鱼缸)兼容性判定 ═══
目标: MJPEG 640×480@30, 过 USB hub, ESP-IDF 5.5.2(无 TT)
设备: 1234:5678  "Example UVC Camera"

[✓] UVC 设备    : 是(VS 流接口 #1)
[✓] 设备速度    : High-Speed (480Mbps) — 可过高速 hub
[✓] UVC 端点    : 4 个 isoc altsetting,每个 1 端点 ✓
[✓] 格式 MJPEG  : 支持
[✓] 默认分辨率  : 640×480@30 直接可用
    可用 MJPEG 组合: 1280×720@15, 640×480@30 ←默认, 320×240@30
[~] 带宽(估算)  : 640×480@30 约 24Mbps ≤ 单端点 isoc 上限(~196Mbps)OK

总判定: ✅ 可用 (PASS)
提示: 速度读数依赖把摄像头插在 Linux 的 USB2.0/3.0 口直连(勿经全速 hub)
```

FAIL(非 UVC,本机现接的 useepluscam 即此类):
```
[✗] UVC 设备    : 否 — 仅含厂商私有接口(class 0xff),无 Video 流接口
总判定: ❌ 不可用 (FAIL) — 非 UVC 视频类设备,ESP32 usb_host_uvc 无法驱动
```

FAIL(全速):`[✗] 设备速度: Full-Speed — 过高速 hub 会被 ESP-IDF 拒绝(无 TT)`。

## 9. 错误处理与边界

- 目标设备未连接 / libusb 打不开 ⇒ 同功能 1 的报错提示(权限/连接)。
- 速度 UNKNOWN ⇒ ⚠ 并建议换 USB2.0/3.0 口直连重试。
- 无 VS 接口 ⇒ 约束 0 判 FAIL(非有效 UVC 流设备)。
- 目标格式下无任何分辨率 ⇒ 约束 4 判 ✗。

## 10. 测试

- 核心 `esp32_compat_check()` 为纯函数,便于将来喂构造 `usb_desc_info_t` 做单测。
- 真机:
  - **PASS 路径**:插一个标准 UVC 摄像头,跑命令 11,核对各项与总判定。
  - **FAIL 路径**:对本机的厂商私有摄像头 `2ce3:3828 useepluscam`(class 0xff、无视频节点)
    手动指定 VID:PID,核对得到"非 UVC ⇒ FAIL"。
- 构建:沿用现有 `make`(libusb-1.0 + readline),`build/uvc-tool`。

## 11. 不做(YAGNI)

- 不做运行时 UVC PROBE/COMMIT 真协商(带宽只做估算)。
- 不解析音频/HID 等其他类的接口。
- 不改 ESP32 项目本身;本功能只在 Linux 侧评估摄像头。
