/*
 * logger.h - 日志模块头文件
 *
 * 提供分级日志输出，同时写入终端（带颜色）和日志文件。
 * 日志级别: DEBUG < INFO < WARN < ERROR
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* 日志级别 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,  /* 调试信息，最详细 */
    LOG_LEVEL_INFO  = 1,  /* 一般信息 */
    LOG_LEVEL_WARN  = 2,  /* 警告，不影响运行 */
    LOG_LEVEL_ERROR = 3,  /* 错误，可能影响功能 */
} log_level_t;

/* 初始化日志系统，打开日志文件 */
int  logger_init(const char *log_file_path, log_level_t min_level);

/* 关闭日志系统 */
void logger_close(void);

/* 设置最低输出级别 */
void logger_set_level(log_level_t level);

/* 核心日志函数（通常通过宏调用） */
void logger_log(log_level_t level, const char *file, int line,
                const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* 便捷宏 */
#define LOG_D(fmt, ...) logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, "%s" fmt, "", ##__VA_ARGS__)
#define LOG_I(fmt, ...) logger_log(LOG_LEVEL_INFO,  __FILE__, __LINE__, "%s" fmt, "", ##__VA_ARGS__)
#define LOG_W(fmt, ...) logger_log(LOG_LEVEL_WARN,  __FILE__, __LINE__, "%s" fmt, "", ##__VA_ARGS__)
#define LOG_E(fmt, ...) logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, "%s" fmt, "", ##__VA_ARGS__)

/* 输出十六进制 dump，用于展示 USB 描述符原始数据 */
void logger_hexdump(log_level_t level, const char *label,
                    const unsigned char *data, int len);

#endif /* LOGGER_H */
