// =============================================================================
// test_printer.cpp —— 打印机模块测试
// =============================================================================
// 【测什么】
//   1. CRC-4 算法 —— 和参考值逐条对比
//      （这是最关键的一项：算错了二维码校验就过不了，K230 会拒读）
//   2. 二维码内容格式
//   3. 打印指令序列的关键字节
//   4. UTF-8 → GBK 转换
//
// 【参考值从哪来】
//   参考值：
//     BOOK:1                    → 000A
//     BOOK:7484681093755318273  → 000A
//     BOOK:5891198275           → 000B
//     HELLO                     → 000C
//   和 CLAUDE.md 里记录的联调测试向量一致。
// =============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "core/printer.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const std::string& msg) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s\n", msg.c_str());
    }
}

// 把 CRC4 结果格式化成 4 位十六进制
static std::string crc_hex(const std::string& s) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04X", core::crc4(s));
    return std::string(buf);
}

int main() {
    std::printf("=== 打印机模块测试 ===\n");

    // =========================================================================
    // 1. CRC-4 与参考值对比
    // =========================================================================
    {
        check(crc_hex("BOOK:1") == "000A", "CRC4(BOOK:1) = 000A");
        check(crc_hex("BOOK:7484681093755318273") == "000A",
              "CRC4(BOOK:7484681093755318273) = 000A");
        check(crc_hex("BOOK:5891198275") == "000B", "CRC4(BOOK:5891198275) = 000B");
        check(crc_hex("HELLO") == "000C", "CRC4(HELLO) = 000C");

        // 空字符串的 CRC 应为 0
        check(core::crc4("") == 0, "CRC4(空) = 0");

        // 结果必须始终在 0-15 范围内
        bool in_range = true;
        for (int i = 0; i < 1000; ++i) {
            std::string s = "BOOK:" + std::to_string(i);
            if (core::crc4(s) > 15) in_range = false;
        }
        check(in_range, "CRC4 结果始终在 4 位范围内（0-15）");
    }

    // =========================================================================
    // 2. 二维码内容格式
    // =========================================================================
    {
        std::string qr = core::make_qr_text("5891198275");
        check(qr == "BOOK:5891198275:000B", "QR 内容格式正确: " + qr);

        std::string qr2 = core::make_qr_text("1");
        check(qr2 == "BOOK:1:000A", "短书号的 QR 内容: " + qr2);
    }

    // =========================================================================
    // 3. 打印指令序列
    // =========================================================================
    {
        auto cmd = core::build_label("5891198275", "三体");

        // 3.1 开头是初始化指令 1B 40
        check(cmd.size() > 10, "指令序列非空");
        check(cmd[0] == 0x1B && cmd[1] == 0x40, "以初始化指令 1B 40 开头");

        // 3.2 包含标签开始 1A 5B 01
        bool has_label_start = false;
        for (size_t i = 0; i + 2 < cmd.size(); ++i) {
            if (cmd[i] == 0x1A && cmd[i+1] == 0x5B && cmd[i+2] == 0x01) {
                has_label_start = true;
                break;
            }
        }
        check(has_label_start, "包含标签开始指令 1A 5B 01");

        // 3.3 包含二维码指令 1A 31 00
        bool has_qr = false;
        for (size_t i = 0; i + 2 < cmd.size(); ++i) {
            if (cmd[i] == 0x1A && cmd[i+1] == 0x31 && cmd[i+2] == 0x00) {
                has_qr = true;
                break;
            }
        }
        check(has_qr, "包含二维码指令 1A 31 00");

        // 3.4 包含标签结束 + 打印
        bool has_end = false, has_print = false;
        for (size_t i = 0; i + 2 < cmd.size(); ++i) {
            if (cmd[i] == 0x1A && cmd[i+1] == 0x5D && cmd[i+2] == 0x00) has_end = true;
            if (cmd[i] == 0x1A && cmd[i+1] == 0x4F && cmd[i+2] == 0x00) has_print = true;
        }
        check(has_end, "包含标签结束指令 1A 5D 00");
        check(has_print, "包含打印指令 1A 4F 00");

        // 3.5 结尾是走纸偏移清零 1B 64 00
        check(cmd.size() >= 3 &&
              cmd[cmd.size()-3] == 0x1B &&
              cmd[cmd.size()-2] == 0x64 &&
              cmd[cmd.size()-1] == 0x00,
              "以走纸偏移清零 1B 64 00 结尾");

        // 3.6 二维码内容确实在里面
        std::string qr_expected = "BOOK:5891198275:000B";
        std::string cmd_str(cmd.begin(), cmd.end());
        check(cmd_str.find(qr_expected) != std::string::npos,
              "指令里包含完整 QR 内容");

        // 3.7 标签尺寸 400×400 dots
        //     在 "1A 5B 01" 之后第 5-6 字节是宽（小端 400 = 90 01）
        for (size_t i = 0; i + 8 < cmd.size(); ++i) {
            if (cmd[i] == 0x1A && cmd[i+1] == 0x5B && cmd[i+2] == 0x01) {
                // X(2) Y(2) 之后的 W(2)
                uint16_t w = static_cast<uint16_t>(cmd[i+7] | (cmd[i+8] << 8));
                check(w == 400, "标签宽度 = 400 dots (50mm)");
                break;
            }
        }
    }

    // =========================================================================
    // 4. UTF-8 → GBK 转换
    // =========================================================================
    {
        // "三体" 的 UTF-8 是 6 字节，GBK 是 4 字节
        std::string gbk = core::utf8_to_gbk("三体");
        check(gbk.size() == 4, "「三体」转 GBK 后是 4 字节（原 UTF-8 是 6 字节）");

        // GBK 里每个汉字的首字节 >= 0x81
        if (gbk.size() == 4) {
            check(static_cast<unsigned char>(gbk[0]) >= 0x81, "GBK 首字节是双字节字符");
            check(static_cast<unsigned char>(gbk[2]) >= 0x81, "第二个字也是双字节");
        }

        // ASCII 内容转换后不变
        check(core::utf8_to_gbk("ABC") == "ABC", "纯 ASCII 转换后不变");

        // 空字符串
        check(core::utf8_to_gbk("").empty(), "空字符串转换结果为空");
    }

    // =========================================================================
    // 5. 长书名不应崩
    // =========================================================================
    {
        std::string long_title(100, 'A');
        auto cmd = core::build_label("1", long_title);
        // 固定开销约 79 字节（初始化 + 标签开始 + QR + 结束），加上 100 字节书名
        check(cmd.size() > 150, "长书名能正常构建指令（约 179 字节）");

        // 书名内容确实在指令里
        std::string cmd_str(cmd.begin(), cmd.end());
        check(cmd_str.find(long_title) != std::string::npos, "长书名的内容被写进指令");
    }

    std::printf("\n========================================\n");
    std::printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    std::printf("========================================\n");

    return g_fail == 0 ? 0 : 1;
}
