#!/usr/bin/env bash
# image-generator/scripts/deploy.sh
# 生成图片 → 自动上传 COS → 输出 URL
#
# 用法: bash image-generator/scripts/deploy.sh --prompt "词语" [--model ...] [--size ...] [--no-remove-bg]
# 依赖: image-generator/scripts/generate.py, upload-to-oss/run.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILL_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$SKILL_DIR")"
SKILL_NAME="$(basename "$SKILL_DIR")"

# Detect Python
PYTHON=""
for py in python3 python py; do
    if command -v "$py" >/dev/null 2>&1 && "$py" -c "import sys" >/dev/null 2>&1; then
        PYTHON="$py"
        break
    fi
done
if [ -z "$PYTHON" ]; then
    echo "错误：未找到 Python" >&2
    exit 1
fi

# 1. 调用 generate.py，透传所有参数
echo "生成图片..." >&2
JSON=$("$PYTHON" "$SCRIPT_DIR/generate.py" "$@")
echo "$JSON" >&2

# 2. 解析结果
SUCCESS=$(echo "$JSON" | "$PYTHON" -c "import sys,json; d=json.load(sys.stdin); print(d.get('success',''))")

if [ "$SUCCESS" != "True" ]; then
    echo "$JSON"
    exit 1
fi

LOCAL_PATH=$(echo "$JSON" | "$PYTHON" -c "import sys,json; d=json.load(sys.stdin); print(d.get('local_path',''))")
COS_KEY=$(echo "$JSON" | "$PYTHON" -c "import sys,json; d=json.load(sys.stdin); print(d.get('cos_key',''))")

if [ -z "$LOCAL_PATH" ] || [ -z "$COS_KEY" ]; then
    echo "错误：无法解析生成结果" >&2
    echo "$JSON"
    exit 1
fi

# 3. 上传到 COS（路径自动从 skill 目录名推导，失败时回退本地路径）
echo "上传: ${LOCAL_PATH} → ${SKILL_NAME}/${COS_KEY}" >&2
URL=""
URL=$("$PYTHON" "$ROOT_DIR/upload-to-oss/main.py" upload "$LOCAL_PATH" "${SKILL_NAME}/${COS_KEY}" 2>/dev/null) || true
if [ -n "$URL" ]; then
    echo "  → ${URL}" >&2
    echo "$URL"
else
    echo "  COS 上传失败，回退本地路径: ${LOCAL_PATH}" >&2
    echo "$LOCAL_PATH"
fi
