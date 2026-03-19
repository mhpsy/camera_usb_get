package main

import (
	"cam-backend/internal/database"
	"cam-backend/internal/handler"
	"cam-backend/internal/middleware"
	"log"
	"net/http"

	"github.com/gin-gonic/gin"
)

const (
	// 服务端口
	ServerPort = ":9090"
	// 数据库文件路径
	DBPath = "data/cam.db"
	// HLS 文件目录
	HLSDir = "data/hls"
)

// 端口说明:
// - 9090: 本 Go 服务 (API + Webhook)
// - 1935: SRS RTMP 接收推流
// - 1985: SRS HTTP API
// - 8080: SRS HTTP Server (HTTP-FLV + HLS 分发)

func main() {
	// 初始化数据库
	database.Init(DBPath)

	r := gin.Default()

	// ---- 静态文件服务: HLS 回放 ----
	r.Static("/hls", HLSDir)

	// ---- API 路由 ----
	api := r.Group("/api/v1")
	api.Use(middleware.Auth())
	{
		// SRS Webhook 回调 (SRS -> Go 服务)
		srs := api.Group("/srs")
		{
			srs.POST("/on_connect", handler.OnConnect)
			srs.POST("/on_close", handler.OnClose)
			srs.POST("/on_publish", handler.OnPublish)
			srs.POST("/on_unpublish", handler.OnUnpublish)
			srs.POST("/on_play", handler.OnPlay)
			srs.POST("/on_stop", handler.OnStop)
			srs.POST("/on_dvr", handler.OnDvr)
			srs.POST("/on_hls", handler.OnHls)
		}

		// 设备管理
		devices := api.Group("/devices")
		{
			devices.GET("", handler.ListDevices)
			devices.GET("/:mac", handler.GetDevice)
			devices.GET("/:mac/sessions", handler.ListSessions)
		}

		// 直播 - 获取 HTTP-FLV 地址 (前端用 flv.js 播放)
		api.GET("/live/:mac", handler.GetLiveURL)

		// 回放 - 通过 m3u8 文件回看 ts 录像
		playback := api.Group("/playback")
		{
			playback.GET("/:mac/dates", handler.ListRecordDates)
			playback.GET("/:mac/:date/files", handler.ListRecordFiles)
			playback.GET("/:mac/:date/url", handler.GetPlaybackURL)
		}
	}

	// 健康检查
	r.GET("/health", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})

	log.Printf("cam-backend starting on %s", ServerPort)
	log.Printf("SRS RTMP: rtmp://localhost:1935/live/{stream}?token=xxx&mac=AA:BB:CC:DD:EE:FF")
	log.Printf("SRS HTTP-FLV: http://localhost:8080/live/{stream}.flv")
	log.Printf("SRS HTTP API: http://localhost:1985")
	log.Printf("HLS playback: http://localhost:9090/hls/{mac}/{date}/{file}.m3u8")

	if err := r.Run(ServerPort); err != nil {
		log.Fatalf("server failed: %v", err)
	}
}
