# Video Generator - 短视频生成工具

一个通用的短视频生成 AI 工具，支持 2-15 秒自定义时长，写实风格。

## 功能特点

- **OSS 预置视频查询**：输入关键词自动翻译为英文，动态查询 OSS 桶中的预置视频，优先返回
- **智能提示词生成**：输入任意描述，自动调用 LLM 生成专业的英文视频提示词
- **趣味性场景**：自动选择有戏剧性的动态场景，避免平淡静态画面
- **写实风格**：原始实拍素材风格，手持镜头拍摄，自然光线
- **高质量视频生成**：使用阿里云 DashScope happyhorse-1.0-t2v 模型
- **自定义时长**：支持 2-15 秒视频时长，默认 3 秒

## 环境要求

- Python 3.10+
- 阿里云 DashScope API Key（需开通 happyhorse-1.0-t2v 模型权限）

## 安装步骤

### 1. 克隆或下载项目

```bash
git clone https://github.com/your-username/video-generator.git
```

### 2. 安装依赖

```bash
pip install -r requirements.txt
```

### 3. 配置 API Key

编辑 `config.json` 文件，填入你的 DashScope API Key：

```json
{
  "api_key": "sk-your-api-key-here",
  "models": {
    "video_generation": "happyhorse-1.0-t2v"
  },
  "default_params": {
    "video_resolution": "1080P"
  },
  "paths": {
    "output_folder": "outputs"
  }
}
```

## 使用方法

### 基础用法

输入任意描述，自动生成 3 秒视频（写实风格）：

```bash
python video-generator/scripts/generate.py --prompt "熊猫"
```

### 指定输出路径

默认视频保存在 `outputs/` 目录，可通过 `--output` 参数指定额外保存路径：

```bash
python video-generator/scripts/generate.py --prompt "狮子" --output ./my_video.mp4
```

### 自定义时长和分辨率

支持 2-15 秒时长（默认 3 秒），以及 480P/720P/1080P 分辨率（默认 1080P）：

```bash
python video-generator/scripts/generate.py --prompt "海豚" --duration 5 --resolution 480P
```

### 使用完整英文提示词

如果你已经有完整的英文提示词（包含逗号或超过 3 个单词），脚本会直接使用：

```bash
python video-generator/scripts/generate.py --prompt "A majestic lion walking in the savanna, slow motion"
```

## 参数说明

| 参数 | 简写 | 说明 | 是否必填 | 默认值 |
|------|------|------|----------|--------|
| `--prompt` | `-p` | 描述词或完整英文提示词 | 是 | - |
| `--output` | `-o` | 额外输出文件路径 | 否 | 仅保存到 outputs/ |
| `--model` | `-m` | 指定视频生成模型 | 否 | config.json 中的 video_generation |
| `--duration` | `-d` | 视频时长（秒，2-15） | 否 | 3 |
| `--resolution` | `-r` | 视频分辨率（480P/720P/1080P） | 否 | 1080P |
| `--ref` | - | 参考素材URL列表（图片或视频），启用参考生视频模式 | 否 | 无 |

## 输出示例

脚本执行成功后，会在控制台输出 JSON 结果：

**正常生成：**
```json
{
  "success": true,
  "prompt": "Two giant pandas tumbling in a bamboo forest, handheld tracking with slight wobble. Fur glistening with dew, visible muscle movement during rolls, morning light filtering through bamboo leaves creating light spots. Shallow depth of field, warm natural color temperature. Low grunts and panting.",
  "local_path": "outputs/video_20260612_143000.mp4",
  "duration": 3,
  "resolution": "1080P"
}
```

**OSS 匹配：**
```json
{
  "success": true,
  "prompt": "[OSS] 熊猫",
  "video_url": "https://static-1386436960.cos.ap-guangzhou.myqcloud.com/vedio/panda_1.mp4",
  "cached": true
}
```

视频文件会保存在 `outputs/` 目录下。

## OSS 视频查询

脚本会自动查询 OSS 桶中的预置视频。输入中文关键词时，先通过千问 AI 翻译为英文，然后在 config.json 中查找对应关键词的文件列表，用 HEAD 请求验证文件是否存在，随机返回一个 OSS 链接。

**匹配规则：**
- 从 config.json 的 `videos` 配置中查找对应关键词
- 用 HEAD 请求验证文件是否真实存在
- 多个匹配文件时随机返回一个

**config.json 中的 OSS 配置：**
```json
{
  "cos": {
    "base_url": "https://static-1386436960.cos.ap-guangzhou.myqcloud.com/vedio",
    "videos": {
      "cat": 1,
      "dog": 2,
      "tiger": 1
    }
  }
}
```

新增视频时，只需在 `videos` 中设置关键词对应的视频数量，文件名格式为 `{关键词}_{序号}.mp4`（如 `cat_1.mp4`, `cat_2.mp4`）。

## 工作流程

```
用户输入描述词
    ↓
翻译中文关键词为英文 → 查找 config.json 中的视频配置
    ↓
    ├─ 有匹配 → HEAD 验证后随机选择一个视频，返回 OSS 链接
    └─ 无匹配 → 继续生成流程
    ↓
判断是否为完整提示词（含逗号或超过 3 个单词）
    ↓
    ├─ 否 → 调用 qwen-turbo LLM 生成英文提示词（写实风格）
    └─ 是 → 直接使用输入内容
    ↓
调用 happyhorse-1.0-t2v 生成视频（支持 2-15 秒自定义时长）
    ↓
下载视频并保存到 outputs/ 目录
    ↓
AI 内容审查（内容安全 + 主题一致性 + 画面质量）
    ↓
审查通过 → 输出 JSON 结果
审查失败 → 删除视频并返回失败信息
```

## 常见问题

### 1. 视频生成失败，提示 API Key 错误

**解决方案**：检查 `config.json` 中的 `api_key` 是否正确填写，确保 DashScope 账户有足够额度。

### 2. 提示词被内容审核拦截

**解决方案**：确保提示词不包含敏感内容。脚本会自动过滤人类、建筑、道具等元素，但仍需避免其他可能触发审核的词汇。

### 3. 视频生成时间很长

**说明**：happyhorse-1.0-t2v 模型生成视频通常需要 1-5 分钟，这是正常现象。脚本会自动等待完成，请耐心等待。

### 4. 如何修改视频时长？

**说明**：通过 `--duration` 参数直接控制视频时长（2-15 秒），例如 `--duration 5` 生成 5 秒视频。默认值为 3 秒。

### 5. 提示词生成不符合预期

**解决方案**：可以查看 `references/prompt-rules.md` 了解提示词生成规则，或直接使用完整英文提示词（包含逗号或超过 3 个单词）跳过 LLM 生成步骤。

### 6. AI 内容审查未通过

**说明**：视频生成成功后，系统会自动调用 qwen-vl-max 多模态模型进行内容审查，审查三个维度：
- **内容安全**：是否包含违规、敏感、NSFW 等内容
- **主题一致性**：生成内容是否与输入的动物/主题匹配
- **画面质量**：清晰度、是否有明显变形、模糊、水印等质量问题

审查不通过时，已下载的视频会被自动删除，并返回失败信息及详细的审查报告。如需重试，可重新运行脚本。

## 项目结构

```
video-generator/
├── SKILL.md              # AI Agent 技能描述（供 AI 读取）
├── README.md             # 本使用文档（供人类阅读）
├── config.json           # 配置文件（需自行配置 API Key）
├── requirements.txt      # Python 依赖列表
├── .gitignore           # Git 忽略文件配置
├── scripts/
│   ├── generate.py      # 主执行脚本
│   ├── config_loader.py # 配置加载模块
│   └── content_review.py # AI 内容审查模块
├── references/
│   └── prompt-rules.md  # 提示词生成规则
└── outputs/             # 生成的视频输出目录（自动创建）
```

## 技术栈

- **Python 3.10+**：主要开发语言
- **DashScope SDK**：阿里云 AI 服务 SDK，用于调用 qwen-turbo 和 happyhorse-1.0-t2v
- **Requests**：HTTP 客户端，用于下载生成的视频

## 许可证

MIT License

## 联系方式

如有问题或建议，请联系炎图科技。
