# run-cam — USB 摄像头 UVC 协议探测工具

一个交互式命令行工具，用于深入探索 USB 摄像头的 UVC（USB Video Class）协议细节。通过 C 语言 + V4L2 + libusb 直接与设备交互，逐字段解析 USB 描述符，枚举所有能力，并支持控制扩展单元（XU）中的厂商私有功能。

## 功能

| 命令 | 说明 |
|------|------|
| `1` | 读取并解析完整的 USB 描述符（设备/配置/接口/端点/UVC 类特定描述符），每个字段附带中文含义说明 |
| `2` | 通过 V4L2 API 枚举所有格式、分辨率、帧率、控制项 |
| `3` | 列出可选的格式+分辨率组合（编号列表，供预览选择） |
| `4` | 交互式修改控制项（亮度、对比度、饱和度、曝光等） |
| `5` | 自动扫描所有扩展单元（XU）的全部控制，读取长度/能力/当前值/范围 |
| `6` | 手动读取指定 XU 控制的值 |
| `7` | 手动写入指定 XU 控制的值（如开灯、切换模式等私有功能） |
| `8` | 选择格式+分辨率后启动 ffplay 实时预览 |
| `9` | 停止 ffplay 预览 |
| `10` | 重启 ffplay 预览（修改格式/分辨率后需要重启） |

## 依赖

- GCC（支持 C11）
- libreadline-dev
- libusb-1.0-0-dev
- ffmpeg / ffplay
- Linux 内核 UVC 驱动（uvcvideo）

**Arch Linux / Manjaro:**

```bash
sudo pacman -S readline libusb ffmpeg
```

**Ubuntu / Debian:**

```bash
sudo apt install libreadline-dev libusb-1.0-0-dev ffmpeg
```

## 构建与运行

```bash
cd run-cam
make            # 编译，产物在 build/uvc-tool
./build/uvc-tool  # 运行（读取 USB 描述符可能需要 sudo）
```

清理构建产物：

```bash
make clean
```

## 项目结构

```
run-cam/
├── Makefile              # 构建脚本
├── src/
│   ├── main.c            # 主程序，readline 交互式 CLI
│   ├── logger.c / .h     # 日志模块（终端彩色输出 + cam.log 文件）
│   ├── usb_desc.c / .h   # USB 描述符解析（libusb，解析 UVC 类特定描述符）
│   ├── v4l2_cap.c / .h   # V4L2 能力枚举（格式/分辨率/帧率/控制项）
│   ├── xu_ctrl.c / .h    # 扩展单元（XU）控制（UVCIOC_CTRL_QUERY）
│   └── ffplay_ctrl.c / .h # ffplay 预览管理（fork+exec 启动/停止/重启）
└── build/                 # 构建产物（.gitignore 已排除）
```

## UVC 协议概览

UVC 设备内部的数据流拓扑：

```
[输入终端 IT] → [处理单元 PU] → [扩展单元 XU] → [输出终端 OT]
     ↑               ↑               ↑               ↑
  摄像头传感器    亮度/对比度等    厂商私有功能     USB 数据流
```

- **输入终端（IT）**：数据来源，通常是 CMOS 传感器，控制曝光、对焦等
- **处理单元（PU）**：图像处理，控制亮度、对比度、饱和度、锐度、白平衡等
- **扩展单元（XU）**：厂商私有功能，通过 GUID 标识，如 LED 控制、HDR、人脸检测等
- **输出终端（OT）**：数据去处，类型为 USB Streaming，将视频帧传输给主机

设备通过两种接口与主机通信：
- **视频控制接口（VC）**：发送控制命令（调参数）
- **视频流接口（VS）**：传输实际视频数据

## 日志

运行时所有操作的详细日志同时输出到终端（带 ANSI 颜色）和当前目录下的 `cam.log` 文件。

## License

MIT
