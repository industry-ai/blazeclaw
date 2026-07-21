---
name: ppt-auto
description: PPT制作、修改、转HTML并自动上传COS返回链接。
---

# PPT Auto



## 使用方式

### 一键全流程（推荐）
制作PPT → 转HTML → 上传COS(/html-ppt/) → 输出访问链接 → 自动清理

```bash
uuid=$(uuidgen)
skillDir="$HOME/.config/opencode/skills/ppt-auto"
tempDir="$skillDir/templates/temp-$uuid"
mkdir -p "$tempDir"
pptx="$tempDir/output.pptx"

node "$skillDir/index.js" modify-direct "$skillDir/模版/语文模版.pptx" "$skillDir/lisao-content.json" "$pptx"
node "$skillDir/index.js" convert "$pptx" "$tempDir"

cosDir="$HOME/.config/opencode/skills/cos-upload-skill"
cd "$cosDir"
url=$(python main.py "html-ppt/$uuid" "$tempDir")
echo "$url"
rm -rf "$tempDir"
cd "$skillDir"
```

```powershell
$uuid = [Guid]::NewGuid().ToString()
$skillDir = "C:\Users\1\.config\opencode\skills\ppt-auto"
$tempDir = "$skillDir\templates\temp-$uuid"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null
$pptx = "$tempDir\output.pptx"

node "$skillDir\index.js" modify-direct "$skillDir\模版\语文模版.pptx" "$skillDir\lisao-content.json" $pptx
node "$skillDir\index.js" convert $pptx $tempDir 

$cosDir = "C:\Users\1\.config\opencode\skills\cos-upload-skill"
Set-Location $cosDir
$url = python main.py "html-ppt/$uuid" $tempDir
Write-Output $url
Remove-Item -Recurse -Force $tempDir
Set-Location $skillDir
```

### 单独命令

| 操作 | 命令 |
|------|------|
| 修改模板 | `node index.js modify-direct <模板.pptx> <内容.json> <输出.pptx>` |
| 转HTML | `node index.js convert <输入.pptx> [输出目录]` |

## 模板与内容

| 学科 | 模板 | 内容JSON |
|------|------|---------|
| 语文 | `模版\语文模版.pptx` | `lisao-content.json` |
| 英语 | `模版\英语模版.pptx` | `learn-english-content.json` |

内容JSON格式（键=页码，值=文本数组）：
```json
{"1":["文本1","文本2"],"2":["文本1"]}
```

## 文件结构

```
ppt-auto/
├── index.js
├── lib/ (create.js, modify.js, convert.js)
├── 模版/
├── templates/
├── lisao-content.json
├── learn-english-content.json
└── SKILL.md
```