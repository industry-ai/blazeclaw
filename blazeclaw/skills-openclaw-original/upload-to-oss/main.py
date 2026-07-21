import logging
import mimetypes
import subprocess
import sys
from pathlib import Path

from config import (
    AUTO_CREATE_LOCAL_DIR,
    BUCKET,
    DOWNLOAD_MAX_RETRY,
    LIST_MAX_COUNT,
    LIST_NO_RECURSIVE,
    PUBLIC_URL_TEMPLATE,
    REGION,
    SECRET_ID,
    SECRET_KEY,
    UPLOAD_DIR_SKIP_EMPTY_FOLDER,
    UPLOAD_DIR_SKIP_SYMLINK,
)

# 日志配置
logging.basicConfig(level=logging.INFO, stream=sys.stdout)

# 自动安装COS SDK
try:
    from qcloud_cos import CosConfig, CosS3Client
    from qcloud_cos.cos_exception import CosClientError, CosServiceError
except ImportError:
    print("检测未安装COS SDK，正在自动安装...")
    subprocess.check_call([
        sys.executable,
        "-m",
        "pip",
        "install",
        "-q",
        "cos-python-sdk-v5",
    ])
    from qcloud_cos import CosConfig, CosS3Client
    from qcloud_cos.cos_exception import CosClientError, CosServiceError


def get_cos_client():
    # 优先读取环境变量密钥，无则使用config配置
    cfg = CosConfig(
        Region=REGION,
        SecretId=SECRET_ID,
        SecretKey=SECRET_KEY,
        Token=None,
        Scheme="https",
    )
    return CosS3Client(cfg)


# 自动匹配文件后缀对应的Content-Type
def get_file_content_type(file_path: Path) -> str:
    ext = file_path.suffix.lower()
    mime_map = {
        ".html": "text/html; charset=utf-8",
        ".htm": "text/html; charset=utf-8",
        ".css": "text/css; charset=utf-8",
        ".js": "application/javascript; charset=utf-8",
        ".json": "application/json; charset=utf-8",
        ".png": "image/png",
        ".jpg": "image/jpeg",
        ".jpeg": "image/jpeg",
        ".gif": "image/gif",
        ".svg": "image/svg+xml",
        ".ico": "image/x-icon",
        ".webp": "image/webp",
        ".txt": "text/plain; charset=utf-8",
        ".md": "text/markdown; charset=utf-8",
        ".xml": "application/xml; charset=utf-8",
        ".pdf": "application/pdf",
        ".zip": "application/zip",
    }
    # 无匹配则使用通用二进制
    return mime_map.get(ext, "application/octet-stream")


# 单文件上传
def upload_file(local_path: Path, cos_key: str):
    client = get_cos_client()
    if not local_path.exists():
        raise FileNotFoundError(f"本地文件不存在: {local_path}")
    if local_path.is_dir():
        raise IsADirectoryError(f"路径是文件夹，请使用 upload-dir 指令: {local_path}")

    # 自动获取文件MIME类型
    content_type = get_file_content_type(local_path)
    client.put_object_from_local_file(
        Bucket=BUCKET,
        LocalFilePath=str(local_path),
        Key=cos_key,
        ContentType=content_type,
        ContentDisposition="inline",  # 在线打开，不强制下载
    )
    url = PUBLIC_URL_TEMPLATE.format(bucket=BUCKET, region=REGION, key=cos_key)
    print(url)


# 批量上传本地文件夹



# 单文件断点续传下载（官方download_file接口）
def download_file(cos_key: str, save_target: Path):
    client = get_cos_client()
    origin_filename = Path(cos_key).name
    # 判断目标是文件夹还是文件
    if save_target.is_dir() or str(save_target).endswith("/"):
        local_save_path = save_target / origin_filename
    else:
        local_save_path = save_target

    if AUTO_CREATE_LOCAL_DIR:
        local_save_path.parent.mkdir(parents=True, exist_ok=True)

    max_retry = DOWNLOAD_MAX_RETRY
    for retry_times in range(max_retry):
        try:
            client.download_file(
                Bucket=BUCKET, Key=cos_key, DestFilePath=str(local_save_path)
            )
            print(f"下载完成，本地路径: {local_save_path}")
            return
        except (CosClientError, CosServiceError) as err:
            print(f"下载异常，重试 {retry_times + 1}/{max_retry} | 错误: {err!s}")
    raise Exception(f"文件 {cos_key} 连续重试{max_retry}次下载失败")


# 批量上传本地文件夹，末尾输出文件夹根访问地址
def upload_directory(client, full_bucket, bucket_region, local_dir: Path, cos_prefix: str):
    if not local_dir.exists():
        raise FileNotFoundError(f"本地文件夹不存在：{local_dir}")
    if not local_dir.is_dir():
        raise NotADirectoryError(f"路径不是文件夹，请使用 upload 单文件上传：{local_dir}")
    if not cos_prefix.endswith("/"):
        cos_prefix += "/"

    uploaded_urls = []
    # 收集所有子目录路径，用于后续创建COS目录占位
    all_subdirs = set()

    for file_path in local_dir.rglob("*"):
        # 收集所有文件夹相对路径
        if file_path.is_dir():
            rel_dir = file_path.relative_to(local_dir)
            dir_key = f"{cos_prefix}{rel_dir}/"
            all_subdirs.add(dir_key)
            if UPLOAD_DIR_SKIP_EMPTY_FOLDER and not any(file_path.iterdir()):
                continue
            continue

        if UPLOAD_DIR_SKIP_SYMLINK and file_path.is_symlink():
            continue

        rel_path = file_path.relative_to(local_dir)
        cos_key = f"{cos_prefix}{rel_path}"

        content_type, _ = mimetypes.guess_type(str(file_path))
        content_type = content_type or "application/octet-stream"
        if content_type.startswith("text/"):
            content_type += "; charset=utf-8"

        client.put_object_from_local_file(
            Bucket=full_bucket,
            LocalFilePath=str(file_path),
            Key=cos_key,
            ContentType=content_type,
            ContentDisposition="inline"
        )
        file_url = PUBLIC_URL_TEMPLATE.format(bucket=full_bucket, region=bucket_region, key=cos_key)
        uploaded_urls.append(file_url)

    # 新增：上传空占位文件，生成COS可识别文件夹
    for dir_key in all_subdirs:
        client.put_object(
            Bucket=full_bucket,
            Key=dir_key,
            Body=b"",
            ContentType="application/x-directory"
        )

    for link in uploaded_urls:
        print(link)
    if not uploaded_urls:
        print("文件夹内无有效文件上传")
    folder_root = PUBLIC_URL_TEMPLATE.format(bucket=full_bucket, region=bucket_region, key=cos_prefix)
    print(f"\n[FolderRoot] {folder_root}")


# 删除云端文件
def delete_file(cos_key: str):
    client = get_cos_client()
    client.delete_object(Bucket=BUCKET, Key=cos_key)
    print(f"云端文件 {cos_key} 删除成功")


# 浏览目录列表
def list_dir(prefix: str, custom_max_count: int = None):
    client = get_cos_client()
    collected = []
    marker = ""
    max_count = custom_max_count if custom_max_count is not None else LIST_MAX_COUNT

    while True:
        resp = client.list_objects(
            Bucket=BUCKET, Prefix=prefix, Marker=marker, MaxKeys=max_count
        )
        contents = resp.get("Contents", [])
        for item in contents:
            key = item["Key"]
            if LIST_NO_RECURSIVE:
                rel_path = key[len(prefix) :]
                if "/" in rel_path:
                    continue
            collected.append(key)
            if len(collected) >= max_count:
                break
        if len(collected) >= max_count or resp["IsTruncated"] == "false":
            break
        marker = resp["NextMarker"]

    for item in collected:
        print(item)
    if len(collected) >= max_count:
        print(f"\n⚠️ 已达最大展示条数 {max_count}，更多文件未加载")


def main():
    args = sys.argv[1:]
    if len(args) < 1:
        raise Exception(
            "缺少操作指令，支持指令：upload / upload-dir / download / download-dir / delete / list"
        )
    cmd = args[0]

    try:
        if cmd == "upload":
            if len(args) != 3:
                raise Exception("上传参数错误：upload 本地文件路径 COS云端路径")
            local_file = Path(args[1])
            cos_save_key = args[2]
            upload_file(local_file, cos_save_key)

        elif cmd == "upload-dir":
            if len(args) != 3:
                raise Exception(
                    "文件夹上传参数错误：upload-dir 本地文件夹路径 COS云端前缀"
                )
            local_folder = Path(args[1])
            cos_root_prefix = args[2]
            upload_directory(get_cos_client(), BUCKET, REGION, local_folder, cos_root_prefix)

        elif cmd == "download":
            if len(args) != 3:
                raise Exception("单文件下载参数错误：download COS云端路径 本地保存路径")
            cos_file_key = args[1]
            local_save = Path(args[2])
            download_file(cos_file_key, local_save)

        elif cmd == "download-dir":
            if len(args) not in (3, 4):
                raise Exception(
                    "目录下载参数错误：download-dir COS目录前缀 本地文件夹 [可选:最大条数]"
                )
            cos_prefix = args[1]
            local_dir = Path(args[2])
            limit = None
            if len(args) == 4:
                try:
                    limit = int(args[3])
                    if limit <= 0:
                        raise ValueError
                except ValueError:
                    raise Exception("最大条数必须为正整数")
            download_directory(cos_prefix, local_dir, limit)

        elif cmd == "delete":
            if len(args) != 2:
                raise Exception("删除参数错误：delete COS云端文件路径")
            cos_file_key = args[1]
            delete_file(cos_file_key)

        elif cmd == "list":
            if len(args) not in (2, 3):
                raise Exception("浏览目录参数错误：list COS目录前缀 [可选:自定义条数]")
            dir_prefix = args[1]
            if not dir_prefix.endswith("/"):
                raise Exception("目录前缀必须以 / 结尾，如 data/")
            custom_limit = None
            if len(args) == 3:
                try:
                    custom_limit = int(args[2])
                    if custom_limit <= 0:
                        raise ValueError()
                except ValueError:
                    raise Exception("自定义条数必须为正整数")
            list_dir(dir_prefix, custom_limit)

        else:
            raise Exception(
                f"不支持的操作指令: {cmd}，可用指令 upload/upload-dir/download/download-dir/delete/list"
            )

    except Exception as err:
        raise Exception(f"COS操作失败：{err!s}") from err


if __name__ == "__main__":
    main()
