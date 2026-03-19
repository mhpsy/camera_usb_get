/*
 * logger.c - 日志模块实现
 *
 * 同时输出到终端（带ANSI颜色）和日志文件（纯文本）。
 * 每条日志包含: 时间戳 | 级别 | 源文件:行号 | 消息
 */

#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

/* ANSI 终端颜色码 */
#define COLOR_RESET   "\033[0m"
#define COLOR_DEBUG   "\033[36m"    /* 青色 - 调试 */
#define COLOR_INFO    "\033[32m"    /* 绿色 - 信息 */
#define COLOR_WARN    "\033[33m"    /* 黄色 - 警告 */
#define COLOR_ERROR   "\033[31m"    /* 红色 - 错误 */

static FILE *g_log_fp = NULL;           /* 日志文件句柄 */
static log_level_t g_min_level = LOG_LEVEL_DEBUG;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str[] = {
    "DEBUG", "INFO ", "WARN ", "ERROR"
};

static const char *level_color[] = {
    COLOR_DEBUG, COLOR_INFO, COLOR_WARN, COLOR_ERROR
};

int logger_init(const char *log_file_path, log_level_t min_level)
{
    g_min_level = min_level;

    if (log_file_path) {
        g_log_fp = fopen(log_file_path, "a");
        if (!g_log_fp) {
            fprintf(stderr, "[日志] 无法打开日志文件: %s\n", log_file_path);
            return -1;
        }
        /* 写入分隔线标记新会话 */
        fprintf(g_log_fp, "\n========== 新会话开始 ==========\n");
        fflush(g_log_fp);
    }

    return 0;
}

void logger_close(void)
{
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
}

void logger_set_level(log_level_t level)
{
    g_min_level = level;
}

void logger_log(log_level_t level, const char *file, int line,
                const char *fmt, ...)
{
    if (level < g_min_level)
        return;

    /* 获取精确到毫秒的时间戳 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);

    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_info);

    /* 提取文件名（去掉路径） */
    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    va_list ap;

    pthread_mutex_lock(&g_log_mutex);

    /* 输出到终端（带颜色） */
    fprintf(stderr, "%s[%s.%03ld] [%s] [%s:%d] ",
            level_color[level], time_buf, tv.tv_usec / 1000,
            level_str[level], basename, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", COLOR_RESET);

    /* 输出到日志文件（无颜色） */
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s.%03ld] [%s] [%s:%d] ",
                time_buf, tv.tv_usec / 1000,
                level_str[level], basename, line);
        va_start(ap, fmt);
        vfprintf(g_log_fp, fmt, ap);
        va_end(ap);
        fprintf(g_log_fp, "\n");
        fflush(g_log_fp);
    }

    pthread_mutex_unlock(&g_log_mutex);
}

void logger_hexdump(log_level_t level, const char *label,
                    const unsigned char *data, int len)
{
    if (level < g_min_level)
        return;

    LOG_D("--- %s (%d 字节) ---", label, len);

    char line[128];
    char ascii[17];
    int offset = 0;

    for (int i = 0; i < len; i++) {
        if (i % 16 == 0) {
            if (i > 0) {
                ascii[16] = '\0';
                /* 拼接可打印ASCII字符 */
                snprintf(line + offset, sizeof(line) - offset, "  |%s|", ascii);
                logger_log(level, __FILE__, __LINE__, "%s", line);
            }
            offset = snprintf(line, sizeof(line), "  %04x: ", i);
            memset(ascii, '.', 16);
        }
        offset += snprintf(line + offset, sizeof(line) - offset, "%02x ", data[i]);
        if (data[i] >= 0x20 && data[i] < 0x7f)
            ascii[i % 16] = data[i];
        else
            ascii[i % 16] = '.';
    }

    /* 处理最后不满16字节的行 */
    if (len > 0) {
        int remain = len % 16;
        if (remain == 0) remain = 16;
        ascii[remain] = '\0';
        /* 填充空格对齐 */
        for (int i = remain; i < 16; i++)
            offset += snprintf(line + offset, sizeof(line) - offset, "   ");
        snprintf(line + offset, sizeof(line) - offset, "  |%s|", ascii);
        logger_log(level, __FILE__, __LINE__, "%s", line);
    }
}
