#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
内容审查模块 — 使用多模态大模型审查生成的图片内容

审查维度：
1. 内容安全：是否包含违规、敏感、NSFW 等内容
2. 主题一致性：生成内容是否与输入的动物/主题匹配
3. 画面质量：清晰度、是否有明显变形、模糊、水印等质量问题
"""

import json
import logging
import dashscope
from dashscope import MultiModalConversation

logger = logging.getLogger(__name__)


def review_image(image_url, original_prompt, user_input):
    """
    审查生成的图片内容
    
    Args:
        image_url: 图片文件的 URL
        original_prompt: 生成图片使用的英文提示词
        user_input: 用户原始输入的词语
        
    Returns:
        dict: {
            "passed": bool,      # 是否通过审查
            "reason": str,       # 不通过的原因（通过时为空）
            "details": dict      # 各维度详细评估
        }
    """
    prompt = f"""请审查这个 AI 生成的图片，从以下三个维度进行评估：

## 审查维度

1. **内容安全**
   - 是否包含暴力、色情、恐怖、政治敏感等违规内容
   - 是否符合中国互联网内容规范

2. **主题一致性**
   - 图片内容是否与用户要求"{user_input}"匹配
   - 画面主体是否是用户期望的物体
   - 是否有明显偏离主题的内容

3. **画面质量**
   - 画面是否清晰，有无严重模糊
   - 物体是否有明显变形、扭曲、残缺
   - 是否有大面积水印、噪点、伪影
   - 构图是否合理，主体是否突出

## 输出格式

请以 JSON 格式输出审查结果：
```json
{{
    "content_safety": {{
        "passed": true/false,
        "reason": "具体原因（如有问题）"
    }},
    "topic_consistency": {{
        "passed": true/false,
        "reason": "具体原因（如有问题）"
    }},
    "visual_quality": {{
        "passed": true/false,
        "reason": "具体原因（如有问题）"
    }},
    "overall_passed": true/false,
    "summary": "总体评价"
}}
```

注意：
- 如果任一维度不通过，overall_passed 应为 false
- 只有严重问题才判定为不通过，轻微瑕疵可以通过
- 保持客观公正的评估标准"""

    messages = [
        {
            "role": "user",
            "content": [
                {"image": image_url},
                {"text": prompt}
            ]
        }
    ]

    try:
        logger.info("[内容审查] 开始审查图片...")
        response = MultiModalConversation.call(
            model='qwen-vl-max',
            messages=messages,
            result_format='message',
            max_tokens=1000,
            temperature=0.1
        )

        if response.status_code != 200:
            logger.warning(f"[内容审查] API 调用失败: {response.code} - {response.message}")
            return {
                "passed": True,  # 审查失败时默认通过，避免阻断正常流程
                "reason": "",
                "details": {"error": f"审查 API 调用失败: {response.message}"}
            }

        content = response.output.choices[0].message.content
        
        # 多模态模型返回 list 格式
        if isinstance(content, list):
            text_parts = []
            for item in content:
                if isinstance(item, dict):
                    text_parts.append(item.get('text', ''))
                else:
                    text_parts.append(str(item))
            content = ' '.join(text_parts)
        
        content = content.strip()
        
        # 提取 JSON 部分
        if "```json" in content:
            content = content.split("```json")[1].split("```")[0].strip()
        elif "```" in content:
            content = content.split("```")[1].split("```")[0].strip()

        review_result = json.loads(content)
        
        passed = review_result.get("overall_passed", True)
        reason = review_result.get("summary", "") if not passed else ""
        
        logger.info(f"[内容审查] 审查完成，通过: {passed}")
        if not passed:
            logger.warning(f"[内容审查] 不通过原因: {reason}")
        
        return {
            "passed": passed,
            "reason": reason,
            "details": review_result
        }

    except json.JSONDecodeError as e:
        logger.warning(f"[内容审查] 解析审查结果失败: {e}")
        return {
            "passed": True,
            "reason": "",
            "details": {"error": "审查结果解析失败"}
        }
    except Exception as e:
        logger.warning(f"[内容审查] 审查过程异常: {e}")
        return {
            "passed": True,
            "reason": "",
            "details": {"error": f"审查异常: {e}"}
        }
