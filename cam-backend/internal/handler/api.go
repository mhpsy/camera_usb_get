package handler

import (
	"cam-backend/internal/database"
	"cam-backend/internal/model"
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"strings"

	"github.com/gin-gonic/gin"
)

// ---- 设备相关 API ----

// ListDevices 获取所有设备列表
func ListDevices(c *gin.Context) {
	var devices []model.Device
	if err := database.DB.Find(&devices).Error; err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "data": devices})
}

// GetDevice 获取单个设备信息
func GetDevice(c *gin.Context) {
	mac := c.Param("mac")
	var device model.Device
	if err := database.DB.Where("mac = ?", mac).First(&device).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "device not found"})
		return
	}
	c.JSON(http.StatusOK, gin.H{"code": 0, "data": device})
}

// ---- 直播相关 API ----

// GetLiveURL 获取设备的直播地址 (HTTP-FLV)
// 前端用 flv.js 播放此地址
func GetLiveURL(c *gin.Context) {
	mac := c.Param("mac")

	var device model.Device
	if err := database.DB.Where("mac = ?", mac).First(&device).Error; err != nil {
		c.JSON(http.StatusNotFound, gin.H{"error": "device not found"})
		return
	}

	if !device.Online {
		c.JSON(http.StatusOK, gin.H{
			"code":    1,
			"message": "device is offline",
		})
		return
	}

	// SRS HTTP-FLV 地址: http://host:8080/live/{stream}.flv
	flvURL := fmt.Sprintf("http://%s:8080/live/%s.flv", c.Request.Host, device.StreamKey)
	// 去掉 host 里可能带的端口
	if idx := strings.Index(c.Request.Host, ":"); idx != -1 {
		flvURL = fmt.Sprintf("http://%s:8080/live/%s.flv", c.Request.Host[:idx], device.StreamKey)
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"flv_url": flvURL,
			"mac":     mac,
			"online":  device.Online,
		},
	})
}

// ---- 回放相关 API ----

// ListRecordDates 获取设备的录制日期列表
func ListRecordDates(c *gin.Context) {
	mac := c.Param("mac")

	var dates []string
	database.DB.Model(&model.RecordFile{}).
		Where("mac = ?", mac).
		Distinct("date").
		Order("date DESC").
		Pluck("date", &dates)

	c.JSON(http.StatusOK, gin.H{"code": 0, "data": dates})
}

// ListRecordFiles 获取设备某天的录制文件列表
func ListRecordFiles(c *gin.Context) {
	mac := c.Param("mac")
	date := c.Param("date")

	var records []model.RecordFile
	database.DB.Where("mac = ? AND date = ?", mac, date).
		Order("created_at ASC").
		Find(&records)

	c.JSON(http.StatusOK, gin.H{"code": 0, "data": records})
}

// GetPlaybackURL 获取回放 m3u8 地址
// 回放通过 m3u8 文件访问对应的 ts 文件
func GetPlaybackURL(c *gin.Context) {
	mac := c.Param("mac")
	date := c.Param("date")

	// HLS 文件存储路径: ./data/hls/{mac}/{date}/
	hlsDir := filepath.Join("data", "hls", mac, date)
	if _, err := os.Stat(hlsDir); os.IsNotExist(err) {
		c.JSON(http.StatusNotFound, gin.H{"error": "no recordings for this date"})
		return
	}

	// 查找 m3u8 文件
	var m3u8Files []string
	entries, err := os.ReadDir(hlsDir)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}
	for _, entry := range entries {
		if strings.HasSuffix(entry.Name(), ".m3u8") {
			m3u8Files = append(m3u8Files, entry.Name())
		}
	}

	// 构建访问 URL
	var urls []gin.H
	for _, f := range m3u8Files {
		urls = append(urls, gin.H{
			"name": f,
			"url":  fmt.Sprintf("/hls/%s/%s/%s", mac, date, f),
		})
	}

	c.JSON(http.StatusOK, gin.H{
		"code": 0,
		"data": gin.H{
			"mac":   mac,
			"date":  date,
			"files": urls,
		},
	})
}

// ---- 推流会话 API ----

// ListSessions 获取设备的推流会话历史
func ListSessions(c *gin.Context) {
	mac := c.Param("mac")

	var sessions []model.StreamSession
	database.DB.Where("mac = ?", mac).
		Order("started_at DESC").
		Limit(50).
		Find(&sessions)

	c.JSON(http.StatusOK, gin.H{"code": 0, "data": sessions})
}
