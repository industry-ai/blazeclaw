#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
h5-cards/scripts/enrich.py
接收 match.py 的输出 + 用户原始意图 → 调用 DeepSeek API → 输出完整卡片 JSON

用法:
  echo '{"card_type":"english_word",...}' | python enrich.py "用户原始意图"
  MATCH=$(python match.py "学单词") && echo "$MATCH" | python enrich.py "学单词apple"

环境变量:
  DEEPSEEK_API_KEY   DeepSeek API Key（可选，fallback 到内置值）
"""

import sys
import json
import os
import re
import urllib.request
import urllib.error
from pathlib import Path

# ---------- 配置 ----------

DEEPSEEK_API_KEY = os.environ.get(
    "DEEPSEEK_API_KEY",
    "sk-686fef4df9c24c4abd637afedcac3c90"
)
DEEPSEEK_API_URL = "https://api.deepseek.com/v1/chat/completions"
DEFAULT_MODEL = "deepseek-v4-pro"
TIMEOUT_SEC = 20
MAX_RETRIES = 1

SCHEMA_PATH = Path(__file__).resolve().parent.parent / "references" / "card-schemas.md"

# card_type → card-schemas.md 段落标题映射
CARD_TYPE_TO_SECTION = {
    "h5_entry":             "text-card 系列",
    "assistant_welcome":    "text-card 系列",
    "recommendation":       "text-card 系列",
    "task":                 "text-card 系列",
    "health_advice":        "text-card 系列",
    "homework_reminder":    "homework-card（homework_reminder）",
    "media_preview":        "media-card（media_preview）",
    "english_word":         "english-word-card（english_word）",
    "english_sentence":     "english-sentence-card（english_sentence）",
    "english_sentence_input": "english-input-card（english_sentence_input）",
    "comic_strip":          "comic-card（comic_strip）",
    "qa_answer":            "answer-card（qa_answer）",
}


# ---------- Schema 加载 ----------

def load_schema_section(card_type: str) -> str:
    """从 card-schemas.md 提取指定 card_type 对应的 schema 段落。"""
    section_title = CARD_TYPE_TO_SECTION.get(card_type)
    if not section_title:
        return ""

    text = SCHEMA_PATH.read_text(encoding="utf-8")

    # 匹配对应的 ## 标题段落
    # 转义特殊字符用于正则
    escaped = re.escape(section_title)
    pattern = rf"^## {escaped}\s*$"
    m = re.search(pattern, text, re.MULTILINE)
    if not m:
        return ""

    start = m.start()
    # 找到下一个 ## 标题（同级或更高级）
    next_section = re.search(r"^## ", text[m.end():], re.MULTILINE)
    if next_section:
        end = m.end() + next_section.start()
    else:
        end = len(text)

    return text[start:end].strip()


def load_full_schema() -> str:
    """加载完整的 card-schemas.md 作为 fallback。"""
    if SCHEMA_PATH.is_file():
        return SCHEMA_PATH.read_text(encoding="utf-8")
    return ""


# ---------- LLM 调用 ----------

def build_prompt(card_type: str, match_result: dict, user_intent: str,
                 schema_section: str) -> dict:
    """构建 LLM 请求的 messages。"""

    system_prompt = (
        "你是一个 H5 卡片 JSON 生成器。你的任务是根据卡片类型、Schema 规则和用户意图，"
        "生成符合规范的完整卡片 JSON。\n\n"
        "规则：\n"
        "1. 严格遵循下方 Schema 规则中的 JSON 模板和构造规则\n"
        "2. 所有必填字段必须填写，缺失的选填字段不包含在输出中\n"
        "3. 根据用户意图智能填充 title、subtitle、description 等文案\n"
        "4. target_url 如果没有真实链接，使用占位 URL \"https://www.baidu.com\"\n"
        "5. 输出纯 JSON，不要包含 ```json 标记或任何解释文字\n"
        "6. JSON 顶层必须包含 schema_version: \"1.0\" 和 card_type 字段"
    )

    user_prompt = f"""Card Type: {card_type}

Match 结果（主题/图标/按钮）:
{json.dumps(match_result, ensure_ascii=False, indent=2)}

用户原始意图:
{user_intent}

Schema 规则:
{schema_section}

请根据以上信息，生成完整的卡片 JSON。只输出 JSON，别无其他："""

    return {
        "model": DEFAULT_MODEL,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ],
        "temperature": 0.3,
        "max_tokens": 2048,
    }


def call_deepseek(payload: dict) -> str | None:
    """调用 DeepSeek API，返回响应文本。"""
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        DEEPSEEK_API_URL,
        data=data,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {DEEPSEEK_API_KEY}",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT_SEC) as resp:
            body = json.loads(resp.read().decode("utf-8"))
            content = body.get("choices", [{}])[0].get("message", {}).get("content", "")
            return content.strip()
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace")
        print(f"错误：DeepSeek API 返回 HTTP {e.code}: {err_body[:200]}", file=sys.stderr)
        return None
    except urllib.error.URLError as e:
        print(f"错误：网络请求失败: {e.reason}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"错误：API 调用异常: {e}", file=sys.stderr)
        return None


# ---------- JSON 提取与校验 ----------

def extract_json(text: str) -> str | None:
    """从 LLM 输出中提取 JSON。支持 ```json 代码块包裹的情况。"""
    text = text.strip()
    # 尝试匹配 ```json ... ``` 包裹
    m = re.search(r"```(?:json)?\s*\n?(.*?)\n?```", text, re.DOTALL)
    if m:
        return m.group(1).strip()
    # 尝试匹配 { 到 } 的最外层
    if text.startswith("{"):
        # 找到匹配的闭合括号
        depth = 0
        for i, ch in enumerate(text):
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[:i + 1]
    return None


REQUIRED_FIELDS = ["schema_version", "card_type"]


def validate_card_json(card: dict, card_type: str) -> list[str]:
    """校验卡片 JSON 是否合法。返回错误列表（空列表表示通过）。"""
    errors = []
    if not isinstance(card, dict):
        errors.append("JSON 根元素不是对象")
        return errors

    for field in REQUIRED_FIELDS:
        if field not in card:
            errors.append(f"缺少必填字段: {field}")

    if card.get("card_type") != card_type:
        errors.append(
            f"card_type 不匹配: 期望 {card_type}, 实际 {card.get('card_type')}"
        )

    if card.get("schema_version") != "1.0":
        errors.append(f"schema_version 不正确: {card.get('schema_version')}")

    return errors


# ---------- 主逻辑 ----------

def enrich(user_intent: str, match_result: dict) -> dict | None:
    """核心函数：调用 LLM 生成完整卡片 JSON。"""
    card_type = match_result.get("card_type", "")
    if not card_type:
        print("错误：match 结果中缺少 card_type", file=sys.stderr)
        return None

    # 加载对应 schema
    schema_section = load_schema_section(card_type)
    if not schema_section:
        # fallback：加载完整 schema
        print(f"警告：未找到 {card_type} 的 schema 段落，使用完整 schema", file=sys.stderr)
        schema_section = load_full_schema()
    if not schema_section:
        print("错误：无法加载 card-schemas.md", file=sys.stderr)
        return None

    payload = build_prompt(card_type, match_result, user_intent, schema_section)

    for attempt in range(MAX_RETRIES + 1):
        if attempt > 0:
            print(f"重试 {attempt}/{MAX_RETRIES}...", file=sys.stderr)

        raw = call_deepseek(payload)
        if raw is None:
            continue

        json_str = extract_json(raw)
        if json_str is None:
            print(f"错误：LLM 输出中未找到有效 JSON。原始输出: {raw[:200]}", file=sys.stderr)
            continue

        try:
            card = json.loads(json_str)
        except json.JSONDecodeError as e:
            print(f"错误：JSON 解析失败: {e}", file=sys.stderr)
            continue

        # 合并 match_result 中的字段（LLM 可能会覆盖，以 match 为准的字段保留）
        # 实际上以 LLM 输出为准，但确保 theme/icon/button_text 与 match 一致
        for key in ("theme", "icon", "button_text"):
            if key in match_result and key not in card:
                card[key] = match_result[key]

        # 确保 layout 字段存在
        if "layout" not in card:
            card["layout"] = {
                "variant": card_type,
                "icon": match_result.get("icon", ""),
            }
        elif "variant" not in card.get("layout", {}):
            card["layout"]["variant"] = card_type

        errors = validate_card_json(card, card_type)
        if errors:
            print(f"错误：校验失败: {'; '.join(errors)}", file=sys.stderr)
            if attempt < MAX_RETRIES:
                # 把校验错误反馈给 LLM 重试
                payload["messages"].append({
                    "role": "assistant",
                    "content": json.dumps(card, ensure_ascii=False),
                })
                payload["messages"].append({
                    "role": "user",
                    "content": f"上述 JSON 校验失败: {'; '.join(errors)}。请修正后重新输出完整 JSON。",
                })
            continue

        return card

    return None


def main():
    # 解析参数
    import argparse
    parser = argparse.ArgumentParser(
        prog="enrich",
        description="使用 LLM 将 match 结果完善为完整卡片 JSON",
    )
    parser.add_argument(
        "intent", nargs="*",
        help="用户原始意图文本（可选，也可从 stdin 读取）",
    )
    parser.add_argument(
        "--model", default=DEFAULT_MODEL,
        help=f"DeepSeek 模型名（默认: {DEFAULT_MODEL}）",
    )
    parser.add_argument(
        "--no-llm", action="store_true",
        help="跳过 LLM 调用，直接输出 match 结果（调试用）",
    )
    args = parser.parse_args()

    # 读取用户意图
    if args.intent:
        user_intent = " ".join(args.intent)
    else:
        # 从 stdin 第二行读取（如果存在）或者空
        user_intent = ""

    # 读取 match 结果（stdin）
    raw_stdin = sys.stdin.read().strip()
    if not raw_stdin:
        print("错误：stdin 无输入（需要 match.py 的输出）", file=sys.stderr)
        sys.exit(1)

    try:
        match_result = json.loads(raw_stdin)
    except json.JSONDecodeError as e:
        print(f"错误：stdin JSON 解析失败: {e}", file=sys.stderr)
        sys.exit(1)

    if match_result.get("error"):
        print(f"错误：match 失败: {match_result['error']}", file=sys.stderr)
        # 仍然输出 match 结果供调试
        print(json.dumps(match_result, ensure_ascii=False))
        sys.exit(1)

    if args.no_llm:
        print(json.dumps(match_result, ensure_ascii=False, indent=2))
        return

    # 更新全局 model
    global DEFAULT_MODEL
    DEFAULT_MODEL = args.model

    card = enrich(user_intent, match_result)
    if card is None:
        print("错误：无法生成完整卡片 JSON", file=sys.stderr)
        # fallback：输出 match 原始结果
        print(json.dumps(match_result, ensure_ascii=False))
        sys.exit(1)

    print(json.dumps(card, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
