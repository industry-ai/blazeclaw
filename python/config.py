from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).resolve().parent
# 模型存放目录
MODEL_DIR = PROJECT_ROOT / "models"
# sherpa-onnx 流式 Zipformer CTC 模型目录（中文 int8 量化版）
SHERPA_MODEL_DIR = MODEL_DIR / "sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30"

# 音频采样率（Hz），16kHz 是语音识别的标准采样率
SAMPLE_RATE = 16000
# 声道数，1 表示单声道
CHANNELS = 1
# 每次读取音频块的时长（秒），0.1 秒 = 100ms
BLOCK_SECONDS = 0.1
# 每个音频块的采样点数
BLOCK_SIZE = int(SAMPLE_RATE * BLOCK_SECONDS)

# sherpa-onnx 模型类型："zipformer2_ctc" 或 "transducer"
SHERPA_MODEL_TYPE = "transducer"
# 词表文件路径
#SHERPA_TOKENS = str(SHERPA_MODEL_DIR / "tokens.txt")
# CTC 模型文件路径（int8 量化）
SHERPA_CTC_MODEL = ""



# transducer 模型文件路径（如使用 transducer 模型需配置）
SHERPA_ENCODER = "models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/encoder-epoch-99-avg-1.onnx"
SHERPA_DECODER = "models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/decoder-epoch-99-avg-1.onnx"
SHERPA_JOINER = "models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/joiner-epoch-99-avg-1.onnx"
SHERPA_TOKENS = "models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/tokens.txt"

# 特征维度（滤波器组维度），Zipformer 默认 80
FEATURE_DIM = 80
# ONNX 推理线程数
NUM_THREADS = 2
# 推理后端，"cpu" 表示只用 CPU
PROVIDER = "cuda"
# 解码方法，"greedy_search" 为贪心搜索
DECODING_METHOD = "greedy_search"
# 最大活跃路径数（用于 modified beam search）
MAX_ACTIVE_PATHS = 4
# 建模单元，"cjkchar" 表示按中日韩字符建模
MODELING_UNIT = "cjkchar"
# BPE 词表路径（当前未使用）
BPE_VOCAB = ""
# 是否开启 sherpa-onnx 调试输出
DEBUG = False

# 是否启用端点检测（一句话结束检测）
ENABLE_ENDPOINT_DETECTION = True
# 端点检测规则 1：不含非静音时的最小尾部静音时长（秒）
RULE1_MIN_TRAILING_SILENCE = 2.4
# 端点检测规则 2：包含非静音后的最小尾部静音时长（秒）
RULE2_MIN_TRAILING_SILENCE = 1.2
# 端点检测规则 3：最大一句话时长（秒）
RULE3_MIN_UTTERANCE_LENGTH = 20.0

# 麦克风设备号，None 表示使用系统默认设备
MIC_DEVICE = None
# 音频队列读取超时时间（秒）
QUEUE_TIMEOUT_SECONDS = 1.0
