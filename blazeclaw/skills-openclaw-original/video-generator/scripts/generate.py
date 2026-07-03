#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Video Generator CLI — 短视频生成命令行工具

用法：
    python generate.py --prompt "熊猫吃竹子"
    python generate.py --prompt "a cat playing" --duration 5 --resolution 480P
"""

import argparse
import json
import time
import os
import sys
import random
import logging
import shutil
import subprocess
import requests
from http import HTTPStatus

import dashscope
from dashscope import VideoSynthesis

# 加载配置
from config_loader import get_config
from content_review import review_video

config = get_config()
dashscope.api_key = config.get_api_key()

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))  # skills/
OUTPUT_FOLDER = os.path.join(SCRIPT_DIR, '..', config.get_path("output_folder"))
OUTPUT_FOLDER = os.path.abspath(OUTPUT_FOLDER)
os.makedirs(OUTPUT_FOLDER, exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)


def _read_template_assets():
    """读取 media-card 模板的 CSS 和 JS，返回 (css, js) 或 (None, None)"""
    tpl_dir = os.path.join(ROOT_DIR, "h5-cards", "templates", "media-card")
    css_path = os.path.join(tpl_dir, "ai-card.css")
    js_path = os.path.join(tpl_dir, "ai-card.js")
    css = js = None
    if os.path.exists(css_path):
        with open(css_path, 'r', encoding='utf-8') as f:
            css = f.read()
    if os.path.exists(js_path):
        with open(js_path, 'r', encoding='utf-8') as f:
            js = f.read()
    return css, js


def _build_card_html(card_json):
    """基于 media-card 模板生成纯 HTML 页面（无 shadow DOM，内联 CSS，秒开）"""
    title = card_json.get('title', '卡片')
    video_url = card_json.get('target_url', card_json.get('video_url', ''))
    description = card_json.get('description', '')
    btn_text = card_json.get('button_text', '播放视频')
    css, _ = _read_template_assets()

    # 把模板 CSS 中的 :host 选择器替换为 .card（shadow DOM 选择器 → 普通 DOM）
    if css:
        css = css.replace(':host', '.card-host')

    video_html = f'<video controls preload="none" src="{video_url}"></video>' if video_url else ''

    return f'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>{title}</title>
<style>
*{{margin:0;padding:0;box-sizing:border-box}}
body{{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;background:#f0f2f5;padding:24px 16px;display:flex;justify-content:center}}
.card-host{{display:block;max-width:380px;width:100%;
  --c-brand:#4f46e5;--c-light:#eef2ff;--c-soft:#e0e7ff;--c-grad-start:#4f46e5;--c-grad-end:#818cf8}}
{css or ""}
</style>
</head>
<body>
<div class="card-host">
  <div class="card">
    <div class="media-area">{video_html}</div>
    <div class="card-body">
      <div class="hdr">
        <div class="icon">🎬</div>
        <div class="hinfo"><div class="title">{title}</div></div>
      </div>
      {"<div class=\"desc\">" + description + "</div>" if description else ""}
      <div class="actions"><button class="btn primary" id="playBtn">{btn_text}</button></div>
    </div>
  </div>
</div>
<script>
(function() {{
  var btn = document.getElementById('playBtn');
  var video = document.querySelector('video');
  if (btn && video) {{
    var playText = btn.textContent;
    btn.addEventListener('click', function() {{
      if (video.paused) {{
        video.play();
        btn.textContent = '暂停';
      }} else {{
        video.pause();
        btn.textContent = playText;
      }}
    }});
  }}
}})();
</script>
</body>
</html>'''


def deploy_card(card_json):
    """渲染卡片 HTML → 上传 COS → 返回 HTML URL"""
    import tempfile
    upload_script = os.path.join(ROOT_DIR, "upload-to-oss", "main.py")

    timestamp = time.strftime("%Y%m%d%H%M%S")
    tmp_dir = os.path.join(tempfile.gettempdir(), f"h5-cards-{timestamp}")
    os.makedirs(tmp_dir, exist_ok=True)

    try:
        # 1. 生成 HTML
        output_html = os.path.join(tmp_dir, "index.html")
        html_content = _build_card_html(card_json)
        with open(output_html, 'w', encoding='utf-8') as f:
            f.write(html_content)
        logger.info(f"[deploy_card] index.html 生成成功: {output_html}")

        # 2. 上传 index.html 到 COS
        cos_key = f"h5-cards/{timestamp}/index.html"
        try:
            upload_result = subprocess.run(
                [sys.executable, upload_script, "upload", output_html, cos_key],
                capture_output=True, text=True, timeout=60,
            )
            if upload_result.returncode == 0:
                # ponytail: upload-to-oss 混 logging 到 stdout，取最后一行作为 URL
                url = upload_result.stdout.strip().splitlines()[-1].strip()
                logger.info(f"上传: index.html → {url}")
                return url
            else:
                logger.warning(f"上传失败: index.html - {upload_result.stderr}")
                return None
        except Exception as e:
            logger.warning(f"上传异常: index.html - {e}")
            return None
    except Exception as e:
        logger.warning(f"deploy_card error: {e}")
        return None
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def upload_to_cos(local_path):
    """Upload file to COS with correct Content-Type, return URL or None."""
    try:
        from qcloud_cos import CosConfig, CosS3Client
    except ImportError:
        logger.warning("COS SDK not installed, falling back to upload-to-oss")
        return _upload_to_cos_subprocess(local_path)

    secret_id = os.environ.get("COS_SECRET_ID", "")
    secret_key = os.environ.get("COS_SECRET_KEY", "")
    region = os.environ.get("COS_REGION", "")
    bucket = os.environ.get("COS_BUCKET", "")

    if not all([secret_id, secret_key, region, bucket]):
        logger.warning("COS env vars not set")
        return _upload_to_cos_subprocess(local_path)

    filename = os.path.basename(local_path)
    cos_key = f"video-generator/{filename}"

    mime_map = {
        ".mp4": "video/mp4",
        ".mov": "video/quicktime",
        ".avi": "video/x-msvideo",
        ".webm": "video/webm",
    }
    ext = os.path.splitext(filename)[1].lower()
    content_type = mime_map.get(ext, "application/octet-stream")

    try:
        cfg = CosConfig(Region=region, SecretId=secret_id, SecretKey=secret_key, Scheme="https")
        client = CosS3Client(cfg)
        client.put_object_from_local_file(
            Bucket=bucket,
            LocalFilePath=local_path,
            Key=cos_key,
            ContentType=content_type,
            ContentDisposition="inline",
        )
        url = f"https://{bucket}.cos.{region}.myqcloud.com/{cos_key}"
        logger.info(f"上传: {filename} → {url} (Content-Type: {content_type})")
        return url
    except Exception as e:
        logger.warning(f"COS upload error: {e}")
        return _upload_to_cos_subprocess(local_path)


def _upload_to_cos_subprocess(local_path):
    """Fallback: upload via upload-to-oss subprocess."""
    upload_script = os.path.join(ROOT_DIR, "upload-to-oss", "main.py")
    filename = os.path.basename(local_path)
    cos_key = f"video-generator/{filename}"
    try:
        result = subprocess.run(
            [sys.executable, upload_script, "upload", local_path, cos_key],
            capture_output=True, text=True, timeout=120,
        )
        if result.returncode != 0:
            logger.warning(f"COS upload failed: {result.stderr}")
            return None
        url = result.stdout.strip().splitlines()[-1].strip()
        return url if url else None
    except Exception as e:
        logger.warning(f"COS upload error: {e}")
        return None


def deploy_error_card(title, error_msg):
    """部署错误信息卡片 → 返回 webview URL"""
    import tempfile
    upload_script = os.path.join(ROOT_DIR, "upload-to-oss", "main.py")
    timestamp = time.strftime("%Y%m%d%H%M%S")
    tmp_dir = os.path.join(tempfile.gettempdir(), f"h5-cards-{timestamp}")
    os.makedirs(tmp_dir, exist_ok=True)
    try:
        html_content = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{title}</title>
<style>
* {{ margin: 0; padding: 0; box-sizing: border-box; }}
body {{ background: #fff8f0; display: flex; justify-content: center; align-items: center; min-height: 100vh; font-family: -apple-system, sans-serif; }}
.card {{ background: white; border-radius: 16px; padding: 40px; max-width: 480px; width: 90%; box-shadow: 0 4px 24px rgba(0,0,0,0.08); text-align: center; }}
.icon {{ font-size: 48px; margin-bottom: 16px; }}
h2 {{ font-size: 18px; color: #333; margin-bottom: 8px; }}
p {{ font-size: 14px; color: #666; line-height: 1.6; }}
</style>
</head>
<body>
<div class="card">
<div class="icon">⚠️</div>
<h2>生成未通过</h2>
<p>{error_msg}</p>
</div>
</body>
</html>"""
        output_html = os.path.join(tmp_dir, "index.html")
        with open(output_html, 'w', encoding='utf-8') as f:
            f.write(html_content)
        cos_key = f"h5-cards/{timestamp}/index.html"
        upload_result = subprocess.run(
            [sys.executable, upload_script, "upload", output_html, cos_key],
            capture_output=True, text=True, timeout=60,
        )
        if upload_result.returncode == 0:
            url = upload_result.stdout.strip().splitlines()[-1].strip()
            logger.info(f"[error_card] 已部署: {url}")
            return url
        logger.warning(f"[error_card] 上传失败: {upload_result.stderr}")
        return None
    except Exception as e:
        logger.warning(f"[error_card] 异常: {e}")
        return None
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def build_card_json(title, video_url, duration=None):
    """Build media_preview card JSON."""
    subtitle = "00:03"
    if duration:
        m, s = divmod(int(duration), 60)
        subtitle = f"{m:02d}:{s:02d}"
    return {
        "schema_version": "1.0",
        "card_type": "media_preview",
        "title": title,
        "subtitle": subtitle,
        "description": "AI 生成视频 · MP4",
        "button_text": "播放视频",
        "target_url": video_url,
        "theme": "video",
        "layout": {
            "variant": "media_preview",
            "icon": "video",
        },
    }


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


def translate_to_english(word):
    """调用千问 AI 将中文翻译为英文关键词
    
    Args:
        word: 中文词语
        
    Returns:
        英文关键词（小写），翻译失败则返回原文
    """
    import re
    # 如果已经是纯英文，直接返回
    if re.match(r'^[a-zA-Z\s]+$', word):
        return word.lower().strip()
    
    prompt_text = f'请将中文词语"{word}"翻译为英文，只输出英文单词，不要解释，不要标点。'
    
    try:
        response = dashscope.Generation.call(
            model='qwen-turbo',
            prompt=prompt_text,
            result_format='message',
            max_tokens=50,
            temperature=0.1,
            timeout=10
        )
        if response.status_code == 200:
            english = response.output.choices[0].message.content.strip().lower()
            # 清理可能的多余字符
            english = re.sub(r'[^a-z\s]', '', english).strip()
            english = english.split()[0] if english else word
            logger.info(f"[翻译] {word} -> {english}")
            return english
    except Exception as e:
        logger.warning(f"[翻译] 翻译失败: {e}")
    
    return word.lower()


def check_cos(keyword):
    """检查 COS 桶中是否有匹配关键词的视频
    
    从 config.json 的 videos 配置中获取关键词对应的视频数量，
    生成文件名列表（如 cat_1.mp4, cat_2.mp4），用 HEAD 请求验证是否存在，
    然后从所有存在的文件中随机等概率返回一个。
    
    Args:
        keyword: 英文关键词
        
    Returns:
        COS 视频链接，如果没有匹配则返回 None
    """
    cos_config = config.config.get("cos", {})
    base_url = cos_config.get("base_url", "")
    videos = cos_config.get("videos", {})
    
    if not base_url or not videos:
        logger.warning("[COS] 配置缺失，跳过 COS 查询")
        return None
    
    # 查找关键词对应的视频数量
    keyword_lower = keyword.lower()
    count = videos.get(keyword_lower)
    
    if not count or not isinstance(count, int) or count < 1:
        logger.info(f"[COS] 未找到关键词 '{keyword}' 的视频配置")
        return None
    
    # 生成文件名列表：{keyword}_{i}.mp4，i 从 1 到 count
    file_list = [f"{keyword_lower}_{i}.mp4" for i in range(1, count + 1)]
    
    # 用 HEAD 请求验证每个文件是否存在
    valid_files = []
    for filename in file_list:
        url = f"{base_url}/{filename}"
        try:
            resp = requests.head(url, timeout=10)
            if resp.status_code == 200:
                valid_files.append(url)
                logger.info(f"[COS] 验证存在: {filename}")
        except Exception as e:
            logger.warning(f"[COS] 验证失败: {filename}, {e}")
    
    if not valid_files:
        logger.info(f"[COS] 关键词 '{keyword}' 的文件都不存在")
        return None
    
    # 从所有存在的文件中随机等概率选择
    cos_url = random.choice(valid_files)
    logger.info(f"[COS] 随机选择: {cos_url}")
    return cos_url


def _load_prompt_rules():
    """从 references/prompt-rules.md 读取提示词规则"""
    rules_path = os.path.join(SCRIPT_DIR, '..', 'references', 'prompt-rules.md')
    try:
        with open(rules_path, 'r', encoding='utf-8') as f:
            return f.read()
    except FileNotFoundError:
        logger.error(f"[提示词规则] 文件不存在: {rules_path}")
        return ""


def resolve_prompt(word):
    """调用 LLM 生成完整英文提示词"""
    rules = _load_prompt_rules()

    prompt_text = (
        f'请为词语"{word}"生成完整的 AI 视频提示词（英文）。\n\n'
        f'以下是提示词生成规则：\n{rules}\n\n'
        f'请严格按照以上规则，根据输入内容自行判断最合适的场景方向和动态表现。\n\n'
        f'重要：每次生成必须有随机性，选择不同的场景、角度、动作组合，避免重复。\n\n'
        f'风格要求：raw handheld camera footage style, natural lighting, imperfect but realistic texture\n\n'
        f'直接输出英文提示词，不要解释，不要换行。'
    )

    max_retries = 2
    for attempt in range(max_retries + 1):
        try:
            response = dashscope.Generation.call(
                model='qwen-turbo',
                prompt=prompt_text,
                result_format='message',
                max_tokens=500,
                temperature=1.0,
                timeout=15
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


def generate_video(prompt, model=None, duration=None, resolution=None, refs=None):
    """使用 DashScope VideoSynthesis API 生成视频
    
    Args:
        prompt: 提示词
        model: 模型名称
        duration: 视频时长（秒）
        resolution: 分辨率
        refs: 参考素材列表，每项包含 type 和 url
    """
    # 如果有参考素材，使用 R2V 模型
    if refs:
        # 检查是否有视频参考（happyhorse 不支持，需要回退到 wan2.7-r2v）
        has_video_ref = any(ref.get('type') == 'reference_video' for ref in refs)
        if has_video_ref:
            model = model or 'wan2.7-r2v'
            logger.info("[文生视频] 检测到视频参考素材，使用 wan2.7-r2v")
        else:
            model = model or config.get_model('video_generation_ref') or 'happyhorse-1.0-r2v'
    else:
        model = model or config.get_model('video_generation') or 'happyhorse-1.0-t2v'
    duration = duration or config.get_param('video_duration') or 3
    resolution = resolution or config.get_param('video_resolution') or '1080P'

    # 确保时长是 2-15 范围内的整数（R2V 最长 10 秒）
    if refs:
        duration = max(2, min(10, int(duration)))
    else:
        duration = max(2, min(15, int(duration)))

    logger.info(f"[文生视频] 提示词: {prompt}")
    logger.info(f"[文生视频] 模型: {model}, 时长: {duration}s, 分辨率: {resolution}")
    if refs:
        logger.info(f"[文生视频] 参考素材: {len(refs)} 个")

    # 负向提示词排除AI风格和非写实元素
    negative_prompt = (
        # 非写实风格
        "卡通, 动漫, 插画, CGI, 3D渲染, 绘画, 手绘, "
        "动画, 数字艺术, 低多边形, 赛璐璐渲染, "
        "玩具感, 微缩模型, 过饱和, 塑料感, "
        "滤镜, 后期调色, 梦幻, 魔幻, 奇幻, "
        # AI典型缺陷
        "过度平滑, 过度锐化, AI生成感, 完美对称, "
        "不自然光照, 模糊纹理, 变形, 肢体扭曲, "
        "恐怖谷效应, 僵硬动作, 机械感, 数字感, "
        "过度美化, 磨皮, 美颜, 虚假质感, "
        # 摄影缺陷
        "镜头光晕, 镜头畸变, 色散, 紫边"
    )

    try:
        seed = random.randint(1, 999999)
        logger.info(f"[文生视频] seed: {seed}")

        # 构建 API 调用参数
        call_params = {
            'api_key': dashscope.api_key,
            'model': model,
            'prompt': prompt,
            'negative_prompt': negative_prompt,
            'duration': duration,
            'resolution': resolution,
            'prompt_extend': True,
            'watermark': False,
            'seed': seed
        }

        # 如果有参考素材，添加 media 参数
        if refs:
            call_params['media'] = refs

        rsp = VideoSynthesis.async_call(**call_params)
        if rsp.status_code != HTTPStatus.OK:
            error_msg = f"任务提交失败: {rsp.message}"
            if hasattr(rsp, 'code') and rsp.code:
                error_msg += f" (code: {rsp.code})"
            logger.error(f"[视频生成] {error_msg}")
            return {"error": error_msg}

        task_id = rsp.output.task_id
        logger.info(f"[视频生成] 任务已提交，task_id: {task_id}，等待完成...")

        # 轮询任务状态
        for _ in range(120):  # 最多等待 10 分钟（120 * 5秒）
            time.sleep(5)
            result = VideoSynthesis.fetch(task_id, api_key=dashscope.api_key)
            status = result.output.task_status

            if status == 'SUCCEEDED':
                video_url = result.output.video_url
                logger.info(f"生成成功！视频链接: {video_url}")

                output_filename = generate_unique_filename("video", 'mp4')
                output_path = os.path.join(OUTPUT_FOLDER, output_filename)

                resp = requests.get(video_url, timeout=60)
                resp.raise_for_status()
                with open(output_path, 'wb') as f:
                    f.write(resp.content)

                logger.info(f"[视频生成] 已保存到: {output_path}")

                return {
                    "success": True,
                    "video_url": video_url,
                    "local_path": output_path,
                    "duration": duration,
                    "resolution": resolution
                }
            elif status == 'FAILED':
                error_msg = f"任务失败: {result.output.message}"
                logger.error(f"[视频生成] {error_msg}")
                return {"error": error_msg}
            else:
                logger.info(f"[视频生成] 任务状态: {status}，继续等待...")

        logger.error("[视频生成] 任务超时（10分钟）")
        return {"error": "任务超时，请稍后重试"}

    except (requests.exceptions.ReadTimeout, requests.exceptions.Timeout, TimeoutError) as e:
        logger.error(f"[视频生成] 请求超时: {e}")
        return {"error": f"模型请求超时: {e}"}
    except Exception as e:
        logger.error(f"[视频生成] 请求异常: {e}")
        return {"error": f"模型请求异常: {e}"}


def _match_resource(user_input):
    """用 LLM 判断用户输入是否匹配预置资源，匹配则返回 (id, url)，否则返回 None
    返回 None 表示没有资源文件，返回 ("__no_match__", None) 表示有资源但没命中"""
    resources_path = os.path.join(SCRIPT_DIR, '..', 'references', 'resources.json')
    if not os.path.exists(resources_path):
        return None

    try:
        with open(resources_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except Exception as e:
        logger.warning(f"[资源匹配] 加载失败: {e}")
        return None

    resources = data.get('resources', [])
    if not resources:
        return None

    # 列出资源信息供 LLM 判断（包含 tags，提高语义匹配准确率）
    items = "\n".join(
        f"- {r['id']}（{r['description']}）\n  相关词：{'、'.join(r.get('tags', []))}"
        for r in resources
    )
    prompt_text = (
        f'用户输入："{user_input}"\n\n'
        f'判断用户输入描述的动作/场景是否与以下某个资源的描述一致：\n'
        f'{items}\n\n'
        f'规则：只有用户输入和资源的描述指向同一个动作/场景时才匹配，'
        f'仅仅是相关但不相同则不匹配（如"老鼠挖洞"和"老鼠觅食"不匹配）。\n\n'
        f'如果匹配，只输出匹配的 id，不要带引号或标点；如果不匹配，只输出 "null"。'
    )

    try:
        response = dashscope.Generation.call(
            model='qwen-turbo',
            prompt=prompt_text,
            result_format='message',
            max_tokens=50,
            temperature=0.05,
            timeout=10
        )
        if response.status_code == 200:
            matched_text = response.output.choices[0].message.content.strip().strip('"\'')
            # LLM 可能输出换行分隔的多个 id，逐个匹配
            for matched_id in matched_text.replace('\r', '').split('\n'):
                matched_id = matched_id.strip().strip('"\'')
                if not matched_id:
                    continue
                for r in resources:
                    if r['id'] == matched_id:
                        logger.info(f"[资源匹配] 命中: {r['id']} → {r['url']}")
                        return (r['id'], r['url'])
            logger.info(f"[资源匹配] LLM 输出了未知 id: {matched_text.replace(chr(10), ', ')}")
    except Exception as e:
        logger.warning(f"[资源匹配] LLM 调用异常: {e}")

    return ("__no_match__", None)


def main():
    parser = argparse.ArgumentParser(description='短视频生成 CLI')
    parser.add_argument('--prompt', '-p', required=True, help='描述词或完整中文提示词')
    parser.add_argument('--output', '-o', default=None, help='输出文件路径（默认保存到 outputs/ 目录）')
    parser.add_argument('--model', '-m', default=None, help='指定模型（默认使用 video_generation 配置）')
    parser.add_argument('--duration', '-d', type=int, default=None, help='视频时长（秒，2-15，默认 3）')
    parser.add_argument('--resolution', '-r', default=None, help='视频分辨率（默认 1080P）')
    parser.add_argument('--ref', nargs='+', default=None, help='参考素材URL列表（图片或视频），使用参考生视频模式（happyhorse-1.0-r2v）')

    args = parser.parse_args()

    logger.info(f"[video-generator] 开始处理: prompt={args.prompt}")

    # 语义匹配预置资源：匹配则直接返回，跳过 AI 生成
    matched = _match_resource(args.prompt)
    if matched and matched[0] != "__no_match__":
        _, resource_url = matched
        card_json = build_card_json(args.prompt, resource_url)
        card_url = deploy_card(card_json)
        if card_url:
            print(json.dumps({
                "type": "webview",
                "title": args.prompt,
                "url": card_url,
            }, ensure_ascii=False))
        else:
            logger.warning("[video-generator] 卡片渲染失败")
            print(json.dumps({
                "success": False,
                "error": "卡片渲染失败，请稍后重试",
                "video_url": resource_url,
            }, ensure_ascii=False))
        sys.exit(0)

    # 有资源但未命中：跳过旧 COS 路径（否则翻译截断会误匹配）
    has_resources = matched is not None and matched[0] == "__no_match__"

    # 构建参考素材列表
    refs = None
    if args.ref:
        refs = []
        for url in args.ref:
            # 根据 URL 后缀判断素材类型
            lower = url.lower()
            if any(lower.endswith(ext) for ext in ['.mp4', '.mov', '.avi', '.webm']):
                refs.append({"type": "reference_video", "url": url})
            else:
                refs.append({"type": "reference_image", "url": url})
        logger.info(f"[video-generator] 参考素材: {refs}")

    # 检查 COS：有 resources.json 时跳过旧路径（避免翻译截断误匹配），
    # 无 resources.json 时向后兼容
    is_full_prompt = ',' in args.prompt or '，' in args.prompt or len(args.prompt.split()) > 3 or len(args.prompt) > 10
    if not has_resources and not is_full_prompt:
        english_keyword = translate_to_english(args.prompt)
        cos_url = check_cos(english_keyword)
        if cos_url:
            logger.info(f"[video-generator] 找到 COS 视频: {cos_url}")
            
            # 如果指定了 --output，下载视频到本地
            if args.output:
                output_dir = os.path.dirname(os.path.abspath(args.output))
                if output_dir:
                    os.makedirs(output_dir, exist_ok=True)
                try:
                    resp = requests.get(cos_url, timeout=60)
                    resp.raise_for_status()
                    with open(args.output, 'wb') as f:
                        f.write(resp.content)
                    logger.info(f"[video-generator] 已下载到: {args.output}")
                except Exception as e:
                    logger.warning(f"[video-generator] 下载视频失败: {e}")
            
            # 构造卡片 → 渲染 → 输出 webview
            card_json = build_card_json(args.prompt, cos_url)
            card_url = deploy_card(card_json)

            if card_url:
                print(json.dumps({
                    "type": "webview",
                    "title": args.prompt,
                    "url": card_url,
                }, ensure_ascii=False))
            else:
                # 卡片渲染失败，输出失败而不是 mp4（APK WebView 无法渲染 mp4）
                logger.warning("[video-generator] 卡片渲染失败")
                print(json.dumps({
                    "success": False,
                    "error": "卡片渲染失败，请稍后重试",
                    "video_url": cos_url,  # 保留视频链接供调试
                }, ensure_ascii=False))
            sys.exit(0)

    # 判断输入是完整提示词还是简单词语

    if is_full_prompt:
        prompt = args.prompt
        logger.info(f"[video-generator] 使用完整提示词: {prompt}")
    else:
        logger.info(f"[video-generator] 调用 LLM 生成提示词...")
        prompt = resolve_prompt(args.prompt)
        logger.info(f"[video-generator] LLM 生成提示词: {prompt}")

    # 如果有参考素材，在提示词中加入引用标识
    if refs:
        ref_tags = []
        for i, ref in enumerate(refs, 1):
            if ref['type'] == 'reference_image':
                ref_tags.append(f"图{i}")
            else:
                ref_tags.append(f"视频{i}")
        ref_desc = "、".join(ref_tags)
        prompt = f"参考素材：{ref_desc}。{prompt}"
        logger.info(f"[video-generator] 提示词已添加参考标识: {prompt}")

    # 生成视频
    result = generate_video(
        prompt=prompt,
        model=args.model,
        duration=args.duration,
        resolution=args.resolution,
        refs=refs
    )

    if 'error' in result:
        print(json.dumps({"success": False, "error": result["error"]}, ensure_ascii=False))
        sys.exit(1)

    # AI 内容审查
    video_url = result.get('video_url', '')
    if video_url:
        review = review_video(video_url, prompt, args.prompt)
        if not review.get('passed', True):
            # 审查不通过，删除已下载的视频
            local_path = result.get('local_path', '')
            if local_path and os.path.exists(local_path):
                os.remove(local_path)
                logger.info(f"[video-generator] 审查未通过，已删除视频: {local_path}")
            error_msg = f"AI 内容审查未通过: {review.get('reason', '未说明原因')}"
            card_url = deploy_error_card(args.prompt, error_msg)
            if card_url:
                print(json.dumps({
                    "type": "webview",
                    "title": args.prompt,
                    "url": card_url,
                }, ensure_ascii=False))
            else:
                print(json.dumps({
                    "success": False,
                    "error": error_msg,
                }, ensure_ascii=False))
            sys.exit(1)

    local_path = result.get('local_path', '')

    # 如果指定了 --output，额外复制一份到目标路径
    if args.output and local_path:
        output_dir = os.path.dirname(os.path.abspath(args.output))
        if output_dir:
            os.makedirs(output_dir, exist_ok=True)
        shutil.copy2(local_path, args.output)
        logger.info(f"[video-generator] 已复制到: {args.output}")

    # 上传 COS → 构造卡片 → 渲染 → 输出 webview
    cos_url = upload_to_cos(local_path) if local_path else None
    if not cos_url:
        # COS 上传失败，返回错误而不是用临时 URL
        logger.error("[video-generator] COS 上传失败")
        print(json.dumps({
            "success": False,
            "error": "视频上传失败，请稍后重试",
        }, ensure_ascii=False))
        sys.exit(1)

    card_json = build_card_json(args.prompt, cos_url, result.get("duration"))
    card_url = deploy_card(card_json)

    if card_url:
        print(json.dumps({
            "type": "webview",
            "title": args.prompt,
            "url": card_url,
        }, ensure_ascii=False))
    else:
        # 卡片渲染失败，输出失败而不是 mp4（APK WebView 无法渲染 mp4）
        logger.warning("[video-generator] 卡片渲染失败")
        print(json.dumps({
            "success": False,
            "error": "卡片渲染失败，请稍后重试",
            "video_url": cos_url,  # 保留视频链接供调试
        }, ensure_ascii=False))


if __name__ == "__main__":
    main()
