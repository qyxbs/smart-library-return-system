// uart.h
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

    void close();

    bool is_open() const { return fd_ >= 0; }

    bool is_mock() const { return mock_; }

    bool write_bytes(const uint8_t* data, size_t len);

    // 写一个字节（最常用）
    bool write_byte(uint8_t b);

    ssize_t read_bytes(uint8_t* buf, size_t len, int timeout_ms = 1000);

    const std::string& device() const { return dev_; }

private:
    int fd_ = -1;
    std::string dev_;
    int baud_ = 0;
    bool mock_ = false;
};

bool init_uart();

bool uart_store(int coord_y);

//   协议：UART4 发 0xFB + coord_y，等 0.5 秒，UART5 发 FD 88 88 FD
bool uart_retrieve(int coord_y);

}
