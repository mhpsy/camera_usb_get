# camera-usb-get

USB 摄像头视频采集与流媒体分发系统。两个独立但互补的子项目：

- **`run-cam/`** — C 语言写的交互式 UVC 协议探测工具。直接与 USB 摄像头硬件对话，解析描述符、枚举能力、读写扩展单元（XU）厂商私有控制。用来摸清一台陌生摄像头能做什么、能怎么调。
- **`cam-backend/`** — Go + SRS 6 的后端流媒体服务。接 RTMP 推流，吐 HTTP-FLV 直播 + HLS 录像回放，配 Webhook 设备管理。把摄像头接进 Web。

```
┌─────────────────┐         ┌────────────────┐         ┌─────────────────┐
│  USB 摄像头      │ ──UVC─→ │  run-cam       │         │   开发者         │
│ (UVC 设备)       │ ←──XU── │ (探测/调参)     │ ←──CLI─ │                 │
└─────────────────┘         └────────────────┘         └─────────────────┘
        │
        │  ffmpeg/RTMP push
        ↓
┌─────────────────┐  webhook ┌────────────────┐  HLS/FLV ┌─────────────────┐
│   SRS 6         │ ───────→ │  cam-backend   │ ───────→ │  Web 播放器      │
│ (RTMP/FLV/HLS)  │ ←─REST── │  (Go + Gin)    │          │ (flv.js/hls.js) │
└─────────────────┘          └────────────────┘          └─────────────────┘
```

## 目录结构

```
camera-usb-get/
├── run-cam/                # C UVC 探测工具
│   ├── src/                # main / logger / usb_desc / v4l2_cap / xu_ctrl / ffplay_ctrl
│   ├── Makefile
│   ├── .clang-format       # 项目级代码风格（4 空格 / 120 列 / Linux 大括号）
│   └── README.md
├── cam-backend/            # Go 流媒体后端
│   ├── main.go
│   ├── internal/           # database / handler / middleware / model
│   ├── deps/srs/           # SRS 6 docker compose
│   └── docs/               # 架构设计 / API / 前端接入
└── USB摄像头技术文档.md     # UVC 协议参考
```

## run-cam — UVC 协议探测工具

交互式 CLI，输入编号执行操作：

| 编号 | 功能 |
|------|------|
| 1 | 解析完整 USB/UVC 描述符（设备/配置/接口/端点/类特定） |
| 2 | V4L2 枚举所有格式、分辨率、帧率、控制项 |
| 3 | 列出可选的格式+分辨率组合 |
| 4 | 修改标准控制项（亮度/对比度/曝光等） |
| 5 | 自动扫描所有 XU 的全部控制 |
| 6/7 | 读/写指定 XU 控制 |
| 8/9/10 | ffplay 预览：启动 / 停止 / 重启 |

**依赖**（Arch）：

```bash
sudo pacman -S readline libusb ffmpeg
```

**构建运行**：

```bash
cd run-cam
make
sudo ./build/uvc-tool        # 读取 USB 描述符通常需要 root
```

详见 [run-cam/README.md](run-cam/README.md)。

## cam-backend — 流媒体后端

| 端口 | 服务 | 用途 |
|------|------|------|
| 1935 | SRS  | RTMP 推流入口 |
| 1985 | SRS  | HTTP API |
| 8080 | SRS  | HTTP-FLV / HLS 播放 |
| 9090 | Go   | REST API + SRS Webhook 回调 |

**快速启动**：

```bash
# 1. 启动 SRS
cd cam-backend/deps/srs && docker compose up -d

# 2. 启动 Go 服务
cd cam-backend && go run .

# 3. 推流测试
ffmpeg -re -i test.mp4 -c:v libx264 -f flv \
  "rtmp://localhost:1935/live/cam01?token=test&mac=AA:BB:CC:DD:EE:FF"
```

设计文档、REST API 详细签名、前端接入示例见 [cam-backend/docs/](cam-backend/docs/)。

## 开发约定

### C 代码风格

`run-cam/.clang-format` 是这个项目的代码风格权威：4 空格缩进、120 列、Linux 内核大括号风格（函数定义大括号换行，控制语句同行）、保留枚举/声明/宏的列对齐。任何编辑器只要支持 clang-format（nvim/clangd、VSCode、CLion 等）保存时格式化都会自动生效。

手动跑一遍：

```bash
cd run-cam
find src -type f \( -name '*.c' -o -name '*.h' \) -exec clang-format -i {} +
```

### Go 代码风格

直接 `gofmt` / `goimports`，无额外配置。

## License

MIT
