package handler

import (
	"cam-backend/internal/database"
	"cam-backend/internal/model"
	"log"
	"net/http"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
)

// SRS Webhook 回调的通用请求体
type SRSCallbackRequest struct {
	Action   string `json:"action"`
	ClientID string `json:"client_id"`
	IP       string `json:"ip"`
	Vhost    string `json:"vhost"`
	App      string `json:"app"`
	Stream   string `json:"stream"`
	Param    string `json:"param"`
	TCUrl    string `json:"tcUrl"`
	PageUrl  string `json:"pageUrl"`
	// on_dvr 特有
	CWD  string `json:"cwd"`
	File string `json:"file"`
	// on_hls 特有
	Duration float64 `json:"duration"`
	Seq      int     `json:"seq"`
	URL      string  `json:"url"`
}

// parseStreamParams 从 SRS param 中解析 token 和 mac
// param 格式类似: ?token=xxx&mac=AA:BB:CC:DD:EE:FF
func parseStreamParams(param string) (token, mac string) {
	param = strings.TrimPrefix(param, "?")
	for _, kv := range strings.Split(param, "&") {
		parts := strings.SplitN(kv, "=", 2)
		if len(parts) != 2 {
			continue
		}
		switch parts[0] {
		case "token":
			token = parts[1]
		case "mac":
			mac = parts[1]
		}
	}
	return
}

// streamKeyFromRequest 用 mac 做 stream key，如果没有 mac 就用 stream 名
func streamKeyFromRequest(req *SRSCallbackRequest) string {
	_, mac := parseStreamParams(req.Param)
	if mac != "" {
		return mac
	}
	return req.Stream
}

// OnConnect 客户端连接 - 有人连上 SRS 时触发
func OnConnect(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_connect] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	log.Printf("[SRS][on_connect] client_id=%s ip=%s app=%s param=%s",
		req.ClientID, req.IP, req.App, req.Param)

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnClose 客户端断开
func OnClose(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_close] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	log.Printf("[SRS][on_close] client_id=%s ip=%s app=%s",
		req.ClientID, req.IP, req.App)

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnPublish 客户端开始推流 - 核心回调
func OnPublish(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_publish] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	token, mac := parseStreamParams(req.Param)
	log.Printf("[SRS][on_publish] client_id=%s ip=%s app=%s stream=%s mac=%s token=%s",
		req.ClientID, req.IP, req.App, req.Stream, mac, token)

	// TODO: 鉴权逻辑 - 目前全部放过
	// if !validateToken(token) {
	//     c.JSON(http.StatusOK, gin.H{"code": 1}) // 返回非0拒绝推流
	//     return
	// }

	if mac != "" {
		// 更新设备状态为在线
		if err := database.SetDeviceOnline(mac, req.Stream, req.IP); err != nil {
			log.Printf("[SRS][on_publish] update device error: %v", err)
		}

		// 创建推流会话
		session := model.StreamSession{
			MAC:       mac,
			StreamKey: req.Stream,
			App:       req.App,
			ClientID:  req.ClientID,
			IP:        req.IP,
			StartedAt: time.Now(),
		}
		if err := database.DB.Create(&session).Error; err != nil {
			log.Printf("[SRS][on_publish] create session error: %v", err)
		}
	}

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnUnpublish 客户端停止推流
func OnUnpublish(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_unpublish] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	_, mac := parseStreamParams(req.Param)
	log.Printf("[SRS][on_unpublish] client_id=%s ip=%s app=%s stream=%s mac=%s",
		req.ClientID, req.IP, req.App, req.Stream, mac)

	if mac != "" {
		// 设备离线
		if err := database.SetDeviceOffline(mac); err != nil {
			log.Printf("[SRS][on_unpublish] update device error: %v", err)
		}

		// 结束推流会话
		now := time.Now()
		database.DB.Model(&model.StreamSession{}).
			Where("mac = ? AND stopped_at IS NULL", mac).
			Update("stopped_at", &now)
	}

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnPlay 有人开始播放
func OnPlay(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_play] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	log.Printf("[SRS][on_play] client_id=%s ip=%s app=%s stream=%s param=%s",
		req.ClientID, req.IP, req.App, req.Stream, req.Param)

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnStop 有人停止播放
func OnStop(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_stop] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	log.Printf("[SRS][on_stop] client_id=%s ip=%s app=%s stream=%s",
		req.ClientID, req.IP, req.App, req.Stream)

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnDvr DVR 录制完成回调
func OnDvr(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_dvr] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	log.Printf("[SRS][on_dvr] app=%s stream=%s file=%s cwd=%s",
		req.App, req.Stream, req.File, req.CWD)

	c.JSON(http.StatusOK, gin.H{"code": 0})
}

// OnHls HLS 切片生成回调
func OnHls(c *gin.Context) {
	var req SRSCallbackRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		log.Printf("[SRS][on_hls] parse error: %v", err)
		c.JSON(http.StatusOK, gin.H{"code": 0})
		return
	}

	_, mac := parseStreamParams(req.Param)
	log.Printf("[SRS][on_hls] app=%s stream=%s mac=%s duration=%.2f seq=%d url=%s",
		req.App, req.Stream, mac, req.Duration, req.Seq, req.URL)

	// 记录 HLS 文件信息
	if mac != "" {
		date := time.Now().Format("2006-01-02")
		record := model.RecordFile{
			MAC:      mac,
			Date:     date,
			FilePath: req.URL,
			Duration: req.Duration,
		}
		if err := database.DB.Create(&record).Error; err != nil {
			log.Printf("[SRS][on_hls] save record error: %v", err)
		}
	}

	c.JSON(http.StatusOK, gin.H{"code": 0})
}
