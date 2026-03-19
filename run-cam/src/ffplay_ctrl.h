/*
 * ffplay_ctrl.h - ffplay 预览控制模块
 *
 * 管理 ffplay 子进程，用于实时预览摄像头画面。
 * 支持启动、停止、重启（参数变更后需要重启）。
 */

#ifndef FFPLAY_CTRL_H
#define FFPLAY_CTRL_H

#include <stdint.h>
#include <sys/types.h>

/* ffplay 进程状态 */
typedef struct {
    pid_t    pid;              /* ffplay 进程ID，0表示未运行 */
    uint32_t pixelformat;      /* 当前使用的像素格式 (V4L2 fourcc) */
    uint32_t width;            /* 当前分辨率宽 */
    uint32_t height;           /* 当前分辨率高 */
    char     dev_path[64];     /* 设备路径 */
} ffplay_state_t;

/*
 * 启动 ffplay 预览
 * dev_path: 设备路径 (如 "/dev/video0")
 * pixelformat: V4L2 fourcc (如 V4L2_PIX_FMT_MJPEG)
 * width, height: 分辨率
 */
int ffplay_start(ffplay_state_t *state, const char *dev_path,
                 uint32_t pixelformat, uint32_t width, uint32_t height);

/*
 * 停止 ffplay 预览
 */
int ffplay_stop(ffplay_state_t *state);

/*
 * 重启 ffplay（停止后重新启动，使用相同参数）
 * 用于控制项修改后刷新预览
 */
int ffplay_restart(ffplay_state_t *state);

/*
 * 检查 ffplay 是否仍在运行
 */
int ffplay_is_running(const ffplay_state_t *state);

#endif /* FFPLAY_CTRL_H */
