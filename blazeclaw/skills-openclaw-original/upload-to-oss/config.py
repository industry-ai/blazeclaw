# ==========================================
# 你只需要设置下面这 4 个环境变量
# # Windows cmd
# set COS_SECRET_ID=xxx
# set COS_SECRET_KEY=xxx
# set COS_REGION=ap-beijing
# set COS_BUCKET=mybucket-1250000000
# # Linux/macOS
# export COS_SECRET_ID=xxx
# export COS_SECRET_KEY=xxx
# export COS_REGION=ap-beijing
# export COS_BUCKET=mybucket-1250000000
# ==========================================

import os

# 1. 腾讯云 SecretId
SECRET_ID = os.getenv("COS_SECRET_ID", "")
# 2. 腾讯云 SecretKey
SECRET_KEY = os.getenv("COS_SECRET_KEY", "")
# 3. 存储桶地域，例如 ap-beijing、ap-shanghai、ap-guangzhou
REGION = os.getenv("COS_REGION", "ap-guangzhou")
# 4. 存储桶名称，格式通常是 bucketName-123456789
BUCKET = os.getenv("COS_BUCKET", "")
# ========== list 目录浏览配置 ==========
LIST_MAX_COUNT = 20  # 默认单次最多展示文件条数
LIST_NO_RECURSIVE = True  # True=只看单层，不递归子文件夹

# ========== 下载配置 ==========
AUTO_CREATE_LOCAL_DIR = True  # 下载时自动创建本地不存在的文件夹
DOWNLOAD_MAX_RETRY = 5  # 断点续传最大重试次数

# ========== 文件夹上传配置 ==========  
UPLOAD_DIR_SKIP_SYMLINK = True  # 跳过软链接
UPLOAD_DIR_SKIP_EMPTY_FOLDER = True  # 忽略空目录

# ========== 公开链接模板（占位符 {bucket} {region} {key}） ==========
PUBLIC_URL_TEMPLATE = "https://{bucket}.cos.{region}.myqcloud.com/{key}"
