# API 接口文档

Base URL: `http://{host}:9090`

所有 API 均在 `/api/v1` 路径下，经过 Auth 中间件（当前全放过）。

---

## 一、设备管理

### 1.1 获取所有设备列表

```
GET /api/v1/devices
```

**响应示例：**

```json
{
  "code": 0,
  "data": [
    {
      "id": 1,
      "mac": "AA:BB:CC:DD:EE:FF",
      "name": "AA:BB:CC:DD:EE:FF",
      "online": true,
      "stream_key": "camera01",
      "ip": "192.168.1.100",
      "created_at": "2026-03-19T10:00:00Z",
      "updated_at": "2026-03-19T10:05:00Z"
    }
  ]
}
```

### 1.2 获取单个设备信息

```
GET /api/v1/devices/:mac
```

**参数：**

| 参数 | 位置 | 说明 |
|------|------|------|
| mac | path | 设备 MAC 地址 |

**响应示例：**

```json
{
  "code": 0,
  "data": {
    "id": 1,
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "AA:BB:CC:DD:EE:FF",
    "online": true,
    "stream_key": "camera01",
    "ip": "192.168.1.100",
    "created_at": "2026-03-19T10:00:00Z",
    "updated_at": "2026-03-19T10:05:00Z"
  }
}
```

**错误响应（设备不存在）：**

```json
// HTTP 404
{ "error": "device not found" }
```

### 1.3 获取设备推流会话历史

```
GET /api/v1/devices/:mac/sessions
```

返回最近 50 条推流会话记录，按开始时间倒序。

**响应示例：**

```json
{
  "code": 0,
  "data": [
    {
      "id": 1,
      "mac": "AA:BB:CC:DD:EE:FF",
      "stream_key": "camera01",
      "app": "live",
      "client_id": "abc123",
      "ip": "192.168.1.100",
      "started_at": "2026-03-19T10:00:00Z",
      "stopped_at": "2026-03-19T11:30:00Z"
    }
  ]
}
```

---

## 二、直播

### 2.1 获取设备直播地址（HTTP-FLV）

```
GET /api/v1/live/:mac
```

前端拿到 `flv_url` 后用 flv.js 播放。

**参数：**

| 参数 | 位置 | 说明 |
|------|------|------|
| mac | path | 设备 MAC 地址 |

**响应示例（设备在线）：**

```json
{
  "code": 0,
  "data": {
    "flv_url": "http://192.168.1.1:8080/live/camera01.flv",
    "mac": "AA:BB:CC:DD:EE:FF",
    "online": true
  }
}
```

**响应示例（设备离线）：**

```json
{
  "code": 1,
  "message": "device is offline"
}
```

---

## 三、回放

### 3.1 获取设备录制日期列表

```
GET /api/v1/playback/:mac/dates
```

**响应示例：**

```json
{
  "code": 0,
  "data": ["2026-03-19", "2026-03-18", "2026-03-17"]
}
```

### 3.2 获取某天的录制文件列表

```
GET /api/v1/playback/:mac/:date/files
```

**参数：**

| 参数 | 位置 | 说明 |
|------|------|------|
| mac | path | 设备 MAC 地址 |
| date | path | 日期，格式 `YYYY-MM-DD` |

**响应示例：**

```json
{
  "code": 0,
  "data": [
    {
      "id": 1,
      "mac": "AA:BB:CC:DD:EE:FF",
      "date": "2026-03-19",
      "file_path": "/live/camera01-0.ts",
      "duration": 10.0,
      "created_at": "2026-03-19T10:00:00Z"
    }
  ]
}
```

### 3.3 获取回放 m3u8 地址

```
GET /api/v1/playback/:mac/:date/url
```

前端拿到 m3u8 URL 后用 hls.js 或原生 `<video>` 播放。

**响应示例：**

```json
{
  "code": 0,
  "data": {
    "mac": "AA:BB:CC:DD:EE:FF",
    "date": "2026-03-19",
    "files": [
      {
        "name": "camera01.m3u8",
        "url": "/hls/AA:BB:CC:DD:EE:FF/2026-03-19/camera01.m3u8"
      }
    ]
  }
}
```

m3u8 文件通过 Go 服务的静态文件路由 `/hls/` 提供访问，内部引用的 ts 文件也在同目录下。

**错误响应（无录像）：**

```json
// HTTP 404
{ "error": "no recordings for this date" }
```

---

## 四、SRS Webhook 回调

这些接口由 SRS 自动调用，**不需要前端/客户端主动请求**。列在这里便于调试和理解系统流程。

所有 Webhook 响应均为 `{"code": 0}` 表示放行。

| 接口 | 触发时机 | 处理逻辑 |
|------|----------|----------|
| `POST /api/v1/srs/on_connect` | 客户端连接 SRS | 打印日志 |
| `POST /api/v1/srs/on_close` | 客户端断开 SRS | 打印日志 |
| `POST /api/v1/srs/on_publish` | 开始推流 | **更新设备在线 + 创建会话** |
| `POST /api/v1/srs/on_unpublish` | 停止推流 | **更新设备离线 + 结束会话** |
| `POST /api/v1/srs/on_play` | 有人开始播放 | 打印日志 |
| `POST /api/v1/srs/on_stop` | 有人停止播放 | 打印日志 |
| `POST /api/v1/srs/on_dvr` | DVR 录制完成 | 打印日志 |
| `POST /api/v1/srs/on_hls` | HLS 切片生成 | **记录录制文件到数据库** |

**Webhook 请求体示例（on_publish）：**

```json
{
  "action": "on_publish",
  "client_id": "abc123",
  "ip": "192.168.1.100",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "camera01",
  "param": "?token=mytoken&mac=AA:BB:CC:DD:EE:FF",
  "tcUrl": "rtmp://192.168.1.1/live"
}
```

---

## 五、其他

### 健康检查

```
GET /health
```

```json
{ "status": "ok" }
```

### HLS 静态文件

```
GET /hls/{mac}/{date}/{filename}
```

由 Gin 静态文件中间件直接返回 m3u8 / ts 文件，不经过 API 逻辑。
