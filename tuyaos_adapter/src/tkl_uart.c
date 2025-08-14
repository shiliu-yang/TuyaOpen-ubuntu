#include "tkl_uart.h"
#include <stdio.h>
#include <termios.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

typedef struct {
    int fd;
    pthread_t tid;
    TUYA_UART_IRQ_CB rx_cb;
    uint32_t port_id;
    uint8_t readchar;
    uint8_t readbuff[1024];
    // 新增缓存相关字段（复用readbuff）
    uint32_t cache_head;         // 缓存头指针
    uint32_t cache_tail;         // 缓存尾指针
    pthread_mutex_t cache_mutex; // 缓存访问互斥锁
} uart_dev_t;

static uart_dev_t s_uart_dev[3];

// 缓存操作辅助函数（使用readbuff）
static uint32_t cache_available_space(uart_dev_t *dev)
{
    return sizeof(dev->readbuff) - 1 -
           ((dev->cache_tail - dev->cache_head + sizeof(dev->readbuff)) % sizeof(dev->readbuff));
}

static uint32_t cache_available_data(uart_dev_t *dev)
{
    return (dev->cache_tail - dev->cache_head + sizeof(dev->readbuff)) % sizeof(dev->readbuff);
}

static uint32_t cache_write(uart_dev_t *dev, uint8_t *data, uint32_t len)
{
    pthread_mutex_lock(&dev->cache_mutex);

    uint32_t available = cache_available_space(dev);
    uint32_t write_len = (len > available) ? available : len;

    for (uint32_t i = 0; i < write_len; i++) {
        dev->readbuff[dev->cache_tail] = data[i];
        dev->cache_tail = (dev->cache_tail + 1) % sizeof(dev->readbuff);
    }

    pthread_mutex_unlock(&dev->cache_mutex);
    return write_len;
}

static uint32_t cache_read(uart_dev_t *dev, uint8_t *data, uint32_t len)
{
    pthread_mutex_lock(&dev->cache_mutex);

    uint32_t available = cache_available_data(dev);
    uint32_t read_len = (len > available) ? available : len;

    for (uint32_t i = 0; i < read_len; i++) {
        data[i] = dev->readbuff[dev->cache_head];
        dev->cache_head = (dev->cache_head + 1) % sizeof(dev->readbuff);
    }

    pthread_mutex_unlock(&dev->cache_mutex);
    return read_len;
}

static void *__tty_irq_handler(void *arg)
{
    uart_dev_t *uart_dev = arg;
    uint8_t temp_buff[256]; // 临时缓冲区
    struct timeval timeout;

    for (;;) {
        fd_set readfd;

        FD_ZERO(&readfd);
        FD_SET(uart_dev->fd, &readfd);

        // 设置超时时间为100ms，用于定期检查缓存
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        int result = select(uart_dev->fd + 1, &readfd, NULL, NULL, &timeout);

        if (result > 0 && FD_ISSET(uart_dev->fd, &readfd)) {
            // 有新数据到达，循环读取直到没有更多数据
            while (1) {
                ssize_t readlen = read(uart_dev->fd, temp_buff, sizeof(temp_buff));
                if (readlen > 0) {
                    // 将数据写入缓存
                    cache_write(uart_dev, temp_buff, readlen);
                } else if (readlen == 0) {
                    // 没有更多数据
                    break;
                } else {
                    // 读取出错或没有数据可读(EAGAIN/EWOULDBLOCK)
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // 没有更多数据可读，退出循环
                        break;
                    } else {
                        // 其他错误，也退出循环
                        break;
                    }
                }
            }

            // 读取完成后，如果缓存中有数据，调用一次回调
            if (cache_available_data(uart_dev) > 0) {
                uart_dev->rx_cb(uart_dev->port_id);
            }
        } else if (result == 0) {
            // 超时，检查缓存中是否有数据
            if (cache_available_data(uart_dev) > 0) {
                // 缓存中有数据，调用回调函数通知上层读取
                uart_dev->rx_cb(uart_dev->port_id);
            }
        }
        // result < 0 表示select出错，继续循环
    }
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
            uart_dev->rx_cb(uart_dev->port_id);
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
                uart_dev->rx_cb(uart_dev->port_id);
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

        s_uart_dev[port_id].port_id = port_id;
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
        s_uart_dev[port_id].port_id = port_id;
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
        struct termios term_orig;
        struct termios term_cfg;

        s_uart_dev[port_id].port_id = port_id;

        // 初始化缓存相关字段
        s_uart_dev[port_id].cache_head = 0;
        s_uart_dev[port_id].cache_tail = 0;
        if (pthread_mutex_init(&s_uart_dev[port_id].cache_mutex, NULL) != 0) {
            perror("pthread_mutex_init error");
            return OPRT_COM_ERROR;
        }

        // 打开串口设备
        s_uart_dev[port_id].fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_NDELAY);
        if (0 > s_uart_dev[port_id].fd) {
            perror("open /dev/ttyUSB0 error");
            pthread_mutex_destroy(&s_uart_dev[port_id].cache_mutex);
            return OPRT_COM_ERROR;
        }

        // 获取当前串口配置
        if (tcgetattr(s_uart_dev[port_id].fd, &term_orig) != 0) {
            perror("tcgetattr error");
            close(s_uart_dev[port_id].fd);
            return OPRT_COM_ERROR;
        }

        // 配置串口参数
        term_cfg = term_orig;

        // 设置波特率
        speed_t baudrate = B115200; // 默认115200波特率
        if (cfg) {
            switch (cfg->baudrate) {
            case 9600:
                baudrate = B9600;
                break;
            case 19200:
                baudrate = B19200;
                break;
            case 38400:
                baudrate = B38400;
                break;
            case 57600:
                baudrate = B57600;
                break;
            case 115200:
                baudrate = B115200;
                break;
            case 230400:
                baudrate = B230400;
                break;
            case 460800:
                baudrate = B460800;
                break;
            case 921600:
                baudrate = B921600;
                break;
            default:
                baudrate = B115200;
                break;
            }
        }
        cfsetispeed(&term_cfg, baudrate);
        cfsetospeed(&term_cfg, baudrate);

        // 控制模式设置
        term_cfg.c_cflag &= ~PARENB; // 清除校验位
        term_cfg.c_cflag &= ~CSTOPB; // 1个停止位
        term_cfg.c_cflag &= ~CSIZE;  // 清除数据位掩码
        term_cfg.c_cflag |= CS8;     // 8个数据位
        term_cfg.c_cflag |= CLOCAL;  // 忽略调制解调器状态线
        term_cfg.c_cflag |= CREAD;   // 启用接收器

        // 根据配置设置校验位
        if (cfg) {
            switch (cfg->parity) {
            case TUYA_UART_PARITY_TYPE_ODD:
                term_cfg.c_cflag |= (PARENB | PARODD);
                break;
            case TUYA_UART_PARITY_TYPE_EVEN:
                term_cfg.c_cflag |= PARENB;
                term_cfg.c_cflag &= ~PARODD;
                break;
            case TUYA_UART_PARITY_TYPE_NONE:
            default:
                term_cfg.c_cflag &= ~PARENB;
                break;
            }

            // 设置停止位
            if (cfg->stopbits == TUYA_UART_STOP_LEN_2BIT) {
                term_cfg.c_cflag |= CSTOPB;
            } else {
                term_cfg.c_cflag &= ~CSTOPB;
            }

            // 设置数据位
            term_cfg.c_cflag &= ~CSIZE;
            switch (cfg->databits) {
            case TUYA_UART_DATA_LEN_5BIT:
                term_cfg.c_cflag |= CS5;
                break;
            case TUYA_UART_DATA_LEN_6BIT:
                term_cfg.c_cflag |= CS6;
                break;
            case TUYA_UART_DATA_LEN_7BIT:
                term_cfg.c_cflag |= CS7;
                break;
            case TUYA_UART_DATA_LEN_8BIT:
            default:
                term_cfg.c_cflag |= CS8;
                break;
            }
        }

        // 输入模式设置
        term_cfg.c_iflag &= ~(IXON | IXOFF | IXANY); // 禁用软件流控制
        term_cfg.c_iflag &= ~(INLCR | ICRNL);        // 禁用换行符转换

        // 输出模式设置
        term_cfg.c_oflag &= ~OPOST; // 原始输出模式

        // 本地模式设置
        term_cfg.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // 原始模式

        // 控制字符设置
        term_cfg.c_cc[VTIME] = 0; // 非阻塞读取
        term_cfg.c_cc[VMIN] = 1;  // 最小读取字符数

        // 应用配置
        if (tcsetattr(s_uart_dev[port_id].fd, TCSANOW, &term_cfg) != 0) {
            perror("tcsetattr error");
            close(s_uart_dev[port_id].fd);
            return OPRT_COM_ERROR;
        }

        // 清空输入输出缓冲区
        tcflush(s_uart_dev[port_id].fd, TCIOFLUSH);

        // 创建接收线程，使用专门的tty中断处理器
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

    if (1 == port_id || 2 == port_id) {
        pthread_cancel(s_uart_dev[port_id].tid);
        pthread_join(s_uart_dev[port_id].tid, 0);

        // 清理port_id=2的缓存资源
        if (2 == port_id) {
            pthread_mutex_destroy(&s_uart_dev[port_id].cache_mutex);
        }
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
    if (0 == port_id || 2 == port_id) {
        ssize_t bytes_written = 0;
        ssize_t total_written = 0;
        char *data = (char *)buff;

        while (total_written < len) {
            bytes_written = write(s_uart_dev[port_id].fd, data + total_written, len - total_written);

            if (bytes_written > 0) {
                total_written += bytes_written;
            } else if (bytes_written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 缓冲区满，稍微等待
                    usleep(1000); // 等待1ms
                    continue;
                } else {
                    // 其他错误，返回已写入的字节数
                    return total_written > 0 ? total_written : -1;
                }
            } else {
                // bytes_written == 0, 设备可能关闭
                break;
            }
        }

        return total_written;
    } else if (1 == port_id) {
        int port = 7878;
        const char *ip = "172.16.61.117"; // IP地址字符串
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = inet_addr(ip);

        return sendto(s_uart_dev[port_id].fd, buff, len, 0, (struct sockaddr *)&address, sizeof(address));
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
        ssize_t bytes_read;

        // 使用循环而不是递归来处理EINTR
        while (1) {
            bytes_read = read(s_uart_dev[port_id].fd, buff, len);

            if (bytes_read >= 0) {
                return bytes_read;
            } else {
                // 处理读取错误
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 没有数据可读（非阻塞模式下正常情况）
                    return 0;
                } else if (errno == EINTR) {
                    // 被信号中断，继续循环重试
                    continue;
                } else {
                    // 其他错误
                    return -1;
                }
            }
        }
    } else if (1 == port_id) {
        if (len > 0) {
            *(uint8_t *)buff = s_uart_dev[port_id].readchar;
            return 1;
        }
        return 0;
    } else if (2 == port_id) {
        // 使用缓存读取
        return cache_read(&s_uart_dev[port_id], (uint8_t *)buff, len);
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
