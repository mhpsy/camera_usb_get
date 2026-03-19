package database

import (
	"cam-backend/internal/model"
	"log"

	"gorm.io/driver/sqlite"
	"gorm.io/gorm"
)

var DB *gorm.DB

func Init(dbPath string) {
	var err error
	DB, err = gorm.Open(sqlite.Open(dbPath), &gorm.Config{})
	if err != nil {
		log.Fatalf("failed to connect database: %v", err)
	}

	// 自动迁移表结构
	if err := DB.AutoMigrate(
		&model.Device{},
		&model.StreamSession{},
		&model.RecordFile{},
	); err != nil {
		log.Fatalf("failed to migrate database: %v", err)
	}

	log.Println("[DB] SQLite initialized, path:", dbPath)
}

// GetOrCreateDevice 根据 MAC 获取或创建设备
func GetOrCreateDevice(mac string) (*model.Device, error) {
	var device model.Device
	result := DB.Where("mac = ?", mac).First(&device)
	if result.Error == gorm.ErrRecordNotFound {
		device = model.Device{MAC: mac, Name: mac}
		if err := DB.Create(&device).Error; err != nil {
			return nil, err
		}
	} else if result.Error != nil {
		return nil, result.Error
	}
	return &device, nil
}

// SetDeviceOnline 设置设备在线
func SetDeviceOnline(mac, streamKey, ip string) error {
	device, err := GetOrCreateDevice(mac)
	if err != nil {
		return err
	}
	return DB.Model(device).Updates(map[string]interface{}{
		"online":     true,
		"stream_key": streamKey,
		"ip":         ip,
	}).Error
}

// SetDeviceOffline 设置设备离线
func SetDeviceOffline(mac string) error {
	return DB.Model(&model.Device{}).Where("mac = ?", mac).Updates(map[string]interface{}{
		"online":     false,
		"stream_key": "",
	}).Error
}
