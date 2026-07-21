#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
h5-cards/scripts/match.py
根据用户意图文本匹配 card_type，输出 JSON。

用法:
  python match.py "学单词"
  echo "看漫画" | python match.py
"""

import sys
import json

RULES = [
    # (card_type, theme, icon, button_text, keywords)
    ("english_word",       "abc",      "abc",     "单词发音", ["学单词", "学英语", "单词卡片", "学习单词", "英语单词", "英语启蒙", "字母启蒙", "来个单词"]),
    ("english_sentence",   "sentence", "sentence","句子发音", ["每日一句", "英语句子", "学句子", "今日英语", "每日英语", "励志句子"]),
    ("english_sentence_input", "sentence", "sentence", "句子发音", ["翻译", "输入句子", "翻译句子", "翻译卡片", "输入翻译"]),
    ("comic_strip",        "comic",    "comic",   "开始阅读", ["看漫画", "漫画", "英语漫画", "漫画卡片", "连环画"]),
    ("homework_reminder",  None,       None,      "查看作业", ["作业", "布置作业", "家庭作业", "课后练习", "复习作业"]),
    ("media_preview",      "video",    None,      None,       ["播放视频", "看视频", "播放音频", "听音频", "媒体卡片", "视频卡片", "音频卡片"]),
    ("qa_answer",          None,       None,      None,       ["知识问答", "问答", "问答卡片", "回答问题", "提问"]),
    ("recommendation",     "recommendation", "audio", "查看详情", ["推荐", "推荐内容", "推荐卡片", "内容推荐"]),
    ("assistant_welcome",  "ai",       "ai",      "开始对话", ["欢迎", "AI助手", "欢迎卡片", "AI助理", "智能助手"]),
    ("task",               "task",     "task",    "查看任务", ["任务", "协作", "周报", "任务卡片", "待办", "通知"]),
    # health_advice 已迁移至 medical skill，此处保留规则定义供未来复用
    # ("health_advice",      "health",   "health",  "查看建议", ["健康", "健康建议", "用药提醒", "健康卡片", "饮食建议", "服药", "吃药", "药品", "药物", "药盒", "处方", "用药", "药品提醒"]),
    ("h5_entry",           "general",  "link",    "打开页面", ["H5入口", "入口卡片", "打开页面", "页面入口", "链接卡片"]),
]

# 科目子规则 (homework_reminder)
SUBJECT_RULES = [
    ("chinese", ["语文", "chinese", "阅读理解", "作文", "课文"]),
    ("math",    ["数学", "math", "分数", "计算", "代数"]),
    ("english", ["英语", "english", "单词", "语法"]),
]

# 媒体类型子规则 (media_preview)
MEDIA_RULES = [
    ("video", ["视频", "播放视频", "看视频", "录像"]),
    ("audio", ["音频", "播放音频", "听音频", "音乐", "录音"]),
    ("image", ["图片", "查看图片", "图像", "照片"]),
    ("file",  ["文件", "下载", "文档", "资料"]),
]


def match_card(text):
    """Match user intent text to card_type. Returns dict with card metadata."""
    text_lower = text.lower()

    for card_type, theme, icon, button, keywords in RULES:
        for kw in keywords:
            if kw in text or kw.lower() in text_lower:
                result = {
                    "card_type": card_type,
                    "theme": theme,
                    "icon": icon,
                    "button_text": button,
                }
                # 子规则: homework_reminder → 匹配科目
                if card_type == "homework_reminder":
                    for subj_theme, subj_kw in SUBJECT_RULES:
                        for skw in subj_kw:
                            if skw in text or skw.lower() in text_lower:
                                result["theme"] = subj_theme
                                result["icon"] = subj_theme
                                break
                        if result["theme"] != theme:
                            break
                    # 默认语文
                    if result["theme"] is None:
                        result["theme"] = "chinese"
                        result["icon"] = "chinese"

                # 子规则: media_preview → 匹配媒体类型
                if card_type == "media_preview":
                    for media_icon, media_kw in MEDIA_RULES:
                        for mkw in media_kw:
                            if mkw in text or mkw.lower() in text_lower:
                                result["icon"] = media_icon
                                break
                        if result["icon"] != icon:
                            break
                    # 默认视频
                    if result["icon"] is None:
                        result["icon"] = "video"
                    # 按钮文案
                    btn_map = {"video": "播放视频", "audio": "播放音频", "image": "查看图片", "file": "下载文件"}
                    result["button_text"] = btn_map.get(result["icon"], "播放视频")

                # 清理 None 字段
                return {k: v for k, v in result.items() if v is not None}

    return {"card_type": None, "error": "无法匹配任何卡片类型"}


def main():
    if len(sys.argv) > 1:
        text = " ".join(sys.argv[1:])
    else:
        text = sys.stdin.read().strip()

    if not text:
        result = {"card_type": None, "error": "输入为空"}
    else:
        result = match_card(text)

    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
