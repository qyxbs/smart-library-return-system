// printer.cpp —— EM20L 打印机实现

#include "core/printer.h"

#include <iconv.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "common/config.h"
#include "common/log.h"
#include "core/uart.h"

namespace core {

uint8_t crc4(const std::string& data) {
    uint8_t crc = 0x0;

    for (unsigned char byte : data) {
        crc ^= (byte & 0xF);

        for (int i = 0; i < 4; ++i) {
            if (crc & 0x8) {
                crc = static_cast<uint8_t>(((crc << 1) ^ 0x3) & 0xF);
            } else {
                crc = static_cast<uint8_t>((crc << 1) & 0xF);
            }
        }
    }
    return crc & 0xF;
}

std::string make_qr_text(const std::string& book_id) {
    std::string prefix = "BOOK:" + book_id;

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", crc4(prefix));
    return prefix + ":" + buf;
}

std::string utf8_to_gbk(const std::string& utf8) {
    if (utf8.empty()) {
        return std::string();
    }

    iconv_t cd = iconv_open("GBK", "UTF-8");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        LOG_ERROR("[打印] iconv_open 失败（系统不支持 GBK？）");
        return std::string();
    }

    std::string out;
    out.resize(utf8.size() * 2 + 16);

    char* in_ptr = const_cast<char*>(utf8.data());
    size_t in_left = utf8.size();

    char* out_ptr = &out[0];
    size_t out_left = out.size();

    size_t ret = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    if (ret == static_cast<size_t>(-1)) {
        LOG_WARN("[打印] GBK 转换失败: ", std::strerror(errno),
                 "（书名含 GBK 不支持的字符，用替代符）");
        iconv_close(cd);
        return std::string();
    }

    out.resize(out.size() - out_left);
    iconv_close(cd);
    return out;
}

namespace {

// 标签物理规格（50mm × 50mm @ 203 DPI）
//   1 dot = 0.125mm，所以 50mm = 400 dots
constexpr int LABEL_W = 400;
constexpr int LABEL_H = 400;

constexpr int QR_ECC    = 3;
constexpr int QR_MODULE = 8;    // 每个模块 8 dots（手册上限，再大不打印）
constexpr int QR_SIZE   = 200;  // 估算尺寸（25 模块 × 8 dots）
constexpr int QR_X      = LABEL_W - QR_SIZE - 20;
constexpr int QR_Y      = LABEL_H - QR_SIZE - 20;

constexpr int TITLE_X    = 200;
constexpr int TITLE_Y    = 40;
constexpr int TITLE_SIZE = 0x11;   // 1 倍字体（0x11/0x22/0x33… 是固定枚举，不能随便填）

void append_u16le(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFF));         // 低字节在前
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));  // 高字节在后
}

}

std::vector<uint8_t> build_label(const std::string& book_id,
                                 const std::string& title) {
    std::vector<uint8_t> cmd;
    cmd.reserve(256);

    cmd.push_back(0x1B);
    cmd.push_back(0x40);

    cmd.push_back(0x1A);
    cmd.push_back(0x5B);
    cmd.push_back(0x01);
    append_u16le(cmd, 0);
    append_u16le(cmd, 0);
    append_u16le(cmd, LABEL_W);
    append_u16le(cmd, LABEL_H);
    cmd.push_back(0x00);

    std::string qr_text = make_qr_text(book_id);

    cmd.push_back(0x1A);
    cmd.push_back(0x31);
    cmd.push_back(0x00);
    cmd.push_back(0x00);
    cmd.push_back(QR_ECC);
    append_u16le(cmd, QR_X);
    append_u16le(cmd, QR_Y);
    cmd.push_back(QR_MODULE);       // 模块大小 8 dots
    cmd.push_back(0x00);

    for (char c : qr_text) {
        cmd.push_back(static_cast<uint8_t>(c));
    }
    cmd.push_back(0x00);

    cmd.push_back(0x1B);
    cmd.push_back(0x61);
    cmd.push_back(0x01);

    std::string title_gbk = utf8_to_gbk(title);
    if (title_gbk.empty()) {
        title_gbk.clear();
        for (char c : title) {
            if (static_cast<unsigned char>(c) < 0x80) {
                title_gbk += c;
            }
        }
        if (title_gbk.empty()) {
            title_gbk = "?";
        }
    }

    cmd.push_back(0x1A);
    cmd.push_back(0x54);
    cmd.push_back(0x01);
    append_u16le(cmd, TITLE_X);
    append_u16le(cmd, TITLE_Y);
    cmd.push_back(0x18);
    cmd.push_back(0x00);
    cmd.push_back(0x00);
    cmd.push_back(TITLE_SIZE);

    for (unsigned char c : title_gbk) {
        cmd.push_back(c);
    }
    cmd.push_back(0x00);

    cmd.push_back(0x1A);
    cmd.push_back(0x5D);
    cmd.push_back(0x00);

    cmd.push_back(0x1A);
    cmd.push_back(0x4F);
    cmd.push_back(0x00);

    cmd.push_back(0x1B);
    cmd.push_back(0x64);
    cmd.push_back(0x00);

    return cmd;
}

namespace {
Uart g_uart9;
}

bool init_printer() {
    return g_uart9.open(cfg::UART9_DEV, cfg::UART9_BAUD);
}

bool print_label(const std::string& book_id, const std::string& title) {
    auto cmd = build_label(book_id, title);

    LOG_INFO("[打印] 标签: 书号=", book_id,
             " 书名=", title,
             " 指令长度=", cmd.size(), " 字节",
             " QR内容=", make_qr_text(book_id));

    return g_uart9.write_bytes(cmd.data(), cmd.size());
}

bool printer_calibrate() {
    const uint8_t cmd[2] = {0x1F, 0x63};
    LOG_INFO("[打印] 发送校准指令 1F 63");
    return g_uart9.write_bytes(cmd, 2);
}

}
