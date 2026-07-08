#!/usr/bin/env python3
"""分类关键词规则兜底 — LLM 分类与强关键词冲突时覆盖

仅在 LLM 输出高置信度错误时介入，其余情况信任 LLM。
"""

# 教辅强制关键词 — 命中则覆盖为教辅
JIAOFU_PATTERNS = [
    "写作", "考试", "真题", "历年", "模拟题", "模拟卷",
    "语法", "词汇", "阅读理解", "完形填空", "听力",
    "教材", "课本", "辅导", "教辅", "练习册",
    "高考", "中考", "四级", "六级", "雅思", "托福", "考研",
    "启蒙", "绘本", "识字", "拼音", "描红",
    "一课一练", "同步", "课课练", "单元测试",
]


def correct_category(title: str, llm_category: str) -> str:
    """关键词规则覆盖 LLM 分类"""
    for kw in JIAOFU_PATTERNS:
        if kw in title:
            if llm_category != "教辅":
                print(f"[Rules] '{kw}' in '{title}' → 教辅 (was {llm_category})", flush=True)
            return "教辅"
    return llm_category
