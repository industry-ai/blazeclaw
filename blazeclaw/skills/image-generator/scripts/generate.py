#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Image Generator CLI — 教育插图生成命令行工具

用法：
    python generate.py --prompt "词语" --output /tmp/output.png
    python generate.py --prompt "医生" --model card_image_generation --size 1024*1024
    python generate.py --prompt "桌子" --no-remove-bg
"""

import argparse
import json
import time
import os
import sys
import logging
import requests

import dashscope
from dashscope.aigc.image_generation import ImageGeneration
from dashscope.api_entities.dashscope_response import Message

# 加载配置
from config_loader import get_config
from content_review import review_image

config = get_config()
dashscope.api_key = config.get_api_key()

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_FOLDER = os.path.join(SCRIPT_DIR, '..', config.get_path("output_folder"))
OUTPUT_FOLDER = os.path.abspath(OUTPUT_FOLDER)
os.makedirs(OUTPUT_FOLDER, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)


def generate_unique_filename(base_name, extension):
    """生成唯一的文件名"""
    timestamp = time.strftime("%Y%m%d_%H%M%S")
    filename = f"{base_name}_{timestamp}.{extension}"
    if not os.path.exists(os.path.join(OUTPUT_FOLDER, filename)):
        return filename
    counter = 1
    while os.path.exists(os.path.join(OUTPUT_FOLDER, f"{base_name}_{timestamp}_{counter}.{extension}")):
        counter += 1
    return f"{base_name}_{timestamp}_{counter}.{extension}"


def _load_prompt_rules():
    """从 references/prompt-rules.md 读取提示词规则"""
    rules_path = os.path.join(SCRIPT_DIR, '..', 'references', 'prompt-rules.md')
    try:
        with open(rules_path, 'r', encoding='utf-8') as f:
            return f.read()
    except FileNotFoundError:
        logger.warning(f"[提示词规则] 文件不存在: {rules_path}，使用内置默认规则")
        return (
            "1. 只描述主体本身，不添加场景、背景、环境或阴影\n"
            "2. 主体必须居中，避免在画面边缘或角落生成任何小元素、装饰、碎片或噪点\n"
            "3. 主体本身避免使用纯白色，如果主体有白色部分，使用米白或浅灰代替，纯白仅用于背景\n"
            "4. 最终提示词结尾附加：, pure white background, no gradient, no shadow, "
            "clear dark outline around subject, high detail and recognizability, "
            "subject itself should not be pure white use off-white or light gray instead, "
            "no corner artifacts, no edge decorations, subject centered\n"
        )

        
def resolve_prompt(word):
    """调用 LLM 生成完整英文提示词"""
    rules = _load_prompt_rules()
    prompt_text = (
        f'请为词语"{word}"生成完整的 AI 图片提示词（英文）。\n\n'
        f'以下是提示词生成规则：\n{rules}\n\n'
        f'请根据以上规则，直接输出英文提示词，不要解释，不要换行。'
    )

    max_retries = 2
    for attempt in range(max_retries + 1):
        try:
            response = dashscope.Generation.call(
                model='qwen-turbo',
                prompt=prompt_text,
                result_format='message',
                max_tokens=500,
                temperature=0.05,
                timeout=30
            )
            if response.status_code == 200:
                text = response.output.choices[0].message.content.strip()
                logger.info(f"[提示词生成] 输入：{word}，LLM返回：{text}")
                return text
            else:
                logger.warning(f"[提示词生成] LLM调用失败 (尝试 {attempt+1}/{max_retries+1}): {response.code} - {response.message}")
        except Exception as e:
            logger.warning(f"[提示词生成] LLM调用异常 (尝试 {attempt+1}/{max_retries+1}): {e}")

        if attempt < max_retries:
            wait_time = 2 ** attempt
            logger.info(f"[提示词生成] 等待 {wait_time} 秒后重试...")
            time.sleep(wait_time)

    logger.warning(f"[提示词生成] LLM 调用失败，使用原始词语")
    return word


def remove_background(image_path, threshold=240):
    """将纯白/近白背景像素替换为透明"""
    try:
        from PIL import Image
        import numpy as np

        logger.info(f"[去底] 正在处理: {image_path}")
        img = Image.open(image_path).convert('RGBA')
        arr = np.array(img)

        # 将 RGB 三个通道都 >= threshold 的像素视为背景
        white_mask = (arr[:, :, 0] >= threshold) & (arr[:, :, 1] >= threshold) & (arr[:, :, 2] >= threshold)
        arr[white_mask, 3] = 0

        output = Image.fromarray(arr)

        # 裁剪四周全透明的行/列
        bbox = output.getbbox()
        if bbox:
            output = output.crop(bbox)

        output.save(image_path, 'PNG')
        logger.info(f"[去底] 完成: {image_path}")
        return True
    except Exception as e:
        logger.warning(f"[去底] 失败: {e}")
        return False


def generate_image(prompt, model=None, size=None, auto_remove_bg=True):
    """使用 DashScope wan2.7 API 生成图片"""
    model = model or config.get_model('image_generation') or 'wan2.7-image-pro'
    size = size or config.get_param('image_size')
    size_param = size.replace('*', '*')

    # 追加画图规则
    if auto_remove_bg and 'pure white background' not in prompt:
        style_suffix = ', pure white background, no gradient, no shadow, clear dark outline around subject, high detail and recognizability, subject itself should not be pure white use off-white or light gray instead, no corner artifacts, no edge decorations, subject centered'
        prompt = prompt + style_suffix

    logger.info(f"[文生图] 提示词: {prompt}")
    logger.info(f"[文生图] 模型: {model}, 尺寸: {size_param}")

    message = Message(role="user", content=[{"text": prompt}])

    try:
        rsp = ImageGeneration.call(
            model=model,
            api_key=dashscope.api_key,
            messages=[message],
            watermark=False,
            n=1,
            size=size_param,
            timeout=120,
            thinking_mode=False,
            prompt_extend=False,
        )
    except (requests.exceptions.ReadTimeout, requests.exceptions.Timeout, TimeoutError) as e:
        logger.error(f"[wan2.7] 请求超时: {e}")
        return {"error": f"模型请求超时: {e}"}
    except Exception as e:
        logger.error(f"[wan2.7] 请求异常: {e}")
        return {"error": f"模型请求异常: {e}"}

    if rsp.status_code == 200:
        for choice in rsp.output.choices:
            for content in choice["message"]["content"]:
                if content.get("type") == "image":
                    image_url = content["image"]
                    logger.info(f"生成成功！图片链接: {image_url}")

                    # 本地固定文件名，每次覆盖
                    output_filename = "latest.png"
                    output_path = os.path.join(OUTPUT_FOLDER, output_filename)
                    # COS 上传用唯一 key
                    cos_key = generate_unique_filename("generate", 'png')

                    resp = requests.get(image_url, timeout=30)
                    resp.raise_for_status()
                    with open(output_path, 'wb') as f:
                        f.write(resp.content)

                    # 自动去底
                    if auto_remove_bg:
                        remove_background(output_path)

                    return {
                        "success": True,
                        "image_url": image_url,
                        "local_path": output_path,
                        "cos_key": cos_key
                    }
        return {"error": "未获取到生成的图片"}
    elif "inappropriate content" in rsp.message.lower():
        logger.warning(f"内容审核未通过，停止生成")
        return {"error": "内容审核未通过"}
    else:
        return {"error": f"任务失败: {rsp.message}"}


def main():
    parser = argparse.ArgumentParser(description='教育插图生成 CLI')
    parser.add_argument('--prompt', '-p', required=True, help='输入词语或完整英文提示词')
    parser.add_argument('--output', '-o', default=None, help='输出文件路径（默认保存到 outputs/ 目录）')
    parser.add_argument('--model', '-m', default=None, help='指定模型（默认使用 image_generation 配置）')
    parser.add_argument('--size', '-s', default=None, help='图片尺寸（默认 1024*1024）')
    parser.add_argument('--no-remove-bg', action='store_true', help='禁用自动去底')

    args = parser.parse_args()

    logger.info(f"[image-generator] 开始处理: prompt={args.prompt}")

    # 判断输入是完整提示词还是简单词语
    # 如果包含逗号或多个单词，当作完整提示词；否则调用 LLM 生成
    is_full_prompt = ',' in args.prompt or len(args.prompt.split()) > 3

    if is_full_prompt:
        prompt = args.prompt
        logger.info(f"[image-generator] 使用完整提示词: {prompt}")
    else:
        logger.info(f"[image-generator] 调用 LLM 生成提示词...")
        prompt = resolve_prompt(args.prompt)
        logger.info(f"[image-generator] LLM 生成提示词: {prompt}")

    # 生成图片
    result = generate_image(
        prompt=prompt,
        model=args.model,
        size=args.size,
        auto_remove_bg=not args.no_remove_bg
    )

    if 'error' in result:
        print(json.dumps({"success": False, "error": result["error"]}, ensure_ascii=False))
        sys.exit(1)

    # AI 内容审查
    image_url = result.get('image_url', '')
    if image_url:
        review = review_image(image_url, prompt, args.prompt)
        if not review.get('passed', True):
            # 审查不通过，删除已下载的图片
            local_path = result.get('local_path', '')
            if local_path and os.path.exists(local_path):
                os.remove(local_path)
                logger.info(f"[image-generator] 审查未通过，已删除图片: {local_path}")
            print(json.dumps({
                "success": False,
                "error": f"AI 内容审查未通过: {review.get('reason', '未说明原因')}",
                "review_details": review.get('details', {})
            }, ensure_ascii=False))
            sys.exit(1)

    local_path = result.get('local_path', '')

    # 如果指定了 --output，额外复制一份到目标路径（不影响 local_path）
    if args.output and local_path:
        import shutil
        output_dir = os.path.dirname(os.path.abspath(args.output))
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)
        shutil.copy2(local_path, args.output)
        logger.info(f"[image-generator] 已复制到: {args.output}")

    cos_key = result.get('cos_key', '')

    print(json.dumps({
        "success": True,
        "prompt": prompt,
        "local_path": local_path,
        "cos_key": cos_key,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
