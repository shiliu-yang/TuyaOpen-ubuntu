#include "tal_log.h"
#include "tkl_uart.h"
#include <stdio.h>
#include <termios.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct {
    int fd;
    pthread_t tid;
    TUYA_UART_IRQ_CB rx_cb;
    uint8_t readchar;
    uint8_t readbuff[1024];
    // 字符队列用于正确的数据传递
    uint8_t char_queue[1024];
    int queue_head;
    int queue_tail;
    int queue_count;
    pthread_mutex_t queue_mutex;
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

static int uart_queue_put(uart_dev_t *dev, uint8_t ch)
{
    pthread_mutex_lock(&dev->queue_mutex);

    if (dev->queue_count >= sizeof(dev->char_queue)) {
        pthread_mutex_unlock(&dev->queue_mutex);
        return 0; // 队列满
    }

    dev->char_queue[dev->queue_tail] = ch;
    dev->queue_tail = (dev->queue_tail + 1) % sizeof(dev->char_queue);
    dev->queue_count++;

    pthread_mutex_unlock(&dev->queue_mutex);
    return 1; // 成功
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

    for (;;) {
        fd_set readfd;

        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)) {
            uart_dev->rx_cb(0);
        }
    }
}

static void *__udp_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    for (;;) {
        fd_set readfd;
        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)) {
            ssize_t readlen = recvfrom(uart_dev->fd, uart_dev->readbuff, sizeof(uart_dev->readbuff), 0, NULL, 0);
            for (int i = 0; i < readlen; i++) {
                uart_dev->readchar = uart_dev->readbuff[i];
                uart_dev->rx_cb(1);
            }
        }
    }
}

static void *__tty_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;

    PR_DEBUG("UART2 interrupt handler started");

    for (;;) {
        fd_set readfd;
        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);
        select(uart_dev->fd + 1, &readfd, NULL, NULL, NULL);
        if (FD_ISSET(uart_dev->fd, &readfd)) {
            memset(uart_dev->readbuff, 0, sizeof(uart_dev->readbuff));

            ssize_t readlen = read(uart_dev->fd, uart_dev->readbuff, sizeof(uart_dev->readbuff));
            if (readlen > 0) {
                PR_DEBUG("UART2 received %zd bytes", readlen);

                // 将接收到的数据逐字符放入队列并触发中断
                for (int i = 0; i < readlen; i++) {
                    uint8_t current_char = uart_dev->readbuff[i];

                    // 将字符放入队列
                    if (uart_queue_put(uart_dev, current_char)) {
                        uart_dev->readchar = current_char;
                        if (uart_dev->rx_cb) {
                            uart_dev->rx_cb(2); // 触发中断
                        }
                    }
                }
            }
        }
    }
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
        pthread_create(&s_uart_dev[port_id].tid, &attr, __irq_handler, &s_uart_dev[port_id]);
        pthread_attr_destroy(&attr);

    } else if (1 == port_id) {
        s_uart_dev[port_id].fd = socket(AF_INET, SOCK_DGRAM, 0);
        fcntl(s_uart_dev[port_id].fd, F_SETFD, FD_CLOEXEC);

        int port = 7878;
        const char *ip = "172.16.208.90"; // IP地址字符串
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = inet_addr(ip);

        if (bind(s_uart_dev[port_id].fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
            perror("bind error");
            exit(1);
        }

        pthread_create(&s_uart_dev[port_id].tid, NULL, __udp_irq_handler, &s_uart_dev[port_id]);
    } else if (2 == port_id) {
        struct termios term_vi;

        s_uart_dev[port_id].fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
        if (0 > s_uart_dev[port_id].fd) {
            PR_ERR("Failed to open /dev/ttyUSB0: %s (errno: %d)", strerror(errno), errno);
            return OPRT_COM_ERROR;
        }

        // 初始化字符队列
        uart_queue_init(&s_uart_dev[port_id]);

        // 配置串口参数
        tcgetattr(s_uart_dev[port_id].fd, &term_vi);

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

        tcsetattr(s_uart_dev[port_id].fd, TCSANOW, &term_vi);
        tcflush(s_uart_dev[port_id].fd, TCIOFLUSH);

        // 检查串口状态
        int status;
        if (ioctl(s_uart_dev[port_id].fd, TIOCMGET, &status) == 0) {
            PR_DEBUG("UART2 initial modem status: 0x%x", status);
        }

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&s_uart_dev[port_id].tid, &attr, __tty_irq_handler, &s_uart_dev[port_id]);
        pthread_attr_destroy(&attr);
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
    close(s_uart_dev[port_id].fd);

    if (1 == port_id) {
        pthread_cancel(s_uart_dev[port_id].tid);
        pthread_join(s_uart_dev[port_id].tid, 0);
    } else if (2 == port_id) {
        pthread_cancel(s_uart_dev[port_id].tid);
        pthread_join(s_uart_dev[port_id].tid, 0);
    }

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
        return write(s_uart_dev[port_id].fd, buff, len);
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
    if (0 == port_id) {
        return read(s_uart_dev[port_id].fd, buff, len);
    } else if (1 == port_id) {
        *(uint8_t *)buff = s_uart_dev[port_id].readchar;
        return 1;
    } else if (2 == port_id) {
        // 从字符队列中读取数据而不是返回固定的readchar
        uint8_t ch;
        if (uart_queue_get(&s_uart_dev[port_id], &ch)) {
            *(uint8_t *)buff = ch;
            return 1;
        } else {
            return 0; // 没有数据可读
        }
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
