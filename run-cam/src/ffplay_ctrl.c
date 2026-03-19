/*
 * ffplay_ctrl.c - ffplay 预览控制实现
 *
 * 通过 fork+exec 启动 ffplay 子进程。
 * ffplay 直接使用 v4l2 输入来读取摄像头，命令形如：
 *   ffplay -f v4l2 -input_format mjpeg -video_size 640x480 /dev/video0
 *
 * 注意事项：
 * - ffplay 使用 v4l2 设备时会独占设备（或与其他进程共享，取决于驱动）
 * - 修改控制项（亮度等）不需要重启ffplay，V4L2控制是独立的
 * - 但修改格式/分辨率需要重启，因为格式在流启动时已锁定
 */

#include "ffplay_ctrl.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <linux/videodev2.h>

/* 将 V4L2 fourcc 转为 ffplay 能识别的输入格式名 */
static const char *pixfmt_to_ffmpeg(uint32_t pixelformat)
{
    switch (pixelformat) {
    case V4L2_PIX_FMT_MJPEG:  return "mjpeg";
    case V4L2_PIX_FMT_YUYV:   return "yuyv422";
    case V4L2_PIX_FMT_NV12:   return "nv12";
    case V4L2_PIX_FMT_H264:   return "h264";
    default:                   return "mjpeg";
    }
}

int ffplay_start(ffplay_state_t *state, const char *dev_path,
                 uint32_t pixelformat, uint32_t width, uint32_t height)
{
    /* 如果已经在运行，先停止 */
    if (state->pid > 0) {
        ffplay_stop(state);
    }

    const char *fmt_name = pixfmt_to_ffmpeg(pixelformat);
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%ux%u", width, height);

    LOG_I("启动 ffplay 预览:");
    LOG_I("  设备: %s", dev_path);
    LOG_I("  格式: %s", fmt_name);
    LOG_I("  分辨率: %s", size_str);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_E("fork失败: %m");
        return -1;
    }

    if (pid == 0) {
        /* 子进程: 执行ffplay */

        /*
         * ffplay 命令参数说明:
         * -f v4l2             使用V4L2输入设备
         * -input_format xxx   指定像素格式
         * -video_size WxH     指定分辨率
         * -window_title xxx   设置窗口标题（方便识别）
         * -loglevel warning   减少ffplay自身的输出
         */
        execlp("ffplay", "ffplay",
               "-f", "v4l2",
               "-input_format", fmt_name,
               "-video_size", size_str,
               "-window_title", "UVC Camera Preview",
               "-loglevel", "warning",
               dev_path,
               NULL);

        /* 如果exec失败 */
        _exit(127);
    }

    /* 父进程 */
    state->pid = pid;
    state->pixelformat = pixelformat;
    state->width = width;
    state->height = height;
    strncpy(state->dev_path, dev_path, sizeof(state->dev_path) - 1);

    LOG_I("ffplay 已启动 (PID=%d)", pid);
    return 0;
}

int ffplay_stop(ffplay_state_t *state)
{
    if (state->pid <= 0) {
        LOG_W("ffplay 未在运行");
        return 0;
    }

    LOG_I("停止 ffplay (PID=%d)...", state->pid);

    /* 先发 SIGTERM 优雅退出 */
    kill(state->pid, SIGTERM);

    /* 等待退出，最多2秒 */
    int status;
    int waited = 0;
    for (int i = 0; i < 20; i++) {
        if (waitpid(state->pid, &status, WNOHANG) > 0) {
            waited = 1;
            break;
        }
        usleep(100000); /* 100ms */
    }

    /* 如果还没退出，强制杀死 */
    if (!waited) {
        LOG_W("ffplay 未响应SIGTERM, 发送SIGKILL");
        kill(state->pid, SIGKILL);
        waitpid(state->pid, &status, 0);
    }

    LOG_I("ffplay 已停止");
    state->pid = 0;
    return 0;
}

int ffplay_restart(ffplay_state_t *state)
{
    if (state->pid <= 0) {
        LOG_W("ffplay 未在运行, 无法重启");
        return -1;
    }

    uint32_t pf = state->pixelformat;
    uint32_t w = state->width;
    uint32_t h = state->height;
    char dev[64];
    strncpy(dev, state->dev_path, sizeof(dev) - 1);
    dev[sizeof(dev) - 1] = '\0';

    ffplay_stop(state);
    usleep(200000); /* 等200ms让设备释放 */
    return ffplay_start(state, dev, pf, w, h);
}

int ffplay_is_running(const ffplay_state_t *state)
{
    if (state->pid <= 0) return 0;

    /* 检查进程是否仍然存在 */
    if (kill(state->pid, 0) == 0) {
        return 1;
    }

    return 0;
}
