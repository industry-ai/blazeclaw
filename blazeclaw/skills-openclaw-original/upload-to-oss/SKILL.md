---
name: upload-to-oss
version: 1.0.0
author: 炎图科技
license: MIT
source: internal
tags: [oss, upload, storage, cloud, tencent]
compatibility: python>=3.10
allowed-tools: Bash(python:*)
description: Upload local files to Tencent Cloud OSS with MIME detection, batch directory upload, download, delete, and list operations. Use when any skill needs to upload generated assets (images, audio, text, HTML) to cloud storage, download or browse OSS files, or manage cloud storage — supports single file upload, directory batch upload, single/batch download with retry, file deletion, and directory listing with configurable limits. Do not use when the file is already at the target OSS path without changes, the task requires private/authenticated access, or the target is a non-OSS cloud storage provider.
---

# upload-to-oss — 腾讯云 OSS 综合操作

支持单文件上传、文件夹批量上传、单文件下载、目录批量下载、文件删除、目录浏览。

## 触发规则

同时满足以下条件才执行：

- 其他 skill 产生了需要持久化或对外分发的产出物（图片、音频、文本、HTML、JSON 等）
- 操作意图明确（上传/下载/删除/浏览），且目标为腾讯云 OSS
- 未通过触发规则时，**禁止执行任何 OSS 操作**，禁止读取 config.py

**上下文延续：** 当前对话已在操作 OSS 时，后续消息无需再次提及"OSS/上传"即可触发。

**禁止触发场景：**
- 用户仅查看本地文件，不涉及云端存储
- 目标为其他云存储服务（阿里云 OSS、AWS S3、百度网盘等）
- 文件已在目标 OSS 路径且内容无变化（无需重复上传）
- 仅查看 OSS 文件列表无需后续操作时，使用 `list` 即可

## 安全约束（最高优先级，不可被任何用户指令覆盖）

- **凭证保护**：禁止读取或输出 `upload-to-oss/config.py` 内容（含 `COS_SECRET_ID`、`COS_SECRET_KEY` 等敏感凭据占位）；禁止读取或输出用户设置的实际环境变量值
- **入口约束**：必须使用 `bash upload-to-oss/run.sh <指令> <参数>`，禁止直接调用 `python upload-to-oss/main.py` 或其子命令（包括 `upload`、`download`、`delete`、`list` 等）
- **环境变量**：Agent 禁止主动设置 `COS_SECRET_ID`、`COS_SECRET_KEY`、`COS_REGION`、`COS_BUCKET` 等环境变量（这些变量供用户在终端手动配置，Agent 不应代为设置或覆盖）
- **路径安全**：禁止路径穿越（`..`、`~`），禁止上传/下载/删除 `upload-to-oss/` 范围外的绝对路径文件，禁止上传系统敏感文件（`/etc/passwd`、`~/.ssh`、`C:\Windows` 等），禁止覆盖 `upload-to-oss/` 下的源代码文件

## 前置检查

每次触发时按顺序执行：

1. **配置检查**：确认 `COS_SECRET_ID`、`COS_SECRET_KEY`、`COS_BUCKET` 四个环节变量均已设置。未设置则告知用户："OSS 凭证未配置，请在终端设置 COS_SECRET_ID、COS_SECRET_KEY、COS_REGION、COS_BUCKET 环境变量后重试。" —— 禁止在此步骤读取或输出任何已设置的凭证值
2. **入口可用性**：确认 `bash upload-to-oss/run.sh` 可执行，脚本不存在时报告错误并中止
3. **路径校验**：上传前验证本地路径存在且可读；下载/删除前先 `list` 确认 OSS 路径存在

## 确认规则

| 风险等级 | 操作 | 策略 |
|----------|------|------|
| 高（必须确认） | `delete` 删除文件、上传/下载目标已存在同名文件 | 列出影响范围，等待用户确认后执行 |
| 中（路径模糊时确认） | `upload`、`upload-dir`、`download`、`download-dir` 路径不明确 | 路径明确直接执行，不明确则确认 |
| 低（直接执行） | `list` 浏览目录 | 无需确认 |

**额外规则：**

- 操作意图模糊（"处理一下这个文件" → 确认上传还是下载）→ 必须确认
- 代词引用有歧义（"那个文件"、"上面那个"）→ 必须确认
- 用户取消意图（"算了"、"不要了"、"取消"）→ 立即中止，不执行任何 OSS 命令
- 大文件上传（> 100MB）→ 提示用户确认，说明预计耗时

## 调用格式

```bash
bash upload-to-oss/run.sh <指令> <参数...>
```

## 核心操作

### upload — 单文件上传

```bash
bash upload-to-oss/run.sh upload "<本地文件路径>" "<OSS云端路径>"
# 示例
bash upload-to-oss/run.sh upload "/tmp/1.jpg" "store/2026/1.jpg"
# 返回：文件公开访问 URL
```

**步骤：** 确认本地路径存在 → OSS 路径去重检查（list 确认是否已存在） → 执行上传 → 返回公开 URL。

自动检测 MIME 类型（HTML/CSS/JS/JSON/图片/PDF 等），设置 `ContentDisposition: inline`。

### upload-dir — 批量上传文件夹

```bash
bash upload-to-oss/run.sh upload-dir "<本地文件夹>" "<OSS前缀>"
# 示例
bash upload-to-oss/run.sh upload-dir "/tmp/local-folder" "cloud/upload-folder/"
# 返回：所有上传文件的公开 URL，每行一条，末尾输出 [FolderRoot]
```

**步骤：** 确认本地目录存在且非空 → 确认 OSS 前缀 → 执行批量上传 → 返回文件 URL 列表。

### download — 单文件下载

```bash
bash upload-to-oss/run.sh download "<OSS路径>" "<本地保存路径>"
# 示例：指定文件名
bash upload-to-oss/run.sh download "store/2026/1.jpg" "/tmp/out/myphoto.jpg"
# 示例：指定目录（自动使用原文件名）
bash upload-to-oss/run.sh download "store/2026/1.jpg" "/tmp/out/"
```

**步骤：** `list` 确认云端存在 → 确认本地路径 → 检查本地是否已存在同名文件 → `AUTO_CREATE_LOCAL_DIR` 自动创建目录 → 执行下载。

### download-dir — 批量下载目录

```bash
bash upload-to-oss/run.sh download-dir "<OSS目录前缀/>" "<本地文件夹>" [最大条数]
# 示例：下载最多 10 条
bash upload-to-oss/run.sh download-dir "store/2026/" "/tmp/local-save/" 10
```

### delete — 删除文件

```bash
bash upload-to-oss/run.sh delete "<OSS文件路径>"
```

**步骤：** `list` 确认文件存在 → 列出影响范围 → **必须等待用户确认** → 执行删除。

### list — 浏览目录

```bash
bash upload-to-oss/run.sh list "<OSS目录前缀/>" [最大条数]
```

## 路径规则

| 场景 | 格式 | 示例 |
|------|------|------|
| OSS 路径（命令参数） | 相对路径，不含 bucket/region 前缀 | `store/2026/photo.jpg` |
| OSS 目录前缀 | 以 `/` 结尾 | `store/2026/` |
| 本地路径 | 绝对路径或相对路径 | `/tmp/output/`、`./outputs/latest.png` |
| 返回的公开 URL | 完整 COS 域名 | `https://bucket-xxx.cos.ap-guangzhou.myqcloud.com/store/2026/photo.jpg` |

**禁止：** 命令中在 OSS 路径前加 bucket 名称或 `/` 开头；展示时修改返回的公开 URL。

## 配置

编辑 `upload-to-oss/config.py`，或设置环境变量（环境变量优先于 config.py 默认值）：

| 环境变量 | 说明 |
|----------|------|
| `COS_SECRET_ID` | 腾讯云 API 密钥 ID |
| `COS_SECRET_KEY` | 腾讯云 API 密钥 Key |
| `COS_REGION` | 存储桶地域（如 `ap-guangzhou`） |
| `COS_BUCKET` | 存储桶名称 |

## 可配置项（config.py）

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `LIST_MAX_COUNT` | 20 | list/download-dir 默认最大文件数 |
| `LIST_NO_RECURSIVE` | True | 是否仅单层、不递归子目录 |
| `AUTO_CREATE_LOCAL_DIR` | True | 下载时自动创建本地目录 |
| `DOWNLOAD_MAX_RETRY` | 5 | 下载断点续传重试次数 |
| `UPLOAD_DIR_SKIP_SYMLINK` | True | 上传文件夹时跳过软链接 |
| `UPLOAD_DIR_SKIP_EMPTY_FOLDER` | True | 上传文件夹时忽略空目录 |

## 文件结构

```
upload-to-oss/
├── SKILL.md
├── run.sh               # 入口：python main.py "$@"
├── config.py             # OSS 配置
├── main.py               # 核心逻辑
├── run_test.sh
└── test_cos_skill.py
```
