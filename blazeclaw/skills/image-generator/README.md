# Image Generator 项目文档

## 项目概述
教育插图生成工具，可将任意词语自动转换为卡通风格教育插图，支持透明背景。

## 架构设计
```text
用户输入词语
    │
    ▼
scripts/generate.py --prompt "词语"
    │
    ├── 调用 qwen-turbo LLM  ──▶ 生成完整英文提示词
    │
    ├── 调用 DashScope wan2.7-image-pro API  ──▶ 生成卡通图片（纯白背景）
    │
    └── 白色像素去底  ──▶ 透明背景 PNG
    │
    ▼
调用 upload-to-oss 技能 ──▶ 上传图片到 OSS，返回 URL
    │
    ▼
调用 format-result 技能 ──▶ 标准化 JSON 响应
```

## 核心功能
1. **智能提示词生成** - 自动将中文词语转为英文提示词
2. **图片生成** - 调用 wan2.7-image-pro 模型生成卡通图片
3. **白色像素去底** - 将纯白/近白背景像素替换为透明（提示词约束主体不使用纯白）
4. **OSS 上传** - 通过 upload-to-oss 技能上传到腾讯云 OSS
5. **标准输出** - 通过 format-result 技能返回统一格式

## 使用方式
```bash
# 安装依赖
pip install -r requirements.txt

# 基本调用（生成图片到本地）
python image-generator/scripts/generate.py --prompt "桌子"

# 上传到 OSS
bash upload-to-oss/run.sh upload "outputs/generate_xxx.png" "image-generator/xxx.png"

# 格式化输出
python format-result/scripts/format.py --skill-id image-generator --skill-name "image-generator" --url "OSS_URL" --url-title "生成图片"
```

## 依赖管理
- `dashscope` - 阿里云 DashScope SDK，用于调用 qwen-turbo LLM 生成英文提示词和 wan2.7-image-pro 生成卡通图片
- `requests` - 下载 DashScope 返回的远程图片链接到本地文件
- `Pillow` - 图片处理库，去底时用于打开和保存 PNG 图片
- `numpy` - 数值计算，去底时用于将白色像素替换为透明

## 配置文件
`config.json` - 存放 API Key 和模型配置

## 容错机制
- 调用 wan2.7-image-pro 生成图片
- 内容审核拦截时通过 format-result 返回失败
- 白色像素去底失败时保留原始图片继续流程
- upload-to-oss 失败时通过 format-result 返回失败
- **AI 内容审查**：图片生成成功后，自动调用 qwen-vl-max 多模态模型进行内容审查（内容安全、主题一致性、画面质量），审查不通过时返回失败并删除已下载的图片

## 代码结构
```
image-generator/
├── SKILL.md              # 技能描述
├── README.md             # 本使用文档
├── config.json           # 配置文件
├── requirements.txt      # 依赖声明
├── .gitignore           # Git 忽略文件配置
├── scripts/
│   ├── generate.py       # 主执行脚本
│   ├── config_loader.py  # 配置加载
│   └── content_review.py # AI 内容审查模块
├── references/
│   └── prompt-rules.md   # 提示词规则
└── outputs/              # 生成的图片输出目录（自动创建）
```

## AI 内容审查

图片生成成功后，系统会自动调用 qwen-vl-max 多模态模型进行内容审查，审查三个维度：

| 审查维度 | 说明 |
|----------|------|
| **内容安全** | 是否包含违规、敏感、NSFW 等内容 |
| **主题一致性** | 生成内容是否与输入词语匹配 |
| **画面质量** | 清晰度、是否有明显变形、模糊、水印等质量问题 |

审查不通过时，输出格式：
```json
{
  "success": false,
  "error": "AI 内容审查未通过: 具体原因",
  "review_details": {
    "content_safety": {"passed": true, "reason": ""},
    "topic_consistency": {"passed": false, "reason": "图片内容与输入主题不符"},
    "visual_quality": {"passed": true, "reason": ""},
    "overall_passed": false,
    "summary": "具体评价"
  }
}
```