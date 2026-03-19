# USB 摄像头技术文档
## 设备：Sunplus SPCA2650 PC Camera → 单片机移植研究指南

> 生成日期：2026-03-18
> 适用设备：`/dev/video0` — `SunplusIT Inc SPCA2650 PC Camera`
> Vendor ID: `0x1bcf`，Product ID: `0x0b15`

---

## 一、设备基础信息

| 字段 | 值 |
|------|----|
| 设备名称 | SPCA2650 PC Camera |
| 厂商 | Sunplus Innovation Technology Inc. |
| Vendor ID | `0x1bcf` |
| Product ID | `0x0b15` |
| 硬件版本 | `0x0318`（十进制 792） |
| 序列号 | `01.00.00` |
| Linux 驱动 | `uvcvideo` |
| USB 总线 | Bus 001, Port 003 |
| USB 速度 | **480 Mbps（USB 2.0 High Speed）** |
| USB 接口类 | `0x0e`（Video Class，即 UVC） |
| 接口数量 | 2 个（Interface 0: Video Control，Interface 1: Video Streaming） |
| 设备节点 | `/dev/video0` |

---

## 二、驱动层：uvcvideo 是如何工作的

### 2.1 驱动名称及位置

Linux 内核中的 `uvcvideo` 驱动（`drivers/media/usb/uvc/`）负责驱动这个摄像头。

这个驱动实现了 **USB Video Class（UVC）** 规范，UVC 是 USB IF 组织制定的标准，只要摄像头声明自己是 UVC 设备，Linux/Windows/macOS 均无需安装额外驱动即可识别。

### 2.2 驱动加载流程（Linux 内核侧）

```
插入 USB 设备
    ↓
USB 枚举（xhci_hcd 控制器）
    ↓
读取设备描述符：Class=0x0e (Video), SubClass=0x01/0x02
    ↓
内核 USB Core 匹配到 uvcvideo 驱动
    ↓
uvcvideo 解析 UVC 描述符（Video Control Interface + Video Streaming Interface）
    ↓
注册 V4L2 设备 → 创建 /dev/video0
    ↓
用户空间通过 V4L2 ioctl 与驱动通信
```

### 2.3 USB 接口结构

这台摄像头有两个 USB 接口（通过 IAD—Interface Association Descriptor 绑定在一起）：

| 接口 | 编号 | 类型 | 作用 |
|------|------|------|------|
| Video Control Interface (VCI) | Interface 0 | `0x0e / 0x01` | 控制摄像头参数（亮度、曝光等），使用 **Control Endpoint（EP0 默认控制管道）** |
| Video Streaming Interface (VSI) | Interface 1 | `0x0e / 0x02` | 传输视频数据，使用 **Bulk 或 Isochronous Endpoint** |

### 2.4 Linux 系统中的 sysfs 路径

```
/sys/bus/usb/devices/1-3/          ← USB 设备
/sys/bus/usb/devices/1-3:1.0/      ← Interface 0 (Video Control)
/sys/bus/usb/devices/1-3:1.1/      ← Interface 1 (Video Streaming)
/sys/class/video4linux/video0/     ← V4L2 设备节点
```

---

## 三、通信协议：UVC 协议详解

UVC（USB Video Class）是整个通信的核心。理解它对单片机开发至关重要。

### 3.1 UVC 架构总览

```
主机（Host / 单片机）
    │
    ├── EP0（Default Control Pipe，双向）
    │       └── 发送 UVC 控制请求（SET/GET 亮度、曝光等）
    │
    └── EP_IN（Isochronous 或 Bulk，单向 IN）
            └── 接收视频数据流（包含 UVC Payload Header）
```

### 3.2 控制传输（Control Transfer）

所有摄像头参数的读取和设置，走的是 EP0 标准控制管道，使用 `CLASS` 类型请求。

**请求格式（bmRequestType + bRequest）：**

| 字段 | 读参数（GET） | 写参数（SET） |
|------|--------------|--------------|
| `bmRequestType` | `0xA1`（Device-to-Host, Class, Interface） | `0x21`（Host-to-Device, Class, Interface） |
| `bRequest` | `GET_CUR = 0x81` / `GET_MIN = 0x82` / `GET_MAX = 0x83` / `GET_DEF = 0x87` | `SET_CUR = 0x01` |
| `wValue` | `(CS << 8) | 0x00`（CS=Control Selector） | 同左 |
| `wIndex` | `(EntityID << 8) | InterfaceNumber` | 同左 |
| `wLength` | 参数长度（字节） | 参数长度 |

**常用 Control Selector（CS）示例：**

| 参数 | Entity | CS | 长度 | 范围 |
|------|--------|----|------|------|
| 亮度 Brightness | PU（Processing Unit） | `0x02` | 2 字节（int16） | -64 ~ 64 |
| 对比度 Contrast | PU | `0x03` | 2 字节 | 0 ~ 95 |
| 饱和度 Saturation | PU | `0x07` | 2 字节 | 0 ~ 100 |
| 色调 Hue | PU | `0x06` | 2 字节 | -2000 ~ 2000 |
| 白平衡自动 | PU | `0x0C` | 1 字节（bool） | 0/1 |
| 白平衡温度 | PU | `0x0A` | 2 字节 | 2800 ~ 6500 |
| Gamma | PU | `0x09` | 2 字节 | 100 ~ 300 |
| 锐度 Sharpness | PU | `0x0B` | 2 字节 | 1 ~ 7 |
| 背光补偿 | PU | `0x01` | 2 字节 | 0 ~ 1 |
| 曝光模式 Auto | CT（Camera Terminal） | `0x02` | 1 字节 | 1=手动, 3=光圈优先 |
| 曝光时间 | CT | `0x04` | 4 字节 | 3 ~ 2047 |
| 工频干扰 | PU | `0x05` | 1 字节 | 0=关, 1=50Hz, 2=60Hz |

### 3.3 视频流传输（Streaming）

视频数据通过 **Isochronous IN Endpoint** 传输（这台摄像头是 `ep_87`，即地址 `0x87 = IN | 0x07`）。

**启动视频流的步骤：**

```
1. 发送 VS_PROBE_CONTROL (SET_CUR)   → 探测协商参数（格式/分辨率/帧率）
2. 发送 VS_COMMIT_CONTROL (SET_CUR)  → 确认提交参数
3. 切换 Alternate Setting           → 激活 Isochronous 端点带宽
4. 提交 URB（USB Request Block）     → 开始接收数据
```

**Probe/Commit 结构体（`UVC_VS_PROBE_COMMIT_CONTROL`，26 字节）：**

```c
typedef struct {
    uint16_t bmHint;               // 协商提示位
    uint8_t  bFormatIndex;         // 格式索引（1=MJPG, 2=YUYV）
    uint8_t  bFrameIndex;          // 帧索引（分辨率，从1开始）
    uint32_t dwFrameInterval;      // 帧间隔（100ns单位，如333333=30fps）
    uint16_t wKeyFrameRate;        // 关键帧率（MJPG用）
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;  // 最大帧大小（字节）
    uint32_t dwMaxPayloadTransferSize; // 最大载荷传输大小
} uvc_stream_control_t;
```

**UVC Payload Header（每个 USB 包前缀，最小 2 字节）：**

```
Byte 0: HLE（Header Length，通常=2）
Byte 1: BFH（Bit Field Header）
    bit0: FID（Frame ID，交替0/1，标识新帧开始）
    bit1: EOF（End of Frame，=1 表示这是帧的最后一包）
    bit2: PTS（是否有 Presentation Time Stamp）
    bit3: SCR（是否有 Source Clock Reference）
    bit6: ERR（错误标志）
    bit7: EOH（End of Header）
```

---

## 四、支持的分辨率和格式

### 4.1 MJPG 格式（Motion-JPEG，压缩）

| 分辨率 | 帧率 | 帧间隔 |
|--------|------|--------|
| 1280x720 | **15 fps** | 66.7ms |
| 1280x712 | 15 fps | 66.7ms |
| 1280x480 | 20 fps | 50ms |
| **640x480** | **30 fps** | 33.3ms |
| 640x472 | 30 fps | 33.3ms |

> MJPG 是压缩格式，每帧是一个独立的 JPEG 图像。数据量小，适合高分辨率。
> 在单片机上需要 JPEG 解码能力（或直接传输给显示器）。

### 4.2 YUYV 格式（YUV 4:2:2，未压缩）

| 分辨率 | 帧率 | 每帧大小 | 带宽需求 |
|--------|------|----------|----------|
| 1280x720 | **10 fps** | 1,843,200 字节 (~1.8MB) | ~18 MB/s |
| 1280x712 | 10 fps | 1,822,720 字节 | ~18 MB/s |
| 1280x480 | 10 fps | 1,228,800 字节 | ~12 MB/s |
| **640x480** | **30 fps** | 614,400 字节 (~600KB) | ~18 MB/s |
| 640x472 | 10 fps | 604,160 字节 | ~6 MB/s |

> YUYV 是原始像素格式，每个像素 2 字节。数据量大，但处理简单（无需解码）。
> **640x480 @ 30fps** 是最常用的配置，与默认配置一致。

### 4.3 选择建议

| 场景 | 推荐格式 | 原因 |
|------|----------|------|
| 单片机资源受限 | MJPG 640x480 30fps | 压缩数据，USB 带宽需求低 |
| 需要直接处理像素 | YUYV 640x480 30fps | 无需解码，直接读取像素值 |
| 高分辨率采集 | MJPG 1280x720 15fps | 压缩后带宽可接受 |

---

## 五、可配置参数完整列表

### 5.1 图像质量参数（Processing Unit）

| 参数 | 默认值 | 范围 | 步长 | 说明 |
|------|--------|------|------|------|
| brightness（亮度） | 0 | -64 ~ 64 | 1 | 0为中性 |
| contrast（对比度） | 0 | 0 ~ 95 | 1 | |
| saturation（饱和度） | 64 | 0 ~ 100 | 1 | |
| hue（色调） | 0 | -2000 ~ 2000 | 1 | |
| gamma（伽马） | 100 | 100 ~ 300 | 1 | |
| sharpness（锐度） | 2 | 1 ~ 7 | 1 | |
| backlight_compensation（背光补偿） | 1 | 0 ~ 1 | 1 | |
| white_balance_automatic（自动白平衡） | 1（开） | bool | — | 开启时温度值无效 |
| white_balance_temperature（白平衡温度） | 4600K | 2800 ~ 6500 | 1 | 需关闭自动白平衡 |
| power_line_frequency（工频消除） | 2（60Hz） | 0/1/2 | — | 0=关,1=50Hz,2=60Hz |

### 5.2 相机控制参数（Camera Terminal）

| 参数 | 默认值 | 范围 | 说明 |
|------|--------|------|------|
| auto_exposure（曝光模式） | 3（光圈优先） | 1=手动, 3=自动 | |
| exposure_time_absolute（曝光时间） | 166 | 3 ~ 2047 | 单位：100µs，仅手动模式有效 |

### 5.3 用 v4l2-ctl 操作示例

```bash
# 读取当前亮度
v4l2-ctl --device=/dev/video0 --get-ctrl=brightness

# 设置亮度为 20
v4l2-ctl --device=/dev/video0 --set-ctrl=brightness=20

# 切换到手动曝光并设置曝光时间
v4l2-ctl --device=/dev/video0 --set-ctrl=auto_exposure=1
v4l2-ctl --device=/dev/video0 --set-ctrl=exposure_time_absolute=100

# 采集一帧 YUYV 640x480 并保存
v4l2-ctl --device=/dev/video0 \
  --set-fmt-video=width=640,height=480,pixelformat=YUYV \
  --stream-mmap --stream-count=1 --stream-to=frame.yuv

# 采集一帧 MJPG 并保存
v4l2-ctl --device=/dev/video0 \
  --set-fmt-video=width=640,height=480,pixelformat=MJPG \
  --stream-mmap --stream-count=1 --stream-to=frame.jpg
```

---

## 六、单片机开发路线图

### 6.1 核心难点概览

在单片机上驱动 USB 摄像头，本质是实现一个 **USB Host 上的 UVC 类驱动**。

```
单片机
├── USB Host Controller（硬件层）
│     ├── 枚举设备（读描述符）
│     ├── 管理 Endpoint
│     └── 提交 URB
├── UVC 驱动层（你需要实现）
│     ├── 解析 UVC 描述符
│     ├── 发送 Probe/Commit
│     ├── 控制参数读写（EP0）
│     └── 接收视频流（Isochronous IN）
└── 应用层
      ├── JPEG 解码（如使用 MJPG）
      └── 图像处理 / 显示
```

### 6.2 硬件选型建议

| 平台 | USB Host | RAM | 适合场景 |
|------|----------|-----|----------|
| STM32H7xx | USB OTG HS（带 PHY） | 1MB+ | 高性能，能处理 MJPG |
| ESP32-S2/S3 | USB OTG FS/HS | 512KB+ PSRAM | 适合 MJPG 流 |
| Raspberry Pi Pico 2 | USB Host（TinyUSB） | 264KB+外部 | 资源受限，仅低帧率 |
| i.MX RT1060/1064 | USB HS OTG | 1MB | 工业级首选 |

> **关键点**：这台摄像头 640x480 YUYV 30fps 需要约 **18 MB/s** 的 USB 带宽，必须使用 **USB 2.0 High Speed（480Mbps）**。Full Speed（12Mbps）**无法**驱动高帧率，只能用低帧率 MJPG。

### 6.3 USB Host 栈选择

| 软件栈 | 平台支持 | UVC 支持 | 说明 |
|--------|----------|----------|------|
| **TinyUSB** | 多平台 | 有 UVC Host 示例 | 开源，推荐首选 |
| STM32 USB Host Library (USBH) | STM32 | 无官方 UVC，需自写 | 官方库，扩展性一般 |
| CherryUSB | 国产多平台 | 有 UVC Host | 国产开源，文档中文 |
| lwIP + libusb | Linux 嵌入式 | 完整 | 资源占用较大 |

### 6.4 开发步骤（推荐顺序）

#### 阶段一：理解 USB 描述符（必须做）

```
目标：用 Linux 工具 dump 出摄像头的完整 USB 描述符，逐字节理解。

命令：
  lsusb -d 1bcf:0b15 -v 2>/dev/null

重点理解：
  - Configuration Descriptor
  - Interface Association Descriptor (IAD)
  - Video Control Interface Descriptor
  - Video Streaming Interface Descriptor
  - VS_INPUT_HEADER / VS_FORMAT_MJPEG / VS_FRAME_MJPEG
  - VS_FORMAT_UNCOMPRESSED / VS_FRAME_UNCOMPRESSED
  - Endpoint Descriptor（记录 Isochronous EP 地址和最大包大小）
```

#### 阶段二：枚举设备（USB 枚举流程）

```
需要实现的 USB 枚举步骤：
  1. Reset USB bus
  2. GET_DESCRIPTOR(Device Descriptor) → 获取 Vendor/Product ID，确认是 UVC 设备
  3. SET_ADDRESS → 分配地址
  4. GET_DESCRIPTOR(Configuration Descriptor) → 获取完整配置
  5. SET_CONFIGURATION(1) → 激活配置

验证方法：能正确读到 Vendor=0x1bcf, Product=0x0b15 即成功。
```

#### 阶段三：实现 UVC Probe/Commit（协商视频参数）

```c
/* 推荐先从这个配置开始（最容易成功）*/
uvc_stream_control_t probe = {
    .bmHint = 0x0001,          // 固定帧间隔
    .bFormatIndex = 1,          // MJPG（通常是第一个格式）
    .bFrameIndex = 4,           // 640x480（查描述符确认索引）
    .dwFrameInterval = 333333,  // 30fps = 1/30s = 33.3ms = 333333 * 100ns
};

步骤：
  1. SET_CUR(VS_PROBE_CONTROL, probe)
  2. GET_CUR(VS_PROBE_CONTROL) → 读回修正后的参数
  3. SET_CUR(VS_COMMIT_CONTROL, probe) → 提交
```

#### 阶段四：切换 Alternate Setting，激活 Isochronous 端点

```
Video Streaming Interface 通常有多个 Alternate Setting：
  - Alternate 0：带宽为 0（空闲状态）
  - Alternate 1~N：不同带宽分配（按需选择能覆盖 dwMaxPayloadTransferSize 的）

SET_INTERFACE(InterfaceNumber=1, AlternateSetting=N)

注意：对于 Bulk 端点的摄像头，不需要此步骤。
```

#### 阶段五：接收视频数据流

```c
/* 伪代码流程 */
while (streaming) {
    // 提交 Isochronous IN URB
    urb = submit_iso_urb(ep=0x87, buffer=buf, size=max_payload_size);

    // 等待数据
    wait_urb_complete(urb);

    // 解析 UVC Payload Header
    uint8_t hle = buf[0];  // Header Length
    uint8_t bfh = buf[1];  // Bit Field Header

    uint8_t fid = bfh & 0x01;    // Frame ID
    uint8_t eof = (bfh >> 1) & 0x01; // End of Frame

    // 实际数据从 buf[hle] 开始
    uint8_t *data = buf + hle;
    uint32_t data_len = urb->actual_length - hle;

    // 追加到帧缓冲
    append_to_frame_buffer(data, data_len);

    if (eof) {
        // 一帧完整，处理帧
        process_frame(frame_buffer, frame_size);
        reset_frame_buffer();
    }
}
```

#### 阶段六：控制参数（可选）

```c
/* 示例：设置亮度为 20（short 类型，2字节）*/
uint8_t setup_packet[8] = {
    0x21,       // bmRequestType: Host→Device, Class, Interface
    0x01,       // bRequest: SET_CUR
    0x02 << 8,  // wValue: CS=PU_BRIGHTNESS_CONTROL(0x02), 低字节0
    0x00,       // wValue低字节
    unit_id << 8 | interface_num, // wIndex
    0x00,       // wIndex低字节
    0x02,       // wLength: 2字节
    0x00
};
int16_t brightness = 20;
usb_control_transfer(setup_packet, &brightness, 2);
```

### 6.5 内存规划（以 640x480 MJPG 为例）

| 缓冲区 | 大小 | 说明 |
|--------|------|------|
| USB 传输缓冲 | 4KB × N | Isochronous URB 缓冲，建议双缓冲 |
| MJPG 帧缓冲 | ~50KB（压缩） | 单帧 MJPG 数据暂存 |
| JPEG 解码输出 | 640×480×2 = 614KB | YUYV 或 RGB565 格式 |

> **结论**：640x480 MJPG 最低需要约 **700KB RAM**，YUYV 则需约 **1.3MB**。
> 必须使用外部 PSRAM 或高内存单片机（STM32H7 / i.MX RT）。

---

## 七、调试方法与工具

### 7.1 在 Linux 上抓包分析 USB 通信

```bash
# 加载 USB 监控模块
sudo modprobe usbmon

# 用 Wireshark 抓包（选择 usbmon0 或具体总线 usbmon1）
sudo wireshark

# 或用 tshark 命令行
sudo tshark -i usbmon1 -Y "usb.device_address == 75" -w camera.pcap
# 75 是 lsusb 中看到的 Device 编号

# 抓包期间执行采集触发
v4l2-ctl --device=/dev/video0 --stream-mmap --stream-count=5 --stream-to=/dev/null
```

**在 Wireshark 中重点观察：**
- `URB_CONTROL out` → 找 Probe/Commit SET_CUR 请求
- `URB_ISOCHRONOUS in` → 视频数据包，观察 Payload Header

### 7.2 用 Python 在 PC 上验证 UVC 逻辑

```python
import usb.core
import usb.util
import struct

# 找到摄像头
dev = usb.core.find(idVendor=0x1bcf, idProduct=0x0b15)
dev.set_configuration()

# GET_CUR Probe Control（wValue = CS<<8）
# wIndex = EntityID<<8 | InterfaceNum，需从描述符解析
data = dev.ctrl_transfer(
    0xA1,    # bmRequestType
    0x81,    # bRequest: GET_CUR
    0x0100,  # wValue: VS_PROBE_CONTROL CS=0x01
    0x0001,  # wIndex: Interface 1
    26       # wLength
)
print("Probe data:", data.tolist())
```

### 7.3 逻辑分析仪 / USB 协议分析仪

如果在单片机上遇到问题：
- **软件方式**：PC 用 Wireshark + usbmon 先抓正确流量作为参考
- **硬件方式**：Total Phase Beagle USB 480（贵）或 OpenVizsla（开源）
- **替代方案**：用 STM32 的 USB OTG 做 sniffer 模式（开源项目 USBProxy）

---

## 八、注意事项（坑点汇总）

### 8.1 USB 速度问题（最重要）

> 这是最容易踩的坑。

- 这台摄像头工作在 **USB 2.0 High Speed（480Mbps）**
- 必须使用支持 **High Speed** 的 USB Host 控制器
- STM32F4 的 USB OTG **FS** 仅有 12Mbps，**无法**满足 640x480@30fps
- 必须使用 STM32H7 / STM32F7 的 OTG **HS**（需外部 PHY 如 USB3300）或集成 HS PHY 的芯片

### 8.2 Isochronous 端点的特殊性

- Isochronous 传输**没有重传**机制，数据丢失不会重发
- 必须严格按帧间隔提交 URB，否则会漏帧
- 每个 microframe（125µs）允许的最大传输量有限制，需按描述符中的 `wMaxPacketSize` 分配

### 8.3 描述符解析的复杂性

UVC 描述符非常复杂，有大量厂商自定义部分：
- 解析前必须完整读取并 dump 描述符（`lsusb -v`）
- `bFormatIndex` 和 `bFrameIndex` 是从 1 开始的，**不是从 0**
- Extension Unit（XU）描述符含厂商私有控制，通常可以忽略

### 8.4 Alternate Setting 切换时机

- 必须在 Commit 之后，开始传输之前切换
- 不切换到非零 Alternate Setting，Isochronous 端点不会分配带宽，没有数据

### 8.5 帧边界识别

- 使用 Payload Header 中的 **FID（Frame ID）** 位判断帧边界
- FID 在每新帧开始时翻转（0→1 或 1→0）
- 不要依赖 EOF 单独判断，因为某些摄像头实现不规范

### 8.6 格式索引确认

> `bFormatIndex=1` 不一定是 MJPG！必须从描述符确认。

不同摄像头格式顺序不同，必须解析描述符中的 `VS_FORMAT_MJPEG` / `VS_FORMAT_UNCOMPRESSED` 的 `bFormatIndex` 字段。

### 8.7 Power/电源

USB 摄像头需要的电流：
- 通常在 **200~500mA @5V**
- 单片机 USB Host 端口需能提供足够电流
- 建议加 500mA 保险丝和电源滤波

---

## 九、推荐学习资源

| 资源 | 说明 |
|------|------|
| [UVC 1.5 规范](https://www.usb.org/document-library/video-class-v15-document-set) | USB IF 官方规范，必读 |
| Linux `drivers/media/usb/uvc/` 源码 | 最完整的 UVC 参考实现 |
| TinyUSB `examples/host/hid_controller` | 了解 Host 驱动写法 |
| CherryUSB UVC Host | 中文项目，有 UVC Host 实现 |
| `lsusb -v` 输出 | 你自己摄像头的真实描述符 |
| Wireshark USB 抓包 | 调试神器，先在 PC 上抓正常流量 |

---

## 十、快速参考卡（单片机开发备忘）

```
设备信息：
  VID=0x1bcf, PID=0x0b15
  USB Speed: High Speed (480Mbps)

Isochronous IN Endpoint：
  地址：0x87（需从描述符确认）

推荐起步配置：
  格式：MJPG（bFormatIndex=1，需确认）
  分辨率：640x480（bFrameIndex=4，需确认）
  帧率：30fps（dwFrameInterval=333333）

控制接口：EP0，Interface 0
流数据接口：ISO IN EP，Interface 1，需切换 Alternate Setting

关键 UVC 请求：
  GET_CUR = 0x81, SET_CUR = 0x01
  VS_PROBE_CONTROL CS = 0x01
  VS_COMMIT_CONTROL CS = 0x02

Payload Header：
  buf[0] = HLE（通常=2）
  buf[1] bit0 = FID（帧边界检测）
  buf[1] bit1 = EOF（帧结束）
  视频数据从 buf[HLE] 开始
```

---

*文档基于实际设备探测生成。描述符索引（bFormatIndex/bFrameIndex）需通过 `lsusb -d 1bcf:0b15 -v` 确认实际值。*
