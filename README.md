# camera_usb_get

USB 摄像头视频采集与流媒体分发系统，包含底层 UVC 协议探测工具和后端流媒体服务。

## 项目结构

```
camera_usb_get/
├── run-cam/            # C 语言 UVC 协议探测工具（交互式 CLI）
├── cam-backend/        # Go 后端服务（RTMP 收流 → HLS 录制 + HTTP-FLV 直播）
└── USB摄像头技术文档.md  # 摄像头硬件与 UVC 协议参考文档
```

## run-cam — UVC 协议探测工具

交互式命令行工具，用 C + V4L2 + libusb 直接与 USB 摄像头交互：

- 逐字段解析完整 USB/UVC 描述符
- 枚举所有格式、分辨率、帧率、控制项
- 读写扩展单元（XU）厂商私有控制
- ffplay 实时预览

```bash
cd run-cam && make && ./build/uvc-tool
```

详见 [run-cam/README.md](run-cam/README.md)

## cam-backend — 流媒体后端服务

Go + Gin + SRS 6 构建的视频流管理后端：

- 接收客户端 RTMP 推流（H.264），通过 SRS 转 HLS 落盘
- HTTP-FLV 直播（flv.js 播放）
- HLS 录像回放（hls.js 播放）
- SRS Webhook 回调处理（设备上下线、会话记录）
- SQLite 存储设备状态和录制记录

### 端口

| 端口 | 服务 | 用途 |
|------|------|------|
| 1935 | SRS | RTMP 推流 |
| 1985 | SRS | HTTP API |
| 8080 | SRS | HTTP-FLV / HLS |
| 9090 | Go | REST API + Webhook |

### 快速启动

```bash
# 1. 启动 SRS
cd cam-backend/deps/srs && docker compose up -d

# 2. 启动 Go 服务
cd cam-backend && go run .

# 3. 推流测试
ffmpeg -re -i test.mp4 -c:v libx264 -f flv \
  "rtmp://localhost:1935/live/cam01?token=test&mac=AA:BB:CC:DD:EE:FF"
```

详见 [cam-backend/docs/](cam-backend/docs/)

## License

MIT
