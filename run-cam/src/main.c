/*
 * main.c - USB摄像头UVC探测工具 主程序
 *
 * 交互式命令行界面，使用 readline 库提供命令补全和历史记录功能。
 *
 * === 使用说明 ===
 *
 * 启动后输入编号执行对应功能，输入 help 查看帮助。
 * 支持的操作:
 *   1  - 显示USB描述符（设备/配置/接口/端点/UVC类特定描述符）
 *   2  - 枚举V4L2格式/分辨率/帧率/控制项
 *   3  - 显示可选的格式+分辨率列表
 *   4  - 修改控制项的值（亮度、对比度等）
 *   5  - 探测所有扩展单元(XU)的私有控制
 *   6  - 读取XU控制值
 *   7  - 写入XU控制值
 *   8  - 启动ffplay预览
 *   9  - 停止ffplay预览
 *   10 - 重启ffplay预览（修改参数后）
 *   0/quit/exit - 退出
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <linux/videodev2.h>

#include "logger.h"
#include "usb_desc.h"
#include "v4l2_cap.h"
#include "xu_ctrl.h"
#include "ffplay_ctrl.h"
#include "esp32_compat.h"


#define USB_VID  0x0bda /* Sunplus Innovation Technology Inc. (回退默认) */
#define USB_PID  0x5846 /* SPCA2650 PC Camera (回退默认) */
#define LOG_FILE "cam.log"

/* 运行时目标摄像头 (启动时自动检测填充, 命令11可改选/手动指定) */
static uint16_t g_target_vid = USB_VID;
static uint16_t g_target_pid = USB_PID;

/*
 * 当前选中的 V4L2 捕获节点路径。
 *
 * 现代 uvcvideo (kernel >= 4.16) 会为同一摄像头创建多个 /dev/videoN：
 *   - 一个 VIDEO_CAPTURE 节点 (真正的视频流)
 *   - 一个 META_CAPTURE 节点 (UVC payload header 元数据)
 * 所以不能再硬编码 /dev/videoX，启动时由 find_capture_device() 自动定位。
 * 默认值仅作为 fallback，find 失败时使用。
 */
static char g_dev_path[64] = "/dev/video0";

/* 全局状态 */
static usb_desc_info_t g_desc_info;     /* USB描述符中解析出的XU信息 */
static v4l2_cap_info_t g_cap_info;      /* V4L2枚举结果 */
static ffplay_state_t  g_ffplay;        /* ffplay进程状态 */
static int             g_desc_done = 0; /* 是否已执行USB描述符dump */
static int             g_cap_done  = 0; /* 是否已执行V4L2枚举 */

/* readline 命令列表（用于Tab补全） */
static const char *commands[] = {"1",  "2",  "3",    "4",    "5",    "6", "7",
                                 "8",  "9",  "10",   "11",   "help", "quit", "exit", "0", NULL};

/* readline 自定义补全生成器 */
static char *command_generator(const char *text, int state)
{
    static int idx;
    if (!state)
        idx = 0;

    while (commands[idx]) {
        const char *cmd = commands[idx++];
        if (strncmp(cmd, text, strlen(text)) == 0) {
            return strdup(cmd);
        }
    }
    return NULL;
}

static char **command_completion(const char *text, int start, int end)
{
    (void)end;
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    return NULL;
}

/*
 * 自动找到 VID/PID 匹配且支持 VIDEO_CAPTURE 的 V4L2 节点。
 *
 * 算法:
 *   1. 遍历 /dev/video0..63
 *   2. VIDIOC_QUERYCAP 取 device_caps (无 DEVICE_CAPS 时退回 capabilities),
 *      要求含 V4L2_CAP_VIDEO_CAPTURE — 这步会过滤掉 META_CAPTURE 元数据节点
 *   3. 通过 sysfs (/sys/class/video4linux/videoN/device/..) 取该节点
 *      所属 USB device 的 idVendor / idProduct, 匹配 VID/PID
 * 命中后路径写入 out_path, 返回 0; 找不到返回 -1。
 */
static int find_capture_device(uint16_t vid, uint16_t pid, char *out_path, size_t out_size)
{
    for (int i = 0; i < 64; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/video%d", i);

        int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0)
            continue;

        struct v4l2_capability cap;
        int                    qc = ioctl(fd, VIDIOC_QUERYCAP, &cap);
        close(fd);
        if (qc < 0)
            continue;

        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE))
            continue;

        char         sysp[160];
        unsigned int v = 0, p = 0;
        FILE        *fp;

        snprintf(sysp, sizeof(sysp), "/sys/class/video4linux/video%d/device/../idVendor", i);
        fp = fopen(sysp, "r");
        if (!fp)
            continue;
        if (fscanf(fp, "%x", &v) != 1) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        snprintf(sysp, sizeof(sysp), "/sys/class/video4linux/video%d/device/../idProduct", i);
        fp = fopen(sysp, "r");
        if (!fp)
            continue;
        if (fscanf(fp, "%x", &p) != 1) {
            fclose(fp);
            continue;
        }
        fclose(fp);

        LOG_D("扫描 %s: device_caps 含 VIDEO_CAPTURE, USB %04x:%04x", path, v, p);

        if (v == vid && p == pid) {
            snprintf(out_path, out_size, "%s", path);
            return 0;
        }
    }
    return -1;
}

/* 打印菜单 */
static void print_menu(void)
{
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║            USB摄像头 UVC 探测工具                           ║\n");
    printf("║            设备: %-40s  ║\n", g_dev_path);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  [信息查看]                                                 ║\n");
    printf("║    1  - 显示USB描述符 (设备/接口/UVC类描述符)               ║\n");
    printf("║    2  - 枚举V4L2能力 (格式/分辨率/帧率/控制项)             ║\n");
    printf("║    3  - 列出可选格式 (供预览选择)                           ║\n");
    printf("║                                                             ║\n");
    printf("║  [控制调整]                                                 ║\n");
    printf("║    4  - 修改控制项 (亮度/对比度/饱和度等)                   ║\n");
    printf("║                                                             ║\n");
    printf("║  [扩展单元(XU) - 厂商私有功能]                              ║\n");
    printf("║    5  - 探测所有XU控制 (自动扫描所有私有控制)               ║\n");
    printf("║    6  - 读取指定XU控制值                                    ║\n");
    printf("║    7  - 写入指定XU控制值                                    ║\n");
    printf("║                                                             ║\n");
    printf("║  [视频预览]                                                 ║\n");
    printf("║    8  - 启动ffplay预览                                      ║\n");
    printf("║    9  - 停止ffplay预览                                      ║\n");
    printf("║   10  - 重启ffplay预览                                      ║\n");
    printf("║                                                             ║\n");
    printf("║  [ESP32 适配]                                               ║\n");
    printf("║   11  - 判断该摄像头能否用于鱼缸 ESP32                      ║\n");
    printf("║                                                             ║\n");
    printf("║    0/quit - 退出                                            ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    /* 显示ffplay状态 */
    if (ffplay_is_running(&g_ffplay)) {
        printf("  [ffplay 运行中 PID=%d | %ux%u]\n", g_ffplay.pid, g_ffplay.width, g_ffplay.height);
    }
    printf("\n");
}

/* ===== 功能1: USB描述符 ===== */
static void cmd_usb_descriptors(void)
{
    LOG_I("开始读取USB描述符 (VID=0x%04x PID=0x%04x)...", g_target_vid, g_target_pid);
    if (usb_desc_dump(g_target_vid, g_target_pid, &g_desc_info) == 0) {
        g_desc_done = 1;
        printf("\n  USB描述符读取完成。发现 %d 个扩展单元(XU)。\n", g_desc_info.xu_count);
    } else {
        printf("\n  USB描述符读取失败！请检查设备是否连接、是否有权限。\n");
        printf("  提示: 可能需要 sudo 运行，或将用户加入 plugdev 组。\n");
    }
}

/* ===== 功能2: V4L2枚举 ===== */
static void cmd_v4l2_enumerate(void)
{
    LOG_I("开始V4L2枚举...");
    if (v4l2_enumerate_all(g_dev_path, &g_cap_info) == 0) {
        g_cap_done = 1;
        printf("\n  V4L2枚举完成。发现 %d 种格式, %d 个控制项。\n", g_cap_info.format_count, g_cap_info.ctrl_count);
    } else {
        printf("\n  V4L2枚举失败！\n");
    }
}

/* ===== 功能3: 列出格式 ===== */
static void cmd_list_formats(void)
{
    if (!g_cap_done) {
        printf("  请先执行 2 (V4L2枚举) 获取格式信息。\n");
        return;
    }
    v4l2_print_formats(&g_cap_info);
}

/* ===== 功能4: 修改控制项 ===== */
static void cmd_modify_control(void)
{
    if (!g_cap_done) {
        printf("  请先执行 2 (V4L2枚举) 获取控制项信息。\n");
        return;
    }

    v4l2_print_controls(&g_cap_info);

    char *input = readline("\n  输入控制项编号 (或 0 返回): ");
    if (!input)
        return;

    int idx = atoi(input);
    free(input);

    if (idx <= 0 || idx > g_cap_info.ctrl_count) {
        printf("  无效编号。\n");
        return;
    }

    const ctrl_info_t *ci = &g_cap_info.ctrls[idx - 1];
    printf("\n  选择的控制项: %s\n", ci->name);
    printf("  当前值=%d, 范围=[%d ~ %d], 步长=%d, 默认=%d\n", ci->current_value, ci->minimum, ci->maximum, ci->step,
           ci->default_value);

    char prompt[128];
    snprintf(prompt, sizeof(prompt), "  输入新值 [%d ~ %d]: ", ci->minimum, ci->maximum);
    input = readline(prompt);
    if (!input)
        return;

    int32_t new_val = atoi(input);
    free(input);

    if (new_val < ci->minimum || new_val > ci->maximum) {
        printf("  值超出范围！\n");
        return;
    }

    if (v4l2_set_control(g_dev_path, ci->id, new_val) == 0) {
        printf("  设置成功！\n");

        /* 更新本地缓存 */
        /* 安全地更新 - 因为ci是const指针，需要通过索引修改 */
        g_cap_info.ctrls[idx - 1].current_value = new_val;

        /* 提示是否需要重启ffplay */
        if (ffplay_is_running(&g_ffplay)) {
            printf("  注: 大部分控制项修改会实时生效, 无需重启ffplay。\n");
            printf("      如果没有变化, 输入 10 重启预览。\n");
        }
    }
}

/* ===== 功能5: 探测XU ===== */
static void cmd_xu_probe(void)
{
    if (!g_desc_done) {
        printf("  请先执行 1 (USB描述符) 获取XU信息。\n");
        return;
    }
    if (g_desc_info.xu_count == 0) {
        printf("  该设备没有扩展单元(XU)。\n");
        return;
    }
    xu_probe_all(g_dev_path, &g_desc_info);
    printf("\n  XU探测完成。请查看上方日志了解每个控制的详细信息。\n");
}

/* ===== 功能6: 读取XU值 ===== */
static void cmd_xu_read(void)
{
    if (!g_desc_done) {
        printf("  请先执行 1 (USB描述符) 获取XU信息。\n");
        return;
    }

    /* 列出可用的XU */
    printf("\n  可用的扩展单元:\n");
    for (int i = 0; i < g_desc_info.xu_count; i++) {
        char guid_str[64];
        usb_desc_guid_to_str(g_desc_info.xus[i].guid, guid_str, sizeof(guid_str));
        printf("    [%d] Unit ID=%u, GUID=%s, %u个控制\n", i + 1, g_desc_info.xus[i].unit_id, guid_str,
               g_desc_info.xus[i].num_controls);
    }

    char *input = readline("\n  选择XU编号 (或 0 返回): ");
    if (!input)
        return;
    int xu_idx = atoi(input);
    free(input);

    if (xu_idx <= 0 || xu_idx > g_desc_info.xu_count) {
        printf("  无效编号。\n");
        return;
    }
    const xu_info_t *xu = &g_desc_info.xus[xu_idx - 1];

    input = readline("  输入selector编号 (从1开始): ");
    if (!input)
        return;
    int selector = atoi(input);
    free(input);

    if (selector < 1) {
        printf("  无效selector。\n");
        return;
    }

    /* 先获取长度 */
    uint8_t len_buf[2] = {0};
    if (xu_get_value(g_dev_path, xu->unit_id, selector, len_buf, 2, UVC_GET_LEN) < 0) {
        printf("  获取数据长度失败。\n");
        return;
    }
    uint16_t data_len = len_buf[0] | (len_buf[1] << 8);
    printf("  数据长度: %u 字节\n", data_len);

    if (data_len == 0 || data_len > 4096) {
        printf("  数据长度异常。\n");
        return;
    }

    uint8_t *data = calloc(1, data_len);
    if (!data)
        return;

    printf("  选择查询类型:\n");
    printf("    1 - GET_CUR (当前值)\n");
    printf("    2 - GET_MIN (最小值)\n");
    printf("    3 - GET_MAX (最大值)\n");
    printf("    4 - GET_DEF (默认值)\n");
    printf("    5 - GET_RES (步长)\n");

    input = readline("  选择: ");
    if (!input) {
        free(data);
        return;
    }
    int qtype = atoi(input);
    free(input);

    uint8_t query;
    switch (qtype) {
    case 1:
        query = UVC_GET_CUR;
        break;
    case 2:
        query = UVC_GET_MIN;
        break;
    case 3:
        query = UVC_GET_MAX;
        break;
    case 4:
        query = UVC_GET_DEF;
        break;
    case 5:
        query = UVC_GET_RES;
        break;
    default:
        printf("  无效选择。\n");
        free(data);
        return;
    }

    if (xu_get_value(g_dev_path, xu->unit_id, selector, data, data_len, query) == 0) {
        printf("  读取成功:\n  ");
        for (int i = 0; i < data_len; i++) {
            printf("%02x ", data[i]);
            if ((i + 1) % 16 == 0)
                printf("\n  ");
        }
        printf("\n");

        /* 如果数据较短，也显示数值 */
        if (data_len <= 8) {
            uint64_t val = 0;
            for (int i = 0; i < data_len; i++)
                val |= (uint64_t)data[i] << (8 * i);
            printf("  数值: %lu (0x%lx)\n", (unsigned long)val, (unsigned long)val);
        }
    }

    free(data);
}

/* ===== 功能7: 写入XU值 ===== */
static void cmd_xu_write(void)
{
    if (!g_desc_done) {
        printf("  请先执行 1 (USB描述符) 获取XU信息。\n");
        return;
    }

    /* 列出可用的XU */
    printf("\n  可用的扩展单元:\n");
    for (int i = 0; i < g_desc_info.xu_count; i++) {
        char guid_str[64];
        usb_desc_guid_to_str(g_desc_info.xus[i].guid, guid_str, sizeof(guid_str));
        printf("    [%d] Unit ID=%u, GUID=%s\n", i + 1, g_desc_info.xus[i].unit_id, guid_str);
    }

    char *input = readline("\n  选择XU编号 (或 0 返回): ");
    if (!input)
        return;
    int xu_idx = atoi(input);
    free(input);

    if (xu_idx <= 0 || xu_idx > g_desc_info.xu_count) {
        printf("  无效编号。\n");
        return;
    }
    const xu_info_t *xu = &g_desc_info.xus[xu_idx - 1];

    input = readline("  输入selector编号 (从1开始): ");
    if (!input)
        return;
    int selector = atoi(input);
    free(input);

    if (selector < 1) {
        printf("  无效selector。\n");
        return;
    }

    /* 获取长度 */
    uint8_t len_buf[2] = {0};
    if (xu_get_value(g_dev_path, xu->unit_id, selector, len_buf, 2, UVC_GET_LEN) < 0) {
        printf("  获取数据长度失败。\n");
        return;
    }
    uint16_t data_len = len_buf[0] | (len_buf[1] << 8);
    printf("  数据长度: %u 字节\n", data_len);

    if (data_len == 0 || data_len > 256) {
        printf("  数据长度异常或太大。\n");
        return;
    }

    /* 先显示当前值 */
    uint8_t *cur = calloc(1, data_len);
    if (cur && xu_get_value(g_dev_path, xu->unit_id, selector, cur, data_len, UVC_GET_CUR) == 0) {
        printf("  当前值: ");
        for (int i = 0; i < data_len; i++)
            printf("%02x ", cur[i]);
        printf("\n");
    }
    free(cur);

    printf("  输入新值 (十六进制, 空格分隔, 最多%u字节):\n", data_len);
    printf("  例如: 01 00 ff\n");

    input = readline("  > ");
    if (!input)
        return;

    /* 解析16进制输入 */
    uint8_t *data = calloc(1, data_len);
    if (!data) {
        free(input);
        return;
    }

    int   parsed = 0;
    char *tok    = strtok(input, " \t");
    while (tok && parsed < data_len) {
        unsigned int val;
        if (sscanf(tok, "%x", &val) == 1 && val <= 0xFF) {
            data[parsed++] = (uint8_t)val;
        }
        tok = strtok(NULL, " \t");
    }
    free(input);

    if (parsed == 0) {
        printf("  没有有效数据。\n");
        free(data);
        return;
    }

    printf("  将写入 %d 字节: ", parsed);
    for (int i = 0; i < parsed; i++)
        printf("%02x ", data[i]);
    printf("\n");

    input = readline("  确认写入? (y/N): ");
    if (!input || (input[0] != 'y' && input[0] != 'Y')) {
        printf("  取消。\n");
        free(input);
        free(data);
        return;
    }
    free(input);

    /* 填充到完整长度 */
    if (xu_set_value(g_dev_path, xu->unit_id, selector, data, data_len) == 0) {
        printf("  写入成功！\n");
    } else {
        printf("  写入失败。\n");
    }

    free(data);
}

/* ===== 功能8: 启动ffplay ===== */
static void cmd_ffplay_start(void)
{
    if (!g_cap_done) {
        printf("  请先执行 2 (V4L2枚举) 获取格式信息。\n");
        return;
    }

    v4l2_print_formats(&g_cap_info);

    char *input = readline("\n  选择格式编号: ");
    if (!input)
        return;
    int choice = atoi(input);
    free(input);

    /* 根据选择找到对应的格式+分辨率 */
    int idx = 1;
    for (int i = 0; i < g_cap_info.format_count; i++) {
        const format_info_t *fi = &g_cap_info.formats[i];
        for (int j = 0; j < fi->frame_count; j++) {
            const frame_size_t *fs = &fi->frames[j];
            for (int k = 0; k < fs->interval_count; k++) {
                if (idx == choice) {
                    printf("  选择: %s %ux%u @ %.0f fps\n", fi->description, fs->width, fs->height,
                           (double)fs->intervals[k].denominator / fs->intervals[k].numerator);
                    ffplay_start(&g_ffplay, g_dev_path, fi->pixelformat, fs->width, fs->height);
                    return;
                }
                idx++;
            }
        }
    }

    printf("  无效编号。\n");
}

/* ===== 功能11: ESP32 兼容性判定 ===== */
static void cmd_esp32_compat(void)
{
    /* 选目标: 自动检测; 多个或想换则让用户选/手动输入 */
    usb_cam_cand_t cands[8];
    int            nc = usb_find_cameras(cands, 8);
    printf("\n  检测到 %d 个 UVC 摄像头:\n", nc);
    for (int i = 0; i < nc; i++)
        printf("    [%d] %04x:%04x \"%s\"\n", i + 1, cands[i].vid, cands[i].pid, cands[i].product);
    printf("    [m] 手动输入 VID:PID(评估非 UVC 设备也用这个)\n");

    char *in = readline("\n  选择编号或 m(直接回车=当前目标): ");
    if (in && in[0] == 'm') {
        free(in);
        in            = readline("  输入 VID:PID(十六进制, 如 2ce3:3828): ");
        unsigned v = 0, p = 0;
        if (in && sscanf(in, "%x:%x", &v, &p) == 2) {
            g_target_vid = (uint16_t)v;
            g_target_pid = (uint16_t)p;
        } else {
            printf("  格式错误。\n");
            free(in);
            return;
        }
    } else if (in && in[0] >= '1' && in[0] <= '9') {
        int idx = atoi(in);
        if (idx >= 1 && idx <= nc) {
            g_target_vid = cands[idx - 1].vid;
            g_target_pid = cands[idx - 1].pid;
        }
    }
    free(in);

    printf("  目标: %04x:%04x\n", g_target_vid, g_target_pid);

    /* 解析描述符(顺带捕获速度/端点/格式) */
    usb_desc_info_t info;
    if (usb_desc_dump(g_target_vid, g_target_pid, &info) != 0) {
        printf("\n  读取 USB 描述符失败(设备未连接或无权限, 可 sudo 重试)。\n");
        return;
    }

    /* 取产品名用于展示 */
    char           product[64] = "";
    usb_cam_cand_t cc[8];
    int            n2 = usb_find_cameras(cc, 8);
    for (int i = 0; i < n2; i++)
        if (cc[i].vid == g_target_vid && cc[i].pid == g_target_pid) {
            snprintf(product, sizeof(product), "%s", cc[i].product);
            break;
        }

    esp32_compat_report_t rep;
    esp32_compat_check(&info, &ESP32_DEFAULT_TARGET, &rep);
    esp32_compat_print_report(&rep, g_target_vid, g_target_pid, product);
}

/* ===== 信号处理 ===== */
static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n  收到退出信号...\n");
}

/* ===== 主函数 ===== */
int main(void)
{
    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGCHLD, SIG_IGN); /* 自动回收子进程 */

    /* 初始化日志 */
    if (logger_init(LOG_FILE, LOG_LEVEL_DEBUG) < 0) {
        fprintf(stderr, "日志初始化失败\n");
        return 1;
    }

    /* 自动检测连接的 UVC 摄像头作为默认目标 (替换写死的 VID/PID) */
    {
        usb_cam_cand_t cands[8];
        int            nc = usb_find_cameras(cands, 8);
        if (nc == 1) {
            g_target_vid = cands[0].vid;
            g_target_pid = cands[0].pid;
            LOG_I("自动检测到 UVC 摄像头: %04x:%04x \"%s\"", cands[0].vid, cands[0].pid, cands[0].product);
        } else if (nc > 1) {
            g_target_vid = cands[0].vid;
            g_target_pid = cands[0].pid;
            LOG_I("检测到 %d 个 UVC 摄像头, 默认用第一个 %04x:%04x (命令 11 可改选)", nc, cands[0].vid, cands[0].pid);
        } else {
            LOG_W("未自动检测到 UVC 摄像头, 回退默认 %04x:%04x (命令 11 可手动指定)", g_target_vid, g_target_pid);
        }
    }

    LOG_I("═══════════════════════════════════════════════════");
    LOG_I("  USB摄像头UVC探测工具 启动");
    LOG_I("  目标 USB: VID=0x%04x PID=0x%04x", g_target_vid, g_target_pid);
    LOG_I("  日志文件: %s", LOG_FILE);
    LOG_I("═══════════════════════════════════════════════════");

    /* 自动定位视频捕获节点 (避开 META_CAPTURE 元数据节点) */
    if (find_capture_device(g_target_vid, g_target_pid, g_dev_path, sizeof(g_dev_path)) == 0) {
        LOG_I("自动定位到 VIDEO_CAPTURE 节点: %s", g_dev_path);
    } else {
        LOG_W("未找到 VID=0x%04x PID=0x%04x 的捕获节点, 使用默认 %s", g_target_vid, g_target_pid, g_dev_path);
    }

    /* 初始化 ffplay 状态 */
    memset(&g_ffplay, 0, sizeof(g_ffplay));

    /* 配置 readline */
    rl_attempted_completion_function = command_completion;

    /* 初次显示菜单 */
    print_menu();

    /* 主循环 */
    while (g_running) {
        char *line = readline("uvc> ");
        if (!line)
            break; /* EOF (Ctrl+D) */

        /* 去除前后空白 */
        char *cmd = line;
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
        char *end = cmd + strlen(cmd) - 1;
        while (end > cmd && (*end == ' ' || *end == '\t' || *end == '\n'))
            *end-- = '\0';

        if (*cmd == '\0') {
            free(line);
            continue;
        }

        /* 添加到历史 */
        add_history(cmd);

        /* 解析命令 */
        if (strcmp(cmd, "0") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            free(line);
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
            print_menu();
        } else if (strcmp(cmd, "1") == 0) {
            cmd_usb_descriptors();
        } else if (strcmp(cmd, "2") == 0) {
            cmd_v4l2_enumerate();
        } else if (strcmp(cmd, "3") == 0) {
            cmd_list_formats();
        } else if (strcmp(cmd, "4") == 0) {
            cmd_modify_control();
        } else if (strcmp(cmd, "5") == 0) {
            cmd_xu_probe();
        } else if (strcmp(cmd, "6") == 0) {
            cmd_xu_read();
        } else if (strcmp(cmd, "7") == 0) {
            cmd_xu_write();
        } else if (strcmp(cmd, "8") == 0) {
            cmd_ffplay_start();
        } else if (strcmp(cmd, "9") == 0) {
            ffplay_stop(&g_ffplay);
        } else if (strcmp(cmd, "10") == 0) {
            ffplay_restart(&g_ffplay);
        } else if (strcmp(cmd, "11") == 0) {
            cmd_esp32_compat();
        } else {
            printf("  未知命令: %s (输入 help 查看菜单)\n", cmd);
        }

        free(line);
    }

    /* 清理 */
    if (ffplay_is_running(&g_ffplay)) {
        ffplay_stop(&g_ffplay);
    }

    LOG_I("程序退出");
    logger_close();

    printf("\n\n\n\n----------------------\n\n\nbye！\n");
    return 0;
}
