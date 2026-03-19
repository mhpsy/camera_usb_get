/*
 * v4l2_cap.h - V4L2 能力枚举模块
 *
 * 使用Linux V4L2 (Video for Linux 2) API 查询摄像头的所有能力：
 * - 支持的格式 (MJPEG, YUYV 等)
 * - 支持的分辨率
 * - 支持的帧率
 * - 所有可调控制项 (亮度、对比度等)
 */

#ifndef V4L2_CAP_H
#define V4L2_CAP_H

#include <stdint.h>
#include <linux/videodev2.h>

/* 存储一个帧率 */
typedef struct {
    uint32_t numerator;
    uint32_t denominator;
} frame_interval_t;

/* 存储一个分辨率+帧率集合 */
typedef struct {
    uint32_t width;
    uint32_t height;
    int      interval_count;
    frame_interval_t intervals[16];
} frame_size_t;

/* 存储一个格式的完整信息 */
typedef struct {
    uint32_t     pixelformat;     /* V4L2 fourcc */
    char         description[32]; /* 可读名称 */
    int          frame_count;
    frame_size_t frames[32];
} format_info_t;

/* 存储一个控制项 */
typedef struct {
    uint32_t id;
    char     name[32];
    int32_t  type;       /* V4L2_CTRL_TYPE_xxx */
    int32_t  minimum;
    int32_t  maximum;
    int32_t  step;
    int32_t  default_value;
    int32_t  current_value;
    uint32_t flags;
} ctrl_info_t;

/* 完整的设备能力信息 */
typedef struct {
    char         driver[16];
    char         card[32];
    char         bus_info[32];
    uint32_t     capabilities;

    int          format_count;
    format_info_t formats[16];

    int          ctrl_count;
    ctrl_info_t  ctrls[64];
} v4l2_cap_info_t;

/*
 * 打开设备并枚举所有能力
 * dev_path: 设备路径，如 "/dev/video0"
 * info: 输出参数，存储所有枚举结果
 * 返回: 0成功，-1失败
 */
int v4l2_enumerate_all(const char *dev_path, v4l2_cap_info_t *info);

/*
 * 打印所有枚举到的格式信息（格式 → 分辨率 → 帧率 树状结构）
 */
void v4l2_print_formats(const v4l2_cap_info_t *info);

/*
 * 打印所有控制项（名称、范围、当前值）
 */
void v4l2_print_controls(const v4l2_cap_info_t *info);

/*
 * 设置控制项的值
 * dev_path: 设备路径
 * ctrl_id: 控制项ID (V4L2 CID)
 * value: 要设置的值
 * 返回: 0成功，-1失败
 */
int v4l2_set_control(const char *dev_path, uint32_t ctrl_id, int32_t value);

/*
 * 获取控制项的当前值
 */
int v4l2_get_control(const char *dev_path, uint32_t ctrl_id, int32_t *value);

#endif /* V4L2_CAP_H */
