#!/usr/bin/env python3
"""Serper API — 联网纠错+验证书名"""
import os
import requests
import re

SERPER_API_KEY = os.environ.get("SERPER_API_KEY", "")
SERPER_URL = "https://google.serper.dev/search"

CATEGORIES = ["文学小说", "少儿教育", "人文社科", "科技生活"]


def _title_similarity(a: str, b: str) -> float:
    """简单的字符重叠率，用于检查 Serper 结果与输入是否相关"""
    if not a or not b:
        return 0.0
    sa, sb = set(a), set(b)
    if not sa or not sb:
        return 0.0
    return len(sa & sb) / min(len(sa), len(sb))


def search_book(title: str, asr_text: str = "") -> dict | None:
    """联网搜索书名。

    Args:
        title: LLM 提取的书名
        asr_text: ASR 原始文本（可选，用于二次验证）
    """
    if not SERPER_API_KEY:
        return None

    def _do_search(query: str) -> dict | None:
        try:
            resp = requests.post(SERPER_URL,
                json={"q": f"{query} 书籍 豆瓣"},
                headers={"X-API-KEY": SERPER_API_KEY}, timeout=5)
            if resp.status_code != 200:
                return None

            data = resp.json()
            candidates = []

            for item in data.get("organic", [])[:5]:
                link = item.get("link", "")
                rt = item.get("title", "")
                snippet = item.get("snippet", "")

                if "book.douban.com" in link or "douban.com/subject" in link:
                    clean = re.sub(r'\s*[\(（].*?[\)）]\s*', '', rt)
                    if 1 < len(clean) < 30:
                        sim = _title_similarity(title, clean)
                        candidates.append((clean, sim, snippet, rt))

            if not candidates:
                return {"found": False}

            # 按相似度排序，取最相关的
            candidates.sort(key=lambda x: -x[1])
            best_title, best_sim, best_snippet, best_rt = candidates[0]

            # 相关性阈值：与输入书名的字符重叠率必须 >= 0.5
            if best_sim < 0.5:
                return {"found": False, "reason": f"Serper结果不相关 (sim={best_sim:.2f})"}

            return {"found": True, "title": best_title, "similarity": best_sim, "source": "serper"}

        except Exception as e:
            print(f"[Serper] Error: {e}", flush=True)
            return None

    # 先用 LLM 书名搜索
    result = _do_search(title)
    if result and result.get("found"):
        return result

    # 书名搜不到，用 ASR 原始文本再试一次
    if asr_text and asr_text != title and len(asr_text) >= 2:
        result2 = _do_search(asr_text)
        if result2 and result2.get("found"):
            return result2

    return result or {"found": False}
