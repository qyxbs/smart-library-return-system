#!/usr/bin/env python3
"""智能联动管线 — DeepSeek 提取书名+分类 → 书库纠错书名

架构原则:
  - DeepSeek v4-pro 负责分类+书名 — 语义理解比关键词映射可靠
  - 书库 414万 负责书名纠错 — 字符串匹配比 LLM 可靠
  - 书库未命中 → 返回 DeepSeek 原结果 + 未验证标记 (不再用 Serper)

流程:
  1. DeepSeek 输出 {书名, 分类}
  2. 书库精确匹配 → 纠正书名拼写 (分类信任 DeepSeek)
  3. 书库模糊匹配 → 编辑距离+LCS 纠正
  4. 都不匹配 → DeepSeek 原结果 + 低置信度标记
"""

import re
from typing import Optional
from book_index import get_index

TARGET_CATEGORIES = ["教辅", "文学", "科技", "其他"]


class PipelineResult:
    def __init__(self, title: str, category: str, source: str,
                 confidence: float, asr_text: str = "",
                 warning: str = "", title_corrected: bool = False):
        self.title = title
        self.category = category
        self.source = source       # "book_db" | "book_db_fuzzy" | "serper+book_db" | "serper" | "llm_only"
        self.confidence = confidence  # 书名置信度 (非分类置信度)
        self.asr_text = asr_text
        self.warning = warning
        self.title_corrected = title_corrected  # 书名是否被书库纠正

    def to_dict(self):
        return {
            "title": self.title,
            "category": self.category,
            "source": self.source,
            "confidence": self.confidence,
            "asr_text": self.asr_text,
            "warning": self.warning,
            "title_corrected": self.title_corrected,
        }


def validate_llm_output(title: str, category: str | None) -> tuple:
    """验证 LLM 输出的基本有效性"""
    if not title or title in ("书名", "书籍", "无法识别", "未知", ""):
        return None, None

    title = title.strip()
    title = re.sub(r'^[《「『]|[》」』]$', '', title)

    if len(title) < 1 or len(title) > 100:
        return None, None

    # 分类归一化
    if category and category in TARGET_CATEGORIES:
        cat = category
    else:
        cat = ""  # 空分类由后续逻辑处理

    return title, cat


def run_pipeline(asr_text: str, llm_output: tuple) -> PipelineResult:
    """执行智能联动管线

    Args:
        asr_text: ASR 原始识别文本
        llm_output: (title, category) from llm_engine.extract_book_info()

    Returns:
        PipelineResult — title 经过书库纠正, category 信任 Qwen3
    """
    raw_title, raw_category = llm_output
    title, category = validate_llm_output(raw_title, raw_category)

    if not title:
        return PipelineResult(
            title="", category="", source="none",
            confidence=0.0, asr_text=asr_text,
            warning="LLM 未能提取书名",
        )

    book_index = get_index()

    # ==== Step 1: 书库精确匹配 — 纠正书名拼写 ====
    book = book_index.lookup(title)
    if book:
        corrected_title = book["title"]
        return PipelineResult(
            title=corrected_title,
            category=category,          # 分类信任 Qwen3
            source="book_db",
            confidence=1.0,
            asr_text=asr_text,
            title_corrected=(corrected_title != title),
        )

    # ==== Step 2: 书库模糊匹配 — 编辑距离/LCS 纠正 ====
    fuzzy_result = book_index.fuzzy_lookup(title, threshold=0.65)
    if fuzzy_result:
        corrected_title = fuzzy_result["title"]
        return PipelineResult(
            title=corrected_title,
            category=category,          # 分类信任 Qwen3
            source="book_db_fuzzy",
            confidence=fuzzy_result.get("score", 0.8),
            asr_text=asr_text,
            title_corrected=(corrected_title != title),
        )

    # ==== Step 3: 无匹配 — DeepSeek 原结果 + 标记未验证 ====
    # 不再使用 Serper 联网兜底 (Serper 容易把正确书名匹配到错误的豆瓣结果)
    return PipelineResult(
        title=title,
        category=category,
        source="llm_only",
        confidence=0.6,
        asr_text=asr_text,
        warning="书名未在书库中找到，请确认书名是否正确",
    )


# ===== 集成到 voice_web_app =====

def process_voice_result(asr_text: str, llm_title: str, llm_category: str) -> dict:
    """供 voice_web_app.py 调用的便捷函数"""
    from category_rules import correct_category

    pip_result = run_pipeline(asr_text, (llm_title, llm_category))

    # 关键词规则兜底 — 覆盖 LLM 明显分类错误
    if pip_result.title:
        pip_result.category = correct_category(pip_result.title, pip_result.category)

    verified = pip_result.source in ("book_db", "book_db_fuzzy")

    return {
        "success": bool(pip_result.title),
        "title": pip_result.title,
        "category": pip_result.category,
        "verified": verified,
        "source": pip_result.source,
        "confidence": pip_result.confidence,
        "title_corrected": pip_result.title_corrected,
        "warning": pip_result.warning,
        "asr_text": pip_result.asr_text,
    }
