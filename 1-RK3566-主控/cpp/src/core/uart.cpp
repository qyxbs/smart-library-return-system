// =============================================================================
// uart.cpp —— 串口通信实现
// =============================================================================

#include "core/uart.h"

#include <fcntl.h>      // open / O_RDWR / O_NOCTTY
#include <termios.h>    // termios / cfsetispeed / tcsetattr
#include <unistd.h>     // read / write / close
#include <sys/select.h> // select —— 带超时的读

#include <cerrno>
#include <cstdio>     // std::snprintf
#include <cstring>
#include <chrono>
#include <thread>

#include "common/config.h"
#include "common/log.h"

namespace core {

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B9600;
    }
}

Uart::~Uart() {
    close();
}

bool Uart::open(const std::string& dev, int baud) {
    dev_ = dev;
    baud_ = baud;

    fd_ = ::open(dev.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ < 0) {
        LOG_WARN("[UART] 打不开 ", dev, " (", std::strerror(errno),
                 ")，进入模拟模式");
        mock_ = true;
        return false;
    }

    struct termios tty {};
    if (::tcgetattr(fd_, &tty) != 0) {
        LOG_ERROR("[UART] tcgetattr 失败: ", std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        mock_ = true;
        return false;
    }

    ::cfmakeraw(&tty);

    // 波特率（输入和输出分开设，通常相同）
    ::cfsetispeed(&tty, baud_to_speed(baud));
    ::cfsetospeed(&tty, baud_to_speed(baud));

    tty.c_cflag &= ~static_cast<tcflag_t>(CSIZE);   // 先清掉数据位设置
    tty.c_cflag |= CS8 | CLOCAL | CREAD;

    // 无校验（PARENB=奇偶校验使能，PARODD=奇校验）
    tty.c_cflag &= ~static_cast<tcflag_t>(PARENB);

    // 1 位停止位（CSTOPB=2 位停止位）
    tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);

    tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);

    // 关闭软件流控（XON/XOFF）
    tty.c_iflag &= ~static_cast<tcflag_t>(IXON | IXOFF | IXANY);

    // 读超时设置：VMIN=0 + VTIME=1 表示"最多等 0.1 秒"
    // （我们主要用 select 做超时，这里给个兜底值）
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    // 立即生效（TCSANOW）
    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        LOG_ERROR("[UART] tcsetattr 失败: ", std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        mock_ = true;
        return false;
    }

    // 清空输入缓冲区里可能存在的残留数据
    ::tcflush(fd_, TCIOFLUSH);

    mock_ = false;
    LOG_INFO("[UART] 已打开 ", dev, " @ ", baud, " 8N1");
    return true;
}

void Uart::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Uart::write_bytes(const uint8_t* data, size_t len) {
    if (mock_) {
        // 模拟模式：只打日志
        std::string hex;
        char buf[8];
        for (size_t i = 0; i < len; ++i) {
            std::snprintf(buf, sizeof(buf), "%02X ", data[i]);
            hex += buf;
        }
        LOG_INFO("[UART:模拟] ", dev_, " 发送: ", hex);
        return true;
    }

    if (fd_ < 0) {
        LOG_ERROR("[UART] 未打开就写: ", dev_);
        return false;
    }

    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::write(fd_, data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;   // 被信号打断，重试
            LOG_ERROR("[UART] 写失败 ", dev_, ": ", std::strerror(errno));
            return false;
        }
        sent += static_cast<size_t>(n);
    }

    ::tcdrain(fd_);

    return true;
}

bool Uart::write_byte(uint8_t b) {
    return write_bytes(&b, 1);
}

ssize_t Uart::read_bytes(uint8_t* buf, size_t len, int timeout_ms) {
    if (mock_) {
        // 模拟模式：等一会儿然后返回 0（表示超时，没有数据）
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 0;
    }

    if (fd_ < 0) {
        return -1;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(fd_, &readfds);

    struct timeval tv {};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = ::select(fd_ + 1, &readfds, nullptr, nullptr, &tv);

    if (ret < 0) {
        if (errno == EINTR) return 0;
        LOG_ERROR("[UART] select 失败: ", std::strerror(errno));
        return -1;
    }
    if (ret == 0) {
        return 0;   // 超时
    }

    ssize_t n = ::read(fd_, buf, len);
    if (n < 0) {
        if (errno == EINTR) return 0;
        LOG_ERROR("[UART] 读失败: ", std::strerror(errno));
        return -1;
    }
    return n;
}

namespace {

Uart g_uart4;   // STM32 坐标（9600）
Uart g_uart5;   // STM32 控制（38400）

}  // namespace

bool init_uart() {
    bool ok4 = g_uart4.open(cfg::UART4_DEV, cfg::UART4_BAUD);
    bool ok5 = g_uart5.open(cfg::UART5_DEV, cfg::UART5_BAUD);
    return ok4 && ok5;
}

static bool send_action(uint8_t prefix, int coord_y, const char* name) {
    if (coord_y < 11 || coord_y > 28) {
        LOG_ERROR("[UART] 坐标非法: ", coord_y, "（应在 11-28）");
        return false;
    }

    uint8_t buf[2] = {prefix, static_cast<uint8_t>(coord_y)};
    if (!g_uart4.write_bytes(buf, 2)) {
        LOG_ERROR("[UART] ", name, " 坐标发送失败");
        return false;
    }
    LOG_INFO("[UART4] ", name, " 坐标=", coord_y,
             " (", (prefix == 0xFA ? "0xFA 放书" : "0xFB 取书"), ")");

    // 给 STM32 处理坐标的时间
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 控制字：启动机械爪
    const uint8_t ctrl[4] = {0xFD, 0x88, 0x88, 0xFD};
    if (!g_uart5.write_bytes(ctrl, 4)) {
        LOG_ERROR("[UART] 控制字发送失败");
        return false;
    }
    LOG_INFO("[UART5] 控制字 FD 88 88 FD");

    return true;
}

bool uart_store(int coord_y) {
    return send_action(0xFA, coord_y, "放书");
}

bool uart_retrieve(int coord_y) {
    return send_action(0xFB, coord_y, "取书");
}

}  // namespace core
