/*
 * xu_ctrl.c - UVC 扩展单元控制实现
 *
 * 使用 Linux UVC 驱动提供的 UVCIOC_CTRL_QUERY ioctl 来访问XU控制。
 *
 * === UVCIOC_CTRL_QUERY 工作原理 ===
 *
 * struct uvc_xu_control_query {
 *     __u8  unit;      // XU的UnitID (从描述符获取)
 *     __u8  selector;  // 控制编号 (1-based)
 *     __u8  query;     // 操作类型 (UVC_GET_CUR, UVC_SET_CUR 等)
 *     __u16 size;      // 数据长度
 *     __u8 *data;      // 数据缓冲区指针
 * };
 *
 * 对于每个XU控制，典型的访问流程：
 *
 * 1. UVC_GET_LEN → 得到数据长度(2字节返回值)
 * 2. UVC_GET_INFO → 得到能力标志(1字节: bit0=可读, bit1=可写)
 * 3. UVC_GET_CUR → 读取当前值
 * 4. UVC_GET_MIN → 读取最小值
 * 5. UVC_GET_MAX → 读取最大值
 * 6. UVC_GET_DEF → 读取默认值
 * 7. UVC_SET_CUR → 写入新值
 */

#include "xu_ctrl.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/usb/video.h>
#include <linux/uvcvideo.h>

static int xu_query(int fd, uint8_t unit_id, uint8_t selector,
                    uint8_t query, uint8_t *data, uint16_t size)
{
    struct uvc_xu_control_query xctrl;
    memset(&xctrl, 0, sizeof(xctrl));
    xctrl.unit = unit_id;
    xctrl.selector = selector;
    xctrl.query = query;
    xctrl.size = size;
    xctrl.data = data;

    int ret;
    do {
        ret = ioctl(fd, UVCIOC_CTRL_QUERY, &xctrl);
    } while (ret == -1 && errno == EINTR);

    return ret;
}

static const char *query_name(uint8_t query)
{
    switch (query) {
    case UVC_SET_CUR:  return "SET_CUR(设置当前值)";
    case UVC_GET_CUR:  return "GET_CUR(获取当前值)";
    case UVC_GET_MIN:  return "GET_MIN(获取最小值)";
    case UVC_GET_MAX:  return "GET_MAX(获取最大值)";
    case UVC_GET_RES:  return "GET_RES(获取步长)";
    case UVC_GET_LEN:  return "GET_LEN(获取数据长度)";
    case UVC_GET_INFO: return "GET_INFO(获取能力)";
    case UVC_GET_DEF:  return "GET_DEF(获取默认值)";
    default:           return "UNKNOWN";
    }
}

int xu_probe_controls(const char *dev_path, const xu_info_t *xu)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_E("打开设备失败: %s: %s", dev_path, strerror(errno));
        return -1;
    }

    char guid_str[64];
    usb_desc_guid_to_str(xu->guid, guid_str, sizeof(guid_str));

    LOG_I("");
    LOG_I("┌──────────────────────────────────────────────────────────┐");
    LOG_I("│ 探测扩展单元 XU (unit_id=%u)", xu->unit_id);
    LOG_I("│ GUID: %s", guid_str);
    LOG_I("│ 声明的控制数: %u, bmControls=0x%08x", xu->num_controls, xu->bmControls);
    LOG_I("└──────────────────────────────────────────────────────────┘");

    /*
     * 遍历bmControls中每一个为1的位
     * 注意: selector 从1开始, 位0对应selector=1, 位1对应selector=2, ...
     */
    for (int bit = 0; bit < 32; bit++) {
        if (!(xu->bmControls & (1U << bit)))
            continue;

        uint8_t selector = bit + 1;
        LOG_I("");
        LOG_I("  ── 控制 selector=%u (bmControls 第%d位) ──", selector, bit);

        /* Step 1: 获取数据长度 */
        uint8_t len_buf[2] = {0};
        if (xu_query(fd, xu->unit_id, selector, UVC_GET_LEN, len_buf, 2) < 0) {
            LOG_W("    GET_LEN 失败: %s (该控制可能不可用)", strerror(errno));
            continue;
        }
        uint16_t data_len = len_buf[0] | (len_buf[1] << 8);
        LOG_I("    数据长度 = %u 字节", data_len);

        if (data_len == 0 || data_len > 4096) {
            LOG_W("    数据长度异常(%u), 跳过", data_len);
            continue;
        }

        /* Step 2: 获取能力信息 */
        uint8_t info_buf[1] = {0};
        if (xu_query(fd, xu->unit_id, selector, UVC_GET_INFO, info_buf, 1) < 0) {
            LOG_W("    GET_INFO 失败: %s", strerror(errno));
        } else {
            LOG_I("    能力标志 = 0x%02x [%s%s]", info_buf[0],
                  (info_buf[0] & UVC_CTRL_INFO_SUPPORTS_GET) ? "可读" : "",
                  (info_buf[0] & UVC_CTRL_INFO_SUPPORTS_SET) ? " 可写" : "");
        }

        /* 分配数据缓冲区 */
        uint8_t *data = calloc(1, data_len);
        if (!data) {
            LOG_E("    内存分配失败");
            continue;
        }

        /* Step 3: 尝试读取各种值 */
        struct {
            uint8_t query;
            const char *label;
        } queries[] = {
            { UVC_GET_CUR, "当前值(CUR)" },
            { UVC_GET_MIN, "最小值(MIN)" },
            { UVC_GET_MAX, "最大值(MAX)" },
            { UVC_GET_RES, "步长(RES)" },
            { UVC_GET_DEF, "默认值(DEF)" },
        };

        for (int q = 0; q < 5; q++) {
            memset(data, 0, data_len);
            if (xu_query(fd, xu->unit_id, selector, queries[q].query, data, data_len) < 0) {
                LOG_D("    %s: 读取失败 (%s)", queries[q].label, strerror(errno));
            } else {
                /* 如果数据较短（<=8字节），以数值形式显示 */
                if (data_len <= 8) {
                    uint64_t val = 0;
                    for (int b = 0; b < data_len && b < 8; b++)
                        val |= (uint64_t)data[b] << (8 * b);
                    LOG_I("    %s = %lu (0x%lx)", queries[q].label,
                          (unsigned long)val, (unsigned long)val);
                } else {
                    LOG_I("    %s:", queries[q].label);
                }
                /* 都做一次hexdump */
                logger_hexdump(LOG_LEVEL_DEBUG, queries[q].label, data, data_len);
            }
        }

        free(data);
    }

    close(fd);
    return 0;
}

int xu_get_value(const char *dev_path, uint8_t unit_id,
                 uint8_t selector, uint8_t *data, uint16_t len, uint8_t query)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_E("打开设备失败: %s", strerror(errno));
        return -1;
    }

    LOG_D("XU读取: unit=%u selector=%u query=%s len=%u",
          unit_id, selector, query_name(query), len);

    int ret = xu_query(fd, unit_id, selector, query, data, len);
    if (ret < 0) {
        LOG_E("XU读取失败: %s", strerror(errno));
    } else {
        logger_hexdump(LOG_LEVEL_DEBUG, "XU读取结果", data, len);
    }

    close(fd);
    return ret;
}

int xu_set_value(const char *dev_path, uint8_t unit_id,
                 uint8_t selector, const uint8_t *data, uint16_t len)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        LOG_E("打开设备失败: %s", strerror(errno));
        return -1;
    }

    LOG_I("XU写入: unit=%u selector=%u len=%u", unit_id, selector, len);
    logger_hexdump(LOG_LEVEL_DEBUG, "写入数据", data, len);

    /* 注意：UVC_SET_CUR的data参数需要可写缓冲区（某些驱动实现） */
    uint8_t *buf = malloc(len);
    if (!buf) {
        close(fd);
        return -1;
    }
    memcpy(buf, data, len);

    int ret = xu_query(fd, unit_id, selector, UVC_SET_CUR, buf, len);
    if (ret < 0) {
        LOG_E("XU写入失败: %s", strerror(errno));
    } else {
        LOG_I("XU写入成功");

        /* 回读验证 */
        memset(buf, 0, len);
        if (xu_query(fd, unit_id, selector, UVC_GET_CUR, buf, len) == 0) {
            logger_hexdump(LOG_LEVEL_INFO, "回读验证", buf, len);
        }
    }

    free(buf);
    close(fd);
    return ret;
}

int xu_probe_all(const char *dev_path, const usb_desc_info_t *desc_info)
{
    if (!desc_info || desc_info->xu_count == 0) {
        LOG_W("没有发现扩展单元(XU)");
        return 0;
    }

    LOG_I("╔══════════════════════════════════════════════════════════════╗");
    LOG_I("║            探测所有扩展单元(XU)的控制                        ║");
    LOG_I("╚══════════════════════════════════════════════════════════════╝");
    LOG_I("共发现 %d 个扩展单元", desc_info->xu_count);

    for (int i = 0; i < desc_info->xu_count; i++) {
        xu_probe_controls(dev_path, &desc_info->xus[i]);
    }

    return 0;
}
