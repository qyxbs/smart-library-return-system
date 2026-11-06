// =============================================================================
// =============================================================================
// 【Linux 里串口是什么】
#pragma once

#include <cstdint>
#include <string>

namespace core {

class Uart {
public:
    Uart() = default;
    ~Uart();

    Uart(const Uart&) = delete;
    Uart& operator=(const Uart&) = delete;

    bool open(const std::string& dev, int baud);

    // 关掉（析构时也会自动关）
    void close();

    bool is_open() const { return fd_ >= 0; }

    // 是否是模拟模式（设备打不开时为 true）
    bool is_mock() const { return mock_; }

    bool write_bytes(const uint8_t* data, size_t len);

    // 写一个字节（最常用）
    bool write_byte(uint8_t b);

    ssize_t read_bytes(uint8_t* buf, size_t len, int timeout_ms = 1000);

    // 设备路径（日志用）
    const std::string& device() const { return dev_; }

private:
    int fd_ = -1;
    std::string dev_;
    int baud_ = 0;
    bool mock_ = false;   // 打不开设备时进入模拟模式
};

// 初始化全局串口（程序启动时调用一次）
// 返回是否成功打开了真实设备（false 表示进入了模拟模式）
bool init_uart();

bool uart_store(int coord_y);

// 让机械爪"取书"（从指定坐标拿走）
//   协议：UART4 发 0xFB + coord_y，等 0.5 秒，UART5 发 FD 88 88 FD
bool uart_retrieve(int coord_y);

}  // namespace core
