#!/usr/bin/env bash
# h5-cards/scripts/deploy.sh
# 从 stdin 读取卡片 JSON → 渲染 HTML + 保存 JSON/CSS/JS → 全部上传 OSS → 输出 HTML URL
#
# 用法: echo '<JSON>' | bash deploy.sh
# 依赖: h5-cards/scripts/render.py, upload-to-oss/run.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILL_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$SKILL_DIR")"
TIMESTAMP="$(date +%Y%m%d%H%M%S 2>/dev/null || echo "manual")"

# Cross-platform temp directory
if [ -n "${TMPDIR:-}" ]; then
    TMP_DIR="${TMPDIR}/h5-cards-${TIMESTAMP}"
elif [ -n "${TEMP:-}" ]; then
    TMP_DIR="${TEMP}/h5-cards-${TIMESTAMP}"
elif [ -d /tmp ]; then
    TMP_DIR="/tmp/h5-cards-${TIMESTAMP}"
else
    TMP_DIR="${SCRIPT_DIR}/../tmp/h5-cards-${TIMESTAMP}"
fi

COS_PREFIX="h5-cards/${TIMESTAMP}"

mkdir -p "$TMP_DIR"

# Detect Python (try python3 first, then python)
PYTHON=""
for py in python3 python py; do
    if command -v "$py" >/dev/null 2>&1; then
        PYTHON="$py"
        break
    fi
done
if [ -z "$PYTHON" ]; then
    echo "错误：未找到 Python" >&2
    exit 1
fi

# 1. 渲染 HTML + 保存所有资源文件
"$PYTHON" "$SCRIPT_DIR/render.py" "${TMP_DIR}/index.html" --save-dir "$TMP_DIR"
echo "渲染完成: ${TMP_DIR}" >&2

# 2. 上传所有文件到 OSS
HTML_URL=""
for f in "$TMP_DIR"/*; do
    fname="$(basename "$f")"
    cos_path="${COS_PREFIX}/${fname}"
    echo "上传: ${fname} → ${cos_path}" >&2
    url=$("$PYTHON" "$ROOT_DIR/upload-to-oss/main.py" upload "$f" "$cos_path")
    echo "  → ${url}" >&2
    if [ "$fname" = "index.html" ]; then
        HTML_URL="$url"
    fi
done

# 3. 清理临时目录
rm -rf "$TMP_DIR"

# 4. 返回 HTML URL（供上级脚本/format-result 使用）
if [ -n "$HTML_URL" ]; then
    echo "$HTML_URL"
else
    echo "错误：未找到 index.html" >&2
    exit 1
fi
