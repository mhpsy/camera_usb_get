/*
 * xu_ctrl.h - UVC 扩展单元(Extension Unit)控制模块
 *
 * === 什么是扩展单元(XU)? ===
 *
 * UVC规范定义了标准的控制项（亮度、对比度等），但厂商经常需要提供
 * 非标准的功能，比如：LED指示灯控制、HDR模式、人脸检测、固件更新等。
 *
 * 扩展单元就是为此设计的：每个XU由一个GUID标识，内部包含多个"控制"(control)，
 * 每个控制由一个selector编号（从1开始）标识。
 *
 * 访问XU的流程：
 * 1. 通过USB描述符找到XU的 unit_id 和 GUID
 * 2. 通过 UVCIOC_CTRL_QUERY ioctl 发送请求
 * 3. 先用 UVC_GET_LEN 获取数据长度
 * 4. 再用 UVC_GET_INFO 获取能力（是否可读/可写）
 * 5. 然后用 UVC_GET_CUR/MIN/MAX/DEF 读取值
 * 6. 用 UVC_SET_CUR 写入值
 */

#ifndef XU_CTRL_H
#define XU_CTRL_H

#include <stdint.h>
#include "usb_desc.h"

/* UVC XU 查询类型 */
#define UVC_SET_CUR  0x01  /* 设置当前值 */
#define UVC_GET_CUR  0x81  /* 获取当前值 */
#define UVC_GET_MIN  0x82  /* 获取最小值 */
#define UVC_GET_MAX  0x83  /* 获取最大值 */
#define UVC_GET_RES  0x84  /* 获取分辨率(步长) */
#define UVC_GET_LEN  0x85  /* 获取数据长度(字节数) */
#define UVC_GET_INFO 0x86  /* 获取支持的操作(GET/SET) */
#define UVC_GET_DEF  0x87  /* 获取默认值 */

/* XU INFO 标志位 */
#define UVC_CTRL_INFO_SUPPORTS_GET  (1 << 0)  /* 支持读取 */
#define UVC_CTRL_INFO_SUPPORTS_SET  (1 << 1)  /* 支持写入 */

/*
 * 探测一个XU的所有控制：
 * 遍历每个selector，获取长度、能力、当前值等，并打印详细信息
 *
 * dev_path: V4L2设备路径 (如 "/dev/video0")
 * xu: 从USB描述符解析得到的XU信息
 */
int xu_probe_controls(const char *dev_path, const xu_info_t *xu);

/*
 * 读取某个XU控制的值
 *
 * dev_path: V4L2设备路径
 * unit_id: XU的UnitID
 * selector: 控制编号（从1开始）
 * data: 输出缓冲区
 * len: 数据长度（从 UVC_GET_LEN 获取）
 * query: 查询类型 (UVC_GET_CUR, UVC_GET_MIN 等)
 */
int xu_get_value(const char *dev_path, uint8_t unit_id,
                 uint8_t selector, uint8_t *data, uint16_t len, uint8_t query);

/*
 * 设置某个XU控制的值
 *
 * dev_path: V4L2设备路径
 * unit_id: XU的UnitID
 * selector: 控制编号
 * data: 要写入的数据
 * len: 数据长度
 */
int xu_set_value(const char *dev_path, uint8_t unit_id,
                 uint8_t selector, const uint8_t *data, uint16_t len);

/*
 * 探测所有XU的所有控制
 */
int xu_probe_all(const char *dev_path, const usb_desc_info_t *desc_info);

#endif /* XU_CTRL_H */
