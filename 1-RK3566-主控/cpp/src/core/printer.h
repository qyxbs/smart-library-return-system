// =============================================================================
// =============================================================================
// 【这台打印机要干什么】
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {

uint8_t crc4(const std::string& data);

std::string make_qr_text(const std::string& book_id);

std::vector<uint8_t> build_label(const std::string& book_id,
                                 const std::string& title);

bool print_label(const std::string& book_id, const std::string& title);

// 初始化打印机串口（程序启动时调一次）
bool init_printer();

// 打印机校准：发一条校准指令（首次使用或换纸后跑一次）
bool printer_calibrate();

std::string utf8_to_gbk(const std::string& utf8);

}  // namespace core
