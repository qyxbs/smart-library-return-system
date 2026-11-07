// printer.h
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

bool init_printer();

bool printer_calibrate();

std::string utf8_to_gbk(const std::string& utf8);

}
