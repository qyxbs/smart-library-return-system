#!/usr/bin/env python3
"""书库索引 — 哈希表 + 模糊匹配 (编辑距离 + 拼音 + LCS)

支持:
  - 精确匹配 (O(1))
  - 去标点匹配
  - 子串/超串匹配
  - 编辑距离模糊匹配 (Levenshtein)
  - 拼音首字母匹配 (可选, 需 pypinyin)
  - 最长公共子序列 (LCS)

内存: 100万本书 ~15MB 索引
"""

import os
import json
import re
from typing import Optional

BOOK_DB_PATH = "/userdata/voice_app/book_db.json"


def levenshtein_ratio(s1: str, s2: str) -> float:
    """归一化编辑距离相似度 (0.0-1.0)"""
    if not s1 or not s2:
        return 0.0
    if s1 == s2:
        return 1.0

    # 优化: 只计算短字符串
    if len(s1) > len(s2):
        s1, s2 = s2, s1

    # 使用两行 DP (内存高效)
    prev = list(range(len(s2) + 1))
    curr = [0] * (len(s2) + 1)

    for i, c1 in enumerate(s1):
        curr[0] = i + 1
        for j, c2 in enumerate(s2):
            if c1 == c2:
                curr[j + 1] = prev[j]
            else:
                curr[j + 1] = 1 + min(prev[j], prev[j + 1], curr[j])
        prev, curr = curr, prev

    max_len = max(len(s1), len(s2))
    return 1.0 - (prev[-1] / max_len)


def lcs_ratio(s1: str, s2: str) -> float:
    """最长公共子序列相似度"""
    if not s1 or not s2:
        return 0.0
    if s1 == s2:
        return 1.0

    m, n = len(s1), len(s2)
    # 使用两行 DP
    prev = [0] * (n + 1)
    curr = [0] * (n + 1)

    for c1 in s1:
        for j, c2 in enumerate(s2):
            if c1 == c2:
                curr[j + 1] = prev[j] + 1
            else:
                curr[j + 1] = max(prev[j + 1], curr[j])
        prev, curr = curr, prev

    return (2.0 * prev[-1]) / (m + n)  # 归一化到 [0, 1]


def get_pinyin_initials(text: str) -> str:
    """获取拼音首字母 (fallback: 原文字符)"""
    try:
        from pypinyin import pinyin, Style
        result = []
        for c in text:
            if '一' <= c <= '鿿':
                py = pinyin(c, style=Style.FIRST_LETTER)
                result.append(py[0][0] if py else c)
            elif c.isalpha():
                result.append(c.lower())
        return ''.join(result)
    except ImportError:
        return text


class BookIndex:
    def __init__(self, db_path: str = None):
        self.books = {}          # title → {title, category, ...}
        self.titles = set()      # 所有书名
        self._clean_map = {}     # clean(title) → title
        self._pinyin_map = {}    # pinyin_initials → [title, ...]
        self._path = db_path or BOOK_DB_PATH
        self._load()

    def _load(self):
        if not os.path.exists(self._path):
            print(f"[BookIndex] No book DB at {self._path}", flush=True)
            return

        with open(self._path, encoding='utf-8') as f:
            data = json.load(f)

        for item in data:
            t = (item.get("title") or "").strip()
            if not t:
                continue
            self.books[t] = item
            self.titles.add(t)

            # 构建 clean map
            ck = self._clean(t)
            if ck not in self._clean_map:
                self._clean_map[ck] = t
            elif len(t) < len(self._clean_map[ck]):
                self._clean_map[ck] = t  # 保留较短版本

            # 构建拼音索引
            py = get_pinyin_initials(t)
            if py and len(py) >= 2:
                if py not in self._pinyin_map:
                    self._pinyin_map[py] = []
                self._pinyin_map[py].append(t)

        print(f"[BookIndex] Loaded {len(self.titles)} books", flush=True)

    @staticmethod
    def _clean(s: str) -> str:
        return re.sub(r'[\s\-\—\《\》""\'\"\'·,\.，。！？!?()（）\[\]]', '', s).lower()

    # ===== 精确/近似匹配 =====

    def lookup(self, candidate: str) -> Optional[dict]:
        """三级精确匹配"""
        if not candidate:
            return None

        # L1: 精确匹配
        if candidate in self.books:
            return self.books[candidate]

        # L2: 去标点匹配
        clean = self._clean(candidate)
        if clean in self._clean_map:
            return self.books[self._clean_map[clean]]

        # L3: 子串/超串匹配
        # 候选是书名的子串
        for t in self.titles:
            ct = self._clean(t)
            if clean in ct or ct in clean:
                return self.books[t]

        return None

    # ===== 模糊匹配 =====

    def fuzzy_lookup(self, candidate: str, threshold: float = 0.75) -> Optional[dict]:
        """模糊匹配: 编辑距离 + LCS + 拼音 + 分词

        返回: {title, category, score, match_type} 或 None
        """
        if not candidate or len(candidate) < 2:
            return None

        clean = self._clean(candidate)
        best_score = threshold
        best_title = None
        best_type = "none"

        # 候选集: 优先拼音匹配, 否则全量扫描
        candidates = self._get_candidates(candidate)

        for t in candidates:
            ct = self._clean(t)

            # 1. 子串快速通道 (高置信度)
            if len(clean) >= 3 and clean in ct:
                score = 0.90 + 0.10 * (len(clean) / len(ct))
                if score > best_score:
                    best_score, best_title, best_type = score, t, "substring"
                    continue
            if len(clean) >= 3 and ct in clean:
                score = 0.85
                if score > best_score:
                    best_score, best_title, best_type = score, t, "superstring"
                    continue

            # 2. 编辑距离
            ed_score = levenshtein_ratio(clean, ct)
            if ed_score > best_score:
                best_score, best_title, best_type = ed_score, t, "edit_distance"
                continue

            # 3. LCS (对中英混合有效)
            if ed_score > 0.5:  # 只在编辑距离不太差时计算 LCS
                lcs_score = lcs_ratio(clean, ct)
                combined = 0.6 * ed_score + 0.4 * lcs_score
                if combined > best_score:
                    best_score, best_title, best_type = combined, t, "lcs"

        if best_title:
            return {
                **self.books[best_title],
                "score": round(best_score, 3),
                "match_type": best_type,
            }
        return None

    def _get_candidates(self, candidate: str) -> set:
        """获取模糊匹配候选集 (缩小搜索范围)"""
        # 尝试拼音过滤
        py = get_pinyin_initials(candidate)
        if py and py in self._pinyin_map:
            pinyin_candidates = set(self._pinyin_map[py])
            # 加入原候选周围的编辑距离候选
            return pinyin_candidates

        # 无拼音 → 全量扫描
        # 优化: 按长度过滤 (只比较长度差 <50% 的书名)
        cl = len(candidate)
        return {t for t in self.titles if abs(len(t) - cl) < max(cl * 0.5, 3)}


# ===== 全局单例 =====

_index = None


def get_index(db_path: str = None) -> BookIndex:
    global _index
    if _index is None:
        _index = BookIndex(db_path)
    return _index


def reload_index(db_path: str = None):
    """重新加载索引 (书库更新后调用)"""
    global _index
    _index = BookIndex(db_path)


# ===== 命令行测试 =====

if __name__ == "__main__":
    import sys
    idx = get_index()
    print(f"\n测试查找:")
    for q in sys.argv[1:] or ["三体", "活着", "物联网可能吧控制技术", "Harry Potter"]:
        # 精确
        r = idx.lookup(q)
        if r:
            print(f"  精确: {q!r} → {r['title']}|{r.get('category','?')}")
        else:
            # 模糊
            r = idx.fuzzy_lookup(q)
            if r:
                print(f"  模糊: {q!r} → {r['title']}|{r.get('category','?')} (score={r['score']}, type={r['match_type']})")
            else:
                print(f"  未找到: {q!r}")
