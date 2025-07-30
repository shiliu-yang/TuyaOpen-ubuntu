#include "tal_log.h"
#include "tkl_uart.h"
#include <stdio.h>
#include <termios.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct {
    int fd;
    pthread_t tid;
    TUYA_UART_IRQ_CB rx_cb;
    uint8_t readchar;
    uint8_t readbuff[5 * 1024];
    // 字符队列用于正确的数据传递
    uint8_t char_queue[5 * 1024];
    int queue_head;
    int queue_tail;
    int queue_count;
    pthread_mutex_t queue_mutex;
    int initialized; // 标记是否已初始化
} uart_dev_t;

static uart_dev_t s_uart_dev[3];

// 字符队列操作函数
static void uart_queue_init(uart_dev_t *dev)
{
    dev->queue_head = 0;
    dev->queue_tail = 0;
    dev->queue_count = 0;
    pthread_mutex_init(&dev->queue_mutex, NULL);
}

static int uart_queue_put(uart_dev_t *dev, uint8_t *ch, uint32_t len)
{
    pthread_mutex_lock(&dev->queue_mutex);

    if (dev->queue_count + len > sizeof(dev->char_queue)) {
        pthread_mutex_unlock(&dev->queue_mutex);
        PR_WARN("uart_queue_put: queue full, dropping byte 0x%02X", ch);
        return 0; // 队列满
    }

    for (uint32_t i = 0; i < len; i++) {
        dev->char_queue[dev->queue_tail] = ch[i];
        dev->queue_tail = (dev->queue_tail + 1) % sizeof(dev->char_queue);
    }
    dev->queue_count += len;

    // dev->char_queue[dev->queue_tail] = ch;
    // dev->queue_tail = (dev->queue_tail + 1) % sizeof(dev->char_queue);
    // dev->queue_count++;

    pthread_mutex_unlock(&dev->queue_mutex);
    return len; // 成功
}

static int uart_queue_get(uart_dev_t *dev, uint8_t *ch)
{
    pthread_mutex_lock(&dev->queue_mutex);

    if (dev->queue_count == 0) {
        pthread_mutex_unlock(&dev->queue_mutex);
        return 0; // 队列空
    }

    *ch = dev->char_queue[dev->queue_head];
    dev->queue_head = (dev->queue_head + 1) % sizeof(dev->char_queue);
    dev->queue_count--;

    pthread_mutex_unlock(&dev->queue_mutex);
    return 1; // 成功
}

static void *__irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    if (!uart_dev) {
        PR_ERR("__irq_handler: invalid argument");
        return NULL;
    }

    for (;;) {
        fd_set readfd;

        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)) {
            if (uart_dev->rx_cb) {
                uart_dev->rx_cb(0);
            }
        }
    }
}

static void *__udp_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    if (!uart_dev) {
        PR_ERR("__udp_irq_handler: invalid argument");
        return NULL;
    }

    for (;;) {
        fd_set readfd;
        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)) {
            ssize_t readlen = recvfrom(uart_dev->fd, uart_dev->readbuff, sizeof(uart_dev->readbuff), 0, NULL, 0);
            if (readlen > 0) {
                // 初始化队列（如果还没有初始化）
                if (!uart_dev->initialized) {
                    uart_queue_init(uart_dev);
                    uart_dev->initialized = 1;
                }

                // 将数据放入队列
                for (int i = 0; i < readlen; i++) {
                    if (uart_queue_put(uart_dev, &uart_dev->readbuff[i], 1)) {
                        uart_dev->readchar = uart_dev->readbuff[i];
                        if (uart_dev->rx_cb) {
                            uart_dev->rx_cb(1);
                        }
                    }
                }
            }
        }
    }
}

static void *__tty_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    if (!uart_dev) {
        PR_ERR("__tty_irq_handler: invalid argument");
        return NULL;
    }

    for (;;) {
        fd_set readfd;
        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);

        int select_ret = select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);

        if (select_ret < 0) {
            PR_ERR("UART2 select error: %s", strerror(errno));
            continue;
        }

        if (FD_ISSET(uart_dev->fd, &readfd)) {
            memset(uart_dev->readbuff, 0, sizeof(uart_dev->readbuff));

            ssize_t readlen = read(uart_dev->fd, uart_dev->readbuff, sizeof(uart_dev->readbuff));

            PR_HEXDUMP_DEBUG("tkl read data", uart_dev->readbuff, readlen);

            if (readlen > 0) {
                uart_queue_put(uart_dev, uart_dev->readbuff, readlen);
                if (uart_dev->rx_cb) {
                    uart_dev->rx_cb(2); // 触发中断
                }
            } else if (readlen == 0) {
                PR_WARN("UART2 EOF detected");
            } else if (readlen < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    PR_ERR("UART2 read error: %s (errno: %d)", strerror(errno), errno);
                    break;
                }
            }
        }
    }

    PR_ERR("UART2 interrupt handler exiting");
    return NULL;
}

/**
 * @brief uart init
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] cfg: uart config
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_init(uint32_t port_id, TUYA_UART_BASE_CFG_T *cfg)
{
    if (port_id >= 3) {
        return OPRT_INVALID_PARM;
    }

    // 初始化设备结构
    memset(&s_uart_dev[port_id], 0, sizeof(uart_dev_t));
    s_uart_dev[port_id].fd = -1;
    s_uart_dev[port_id].initialized = 0;

    if (0 == port_id) {
        struct termios term_orig;
        struct termios term_vi;

        s_uart_dev[port_id].fd = open("/dev/stdin", O_RDWR);
        if (0 > s_uart_dev[port_id].fd) {
            return OPRT_COM_ERROR;
        }

        tcgetattr(s_uart_dev[port_id].fd, &term_orig);
        term_vi = term_orig;
        term_vi.c_lflag &= (~ICANON & ~ECHO); // leave ISIG ON- allow intr's
        term_vi.c_iflag &= (~IXON & ~ICRNL);
        tcsetattr(s_uart_dev[port_id].fd, TCSANOW, &term_vi);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int ret = pthread_create(&s_uart_dev[port_id].tid, &attr, __irq_handler, &s_uart_dev[port_id]);
        pthread_attr_destroy(&attr);

        if (ret != 0) {
            PR_ERR("Failed to create thread for port 0: %s", strerror(ret));
            close(s_uart_dev[port_id].fd);
            return OPRT_COM_ERROR;
        }

    } else if (1 == port_id) {
        s_uart_dev[port_id].fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_uart_dev[port_id].fd < 0) {
            PR_ERR("Failed to create UDP socket: %s", strerror(errno));
            return OPRT_COM_ERROR;
        }

        fcntl(s_uart_dev[port_id].fd, F_SETFD, FD_CLOEXEC);

        int port = 7878;
        const char *ip = "172.16.208.90"; // IP地址字符串
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = inet_addr(ip);

        if (bind(s_uart_dev[port_id].fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
            PR_ERR("UDP bind error: %s", strerror(errno));
            close(s_uart_dev[port_id].fd);
            return OPRT_COM_ERROR;
        }

        int ret = pthread_create(&s_uart_dev[port_id].tid, NULL, __udp_irq_handler, &s_uart_dev[port_id]);
        if (ret != 0) {
            PR_ERR("Failed to create thread for port 1: %s", strerror(ret));
            close(s_uart_dev[port_id].fd);
            return OPRT_COM_ERROR;
        }
    } else if (2 == port_id) {
        struct termios term_vi;

        // 先检查设备是否存在
        if (access("/dev/ttyUSB0", F_OK) != 0) {
            PR_ERR("UART2 device /dev/ttyUSB0 does not exist: %s", strerror(errno));
            return OPRT_COM_ERROR;
        }

        if (access("/dev/ttyUSB0", R_OK | W_OK) != 0) {
            PR_WARN("UART2 device /dev/ttyUSB0 permission issue: %s", strerror(errno));
        }

        s_uart_dev[port_id].fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (0 > s_uart_dev[port_id].fd) {
            PR_ERR("Failed to open /dev/ttyUSB0: %s (errno: %d)", strerror(errno), errno);
            return OPRT_COM_ERROR;
        }

        // 初始化字符队列
        uart_queue_init(&s_uart_dev[port_id]);
        s_uart_dev[port_id].initialized = 1;

        // 配置串口参数
        if (tcgetattr(s_uart_dev[port_id].fd, &term_vi) != 0) {
            PR_ERR("UART2 tcgetattr failed: %s", strerror(errno));
        }

        // 设置波特率
        cfsetispeed(&term_vi, B921600);
        cfsetospeed(&term_vi, B921600);

        // 控制模式配置
        term_vi.c_cflag = CS8 | CREAD | CLOCAL; // 8位数据，启用接收，本地连接
        term_vi.c_cflag &= ~PARENB;             // 无奇偶校验
        term_vi.c_cflag &= ~CSTOPB;             // 1个停止位

        // 输入模式配置 - 完全原始模式
        term_vi.c_iflag = 0;

        // 输出模式配置 - 完全原始模式
        term_vi.c_oflag = 0;

        // 本地模式配置 - 完全原始模式
        term_vi.c_lflag = 0;

        // 控制字符配置
        term_vi.c_cc[VMIN] = 1;  // 最少读取1个字符
        term_vi.c_cc[VTIME] = 1; // 100ms超时

        if (tcsetattr(s_uart_dev[port_id].fd, TCSANOW, &term_vi) != 0) {
            PR_ERR("UART2 tcsetattr failed: %s", strerror(errno));
        }

        tcflush(s_uart_dev[port_id].fd, TCIOFLUSH);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int ret = pthread_create(&s_uart_dev[port_id].tid, &attr, __tty_irq_handler, &s_uart_dev[port_id]);
        pthread_attr_destroy(&attr);

        if (ret != 0) {
            PR_ERR("Failed to create thread for port 2: %s", strerror(ret));
            close(s_uart_dev[port_id].fd);
            pthread_mutex_destroy(&s_uart_dev[port_id].queue_mutex);
            return OPRT_COM_ERROR;
        }
    }

    return OPRT_OK;
}

/**
 * @brief uart deinit
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_deinit(uint32_t port_id)
{
    if (port_id >= 3) {
        return OPRT_INVALID_PARM;
    }

    if (s_uart_dev[port_id].fd > 0) {
        close(s_uart_dev[port_id].fd);
        s_uart_dev[port_id].fd = -1;
    }

    if (0 == port_id) {
        // port_id 0 的线程是分离状态，只需要取消
        pthread_cancel(s_uart_dev[port_id].tid);
    } else if (1 == port_id) {
        pthread_cancel(s_uart_dev[port_id].tid);
        pthread_join(s_uart_dev[port_id].tid, NULL);
        // 销毁UDP端口的互斥锁（如果已初始化）
        if (s_uart_dev[port_id].initialized) {
            pthread_mutex_destroy(&s_uart_dev[port_id].queue_mutex);
        }
    } else if (2 == port_id) {
        pthread_cancel(s_uart_dev[port_id].tid);
        pthread_join(s_uart_dev[port_id].tid, NULL);
        // 销毁互斥锁
        if (s_uart_dev[port_id].initialized) {
            pthread_mutex_destroy(&s_uart_dev[port_id].queue_mutex);
        }
    }

    s_uart_dev[port_id].initialized = 0;
    return OPRT_OK;
}

/**
 * @brief uart write data
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] data: write buff
 * @param[in] len:  buff len
 *
 * @return return > 0: number of data written; return <= 0: write errror
 */
int tkl_uart_write(uint32_t port_id, void *buff, uint16_t len)
{
    if (port_id >= 3 || !buff || len == 0) {
        return -1;
    }

    if (s_uart_dev[port_id].fd < 0) {
        return -1;
    }

    if (0 == port_id) {
        return write(s_uart_dev[port_id].fd, buff, len);
    } else if (1 == port_id) {
        int port = 7878;
        const char *ip = "172.16.61.117"; // IP地址字符串
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = inet_addr(ip);

        return sendto(s_uart_dev[port_id].fd, buff, len, 0, (struct sockaddr *)&address, sizeof(address));
    } else if (2 == port_id) {
        ssize_t written = write(s_uart_dev[port_id].fd, buff, len);

        PR_DEBUG("UART2 write: expected %d bytes, wrote %zd bytes", len, written);

        if (written < 0) {
            PR_ERR("UART2 write error: %s", strerror(errno));
        }

        // 强制刷新输出缓冲区
        if (tcdrain(s_uart_dev[port_id].fd) != 0) {
            PR_WARN("UART2 tcdrain failed: %s", strerror(errno));
        }

        return written;
    }

    return -1;
}

/**
 * @brief enable uart rx interrupt and regist interrupt callback
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] rx_cb: receive callback
 *
 * @return none
 */
void tkl_uart_rx_irq_cb_reg(uint32_t port_id, TUYA_UART_IRQ_CB rx_cb)
{
    if (port_id >= 3) {
        return;
    }
    s_uart_dev[port_id].rx_cb = rx_cb;
    return;
}

/**
 * @brief regist uart tx interrupt callback
 * If this function is called, it indicates that the data is sent asynchronously through interrupt,
 * and then write is invoked to initiate asynchronous transmission.
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] rx_cb: receive callback
 *
 * @return none
 */
void tkl_uart_tx_irq_cb_reg(uint32_t port_id, TUYA_UART_IRQ_CB tx_cb)
{
    return;
}

/**
 * @brief uart read data
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[out] data: read data
 * @param[in] len:  buff len
 *
 * @return return >= 0: number of data read; return < 0: read errror
 */
int tkl_uart_read(uint32_t port_id, void *buff, uint16_t len)
{
    if (port_id >= 3 || !buff || len == 0) {
        return -1;
    }

    if (s_uart_dev[port_id].fd < 0) {
        return -1;
    }

    if (0 == port_id) {
        return read(s_uart_dev[port_id].fd, buff, len);
    } else if (1 == port_id) {
        // UDP端口：从队列中读取数据
        if (!s_uart_dev[port_id].initialized) {
            return 0; // 队列未初始化，没有数据
        }

        uint8_t *buf = (uint8_t *)buff;
        int read_count = 0;

        // 尽可能多地从队列中读取数据
        for (int i = 0; i < len; i++) {
            uint8_t ch;
            if (uart_queue_get(&s_uart_dev[port_id], &ch)) {
                buf[i] = ch;
                read_count++;
            } else {
                break; // 队列为空
            }
        }

        return read_count;
    } else if (2 == port_id) {
        // TTY端口：从字符队列中读取数据
        uint8_t *buf = (uint8_t *)buff;
        int read_count = 0;

        // 尽可能多地从队列中读取数据
        for (int i = 0; i < len; i++) {
            uint8_t ch;
            if (uart_queue_get(&s_uart_dev[port_id], &ch)) {
                buf[i] = ch;
                read_count++;
            } else {
                break; // 队列为空
            }
        }

        return read_count;
    }

    return -1;
}

/**
 * @brief set uart transmit interrupt status
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] enable: TRUE-enalbe tx int, FALSE-disable tx int
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_set_tx_int(uint32_t port_id, BOOL_T enable)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief set uart receive flowcontrol
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] enable: TRUE-enalbe rx flowcontrol, FALSE-disable rx flowcontrol
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_set_rx_flowctrl(uint32_t port_id, BOOL_T enable)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief wait for uart data
 *
 * @param[in] port_id: uart port id, id index starts at 0
 *                     in linux platform,
 *                         high 16 bits aslo means uart type,
 *                                   it's value must be one of the TUYA_UART_TYPE_E type
 *                         the low 16bit - means uart port id
 *                         you can input like this TUYA_UART_PORT_ID(TUYA_UART_SYS, 2)
 * @param[in] timeout_ms: the max wait time, unit is millisecond
 *                        -1 : block indefinitely
 *                        0  : non-block
 *                        >0 : timeout in milliseconds
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_wait_for_data(uint32_t port_id, int timeout_ms)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief uart control
 *
 * @param[in] uart refer to tuya_uart_t
 * @param[in] cmd control command
 * @param[in] arg command argument
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tkl_uart_ioctl(uint32_t port_id, uint32_t cmd, void *arg)
{
    return OPRT_NOT_SUPPORTED;
}
