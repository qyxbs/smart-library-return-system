#!/usr/bin/env python3
"""LLM — Qwen3-0.6B v2 微调模型 via llama-server HTTP API

训练格式: Qwen3 chat template + "语音：{noise}" → "书名|分类"
推理走 /chat/completions，llama-server 自动应用 chat template。
cache_prompt: true 缓存 system prompt，后续请求快 2.5×。
"""

import re, json, urllib.request, urllib.error

LLM_URL = "http://127.0.0.1:18080/chat/completions"
TIMEOUT = 60

CATEGORIES = ["教辅", "文学", "科技", "其他"]

SYSTEM_PROMPT = (
    "你是书籍提取助手。从语音文本提取书名和分类，输出格式：书名|分类。"
    "分类只有4种："
    "- 教辅：教材/考试/真题/辅导/启蒙/绘本/工具书"
    "- 文学：小说/诗歌/散文/科幻/漫画/轻小说/传记"
    "- 科技：编程/科普/医学/工程/自然科学"
    "- 其他：历史/哲学/经济/艺术/生活/心理/励志等"
    "忽略口语中的干扰词。只输出一行结果，不要解释。"
)


def extract_book_info(asr_text: str, timeout: int = TIMEOUT) -> tuple:
    """通过 llama-server /chat/completions 提取书名+分类

    Returns: (title, category) 或 (None, None)
    """
    body = json.dumps({
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": f"语音：{asr_text}"},
        ],
        "max_tokens": 20,
        "temperature": 0.1,
        "cache_prompt": True,
    }).encode()

    try:
        req = urllib.request.Request(LLM_URL, data=body,
            headers={"Content-Type": "application/json"})
        resp = urllib.request.urlopen(req, timeout=timeout)
        data = json.loads(resp.read())
        content = data["choices"][0]["message"]["content"].strip()

        # 去掉可能的 think 标签
        content = re.sub(r'<think>.*?</think>\s*', '',
                         content, flags=re.DOTALL).strip()

        print(f"[LLM] in={asr_text!r} out={content!r}", flush=True)

        if not content or "|" not in content:
            return None, None

        parts = content.split("|", 1)
        title = parts[0].strip()
        cat = parts[1].strip() if len(parts) > 1 else ""

        # 过滤无效输出
        if title.lower() in ("书名", "书籍", "无法识别", "未知", ""):
            return None, None

        # 匹配分类
        for c in CATEGORIES:
            if c in cat:
                return title, c

        return title, CATEGORIES[-1]  # 无匹配 → 其他

    except urllib.error.HTTPError as e:
        print(f"[LLM] HTTP error {e.code}", flush=True)
        return None, None
    except urllib.error.URLError as e:
        print(f"[LLM] Connection error: {e.reason}", flush=True)
        return None, None
    except Exception as e:
        print(f"[LLM] Error: {e}", flush=True)
        return None, None
