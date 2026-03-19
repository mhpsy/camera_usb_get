package model

import "time"

// Device 设备表 - 以 MAC 地址为唯一标识
type Device struct {
	ID        uint      `gorm:"primarykey" json:"id"`
	MAC       string    `gorm:"uniqueIndex;size:64;not null" json:"mac"`
	Name      string    `gorm:"size:255" json:"name"`
	Online    bool      `gorm:"default:false" json:"online"`
	StreamKey string    `gorm:"size:255" json:"stream_key"` // 当前推流的 stream key
	IP        string    `gorm:"size:64" json:"ip"`
	CreatedAt time.Time `json:"created_at"`
	UpdatedAt time.Time `json:"updated_at"`
}

// StreamSession 推流会话记录
type StreamSession struct {
	ID        uint      `gorm:"primarykey" json:"id"`
	MAC       string    `gorm:"index;size:64;not null" json:"mac"`
	StreamKey string    `gorm:"size:255" json:"stream_key"`
	App       string    `gorm:"size:128" json:"app"`
	ClientID  string    `gorm:"size:128" json:"client_id"`
	IP        string    `gorm:"size:64" json:"ip"`
	StartedAt time.Time `json:"started_at"`
	StoppedAt *time.Time `json:"stopped_at"`
}

// RecordFile 录制文件记录
type RecordFile struct {
	ID        uint      `gorm:"primarykey" json:"id"`
	MAC       string    `gorm:"index;size:64;not null" json:"mac"`
	Date      string    `gorm:"index;size:16" json:"date"` // YYYY-MM-DD
	FilePath  string    `gorm:"size:512" json:"file_path"` // m3u8 文件路径
	Duration  float64   `json:"duration"`                   // 时长(秒)
	CreatedAt time.Time `json:"created_at"`
}
