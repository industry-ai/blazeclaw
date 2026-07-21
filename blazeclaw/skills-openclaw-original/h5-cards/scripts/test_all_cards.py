#!/usr/bin/env python3
"""Test all 12 card types — both explicit content and random generation."""
import subprocess, json, sys, os, tempfile

RENDER = os.path.join(os.path.dirname(__file__), 'render.py')
OUTDIR = os.path.join(tempfile.gettempdir(), 'h5-cards-test')
os.makedirs(OUTDIR, exist_ok=True)

PASS = 0
FAIL = 0
results = []

def test(label, card_data):
    global PASS, FAIL
    outfile = os.path.join(OUTDIR, label.replace(' ', '_').replace('/', '_') + '.html')
    try:
        proc = subprocess.run(
            [sys.executable, RENDER, outfile],
            input=json.dumps(card_data, ensure_ascii=False).encode('utf-8'),
            capture_output=True, timeout=10
        )
        if proc.returncode == 0 and os.path.isfile(outfile) and os.path.getsize(outfile) > 0:
            size = os.path.getsize(outfile)
            print(f"  [PASS] {label}  ({size} bytes)")
            PASS += 1
            results.append(f"PASS | {label} | {size}B")
            return True
        else:
            err = proc.stderr.decode('utf-8', errors='replace')[:120]
            print(f"  [FAIL] {label}  -> {err}")
            FAIL += 1
            results.append(f"FAIL | {label} | {err}")
            return False
    except Exception as e:
        print(f"  [FAIL] {label}  -> {e}")
        FAIL += 1
        results.append(f"FAIL | {label} | {str(e)}")
        return False

print("=" * 60)
print("H5 Cards — 全量测试 (12 card_types x 2 模式)")
print("=" * 60)

# ================================================================
# 1. h5_entry
# ================================================================
print("\n▶ text-card 系列")

test("h5_entry_传内容_AI入口", {
    "schema_version": "1.0", "card_type": "h5_entry",
    "title": "炎图 AI 助手", "subtitle": "我已加入当前聊天室",
    "description": "可以为你提供智能协同服务，点击查看推荐内容。",
    "button_text": "打开页面", "target_url": "https://www.baidu.com",
    "theme": "general", "layout": {"variant": "h5_entry", "icon": "ai"}
})

test("h5_entry_随机_作文范文", {
    "schema_version": "1.0", "card_type": "h5_entry",
    "title": "作文范文：我的家乡", "subtitle": "点击查看完整内容与老师点评",
    "description": "三年级优秀作文展示，描写家乡的自然风光和人文特色。",
    "button_text": "查看详情", "target_url": "https://example.com/essay",
    "theme": "general", "layout": {"variant": "h5_entry", "icon": "link"}
})

# ================================================================
# 2. assistant_welcome
# ================================================================
test("assistant_welcome_传内容_教育AI", {
    "schema_version": "1.0", "card_type": "assistant_welcome",
    "title": "教育 AI 助手", "subtitle": "已就绪 · 随时为你服务",
    "description": "我是你的专属学习伙伴，可以帮你解答问题、推荐学习内容、管理作业安排。",
    "button_text": "开始对话", "target_url": "https://example.com/edu-ai",
    "theme": "ai", "layout": {"variant": "assistant_welcome", "icon": "ai"}
})

test("assistant_welcome_随机_医疗AI", {
    "schema_version": "1.0", "card_type": "assistant_welcome",
    "title": "医疗 AI 助手", "subtitle": "已就绪 · 李医生随访团队",
    "description": "提供用药提醒、健康建议和随访管理服务。",
    "button_text": "开始对话", "target_url": "https://example.com/medical-ai",
    "theme": "ai", "layout": {"variant": "assistant_welcome", "icon": "ai"}
})

# ================================================================
# 3. recommendation
# ================================================================
test("recommendation_传内容_英语口语", {
    "schema_version": "1.0", "card_type": "recommendation",
    "title": "英语口语 · 餐厅点餐场景", "subtitle": "根据你的学习进度智能推荐",
    "description": "包含常用句型、对话模板和发音指导，适合初级学习者。",
    "button_text": "查看详情", "target_url": "https://example.com/oral",
    "theme": "recommendation", "layout": {"variant": "recommendation", "icon": "audio"}
})

test("recommendation_随机_文章推荐", {
    "schema_version": "1.0", "card_type": "recommendation",
    "title": "科普阅读 · 太阳系八大行星", "subtitle": "根据你的阅读兴趣智能推荐",
    "description": "从水星到海王星，带你了解太阳系的每一颗行星。",
    "button_text": "开始阅读", "target_url": "https://example.com/planets",
    "theme": "recommendation", "layout": {"variant": "recommendation", "icon": "link"}
})

# ================================================================
# 4. task
# ================================================================
test("task_传内容_周报汇总", {
    "schema_version": "1.0", "card_type": "task",
    "title": "班级群 · 教学周报汇总", "subtitle": "由班主任发起 · 三年级组",
    "description": "请各位老师在周五前提交本周教学周报，汇总后将统一上报教务处。",
    "button_text": "查看任务", "target_url": "https://example.com/weekly",
    "theme": "task", "layout": {"variant": "task", "icon": "task"}
})

test("task_随机_班级通知", {
    "schema_version": "1.0", "card_type": "task",
    "title": "家长会通知 · 三年级二班", "subtitle": "由班主任发起 · 下周二晚7点",
    "description": "请各位家长准时参加本学期第二次家长会，地点在三楼多媒体教室。",
    "button_text": "查看详情", "target_url": "https://example.com/meeting",
    "theme": "task", "layout": {"variant": "task", "icon": "task"}
})

# ================================================================
# 5. health_advice
# ================================================================
test("health_advice_传内容_服药提醒", {
    "schema_version": "1.0", "card_type": "health_advice",
    "title": "服药提醒 · 阿莫西林", "subtitle": "医疗 AI 助手 · 李医生随访",
    "description": "每日三次，饭后服用，每次一粒。请勿空腹服用，服药期间避免饮酒。",
    "button_text": "查看建议", "target_url": "https://example.com/med",
    "theme": "health", "layout": {"variant": "health_advice", "icon": "health"}
})

test("health_advice_随机_饮食建议", {
    "schema_version": "1.0", "card_type": "health_advice",
    "title": "饮食建议 · 春季养生", "subtitle": "健康 AI 助手 · 营养科",
    "description": "春季宜食清淡，多摄入新鲜蔬菜水果，适量补充维生素C，保持充足水分摄入。",
    "button_text": "查看详情", "target_url": "https://example.com/diet",
    "theme": "health", "layout": {"variant": "health_advice", "icon": "health"}
})

# ================================================================
# 6. homework_reminder
# ================================================================
print("\n▶ homework-card")

test("homework_传内容_语文", {
    "schema_version": "1.0", "card_type": "homework_reminder",
    "title": "语文 · 阅读理解训练", "subtitle": "张老师 · 三年级二班",
    "description": "阅读课文《荷花》第2-4自然段，完成课后练习题第1-3题，体会作者对荷花的描写手法。",
    "button_text": "查看作业", "target_url": "https://homework.example.com/chinese",
    "theme": "chinese", "layout": {"variant": "homework_reminder", "icon": "chinese"}
})

test("homework_随机_数学", {
    "schema_version": "1.0", "card_type": "homework_reminder",
    "title": "数学 · 第三章分数练习", "subtitle": "王老师 · 三年级二班",
    "description": "完成课本第42-44页分数加减法练习题，重点掌握同分母分数的加减运算方法。",
    "button_text": "查看作业", "target_url": "https://homework.example.com/math",
    "theme": "math", "layout": {"variant": "homework_reminder", "icon": "math"}
})

test("homework_传内容_英语", {
    "schema_version": "1.0", "card_type": "homework_reminder",
    "title": "英语 · Unit 5 My Day", "subtitle": "李老师 · 三年级二班",
    "description": "熟记本单元20个单词，完成Activity Book第28-29页，录制1分钟英语口语视频。",
    "button_text": "查看作业", "target_url": "https://homework.example.com/english",
    "theme": "english", "layout": {"variant": "homework_reminder", "icon": "english"}
})

# ================================================================
# 7. media_preview
# ================================================================
print("\n▶ media-card")

test("media_传内容_视频", {
    "schema_version": "1.0", "card_type": "media_preview",
    "title": "课堂录像 · 分数的加减法", "subtitle": "42:18",
    "description": "张老师 · 2026/05/24 上传 · MP4 · 1080p · 1.2 GB · 第三章重难点讲解。",
    "button_text": "播放视频",
    "target_url": "https://media.bjnews.com.cn/video/out/2025/09/03/5627281659826089570.m3u8",
    "theme": "video", "layout": {"variant": "media_preview", "icon": "video"}
})

test("media_随机_音频", {
    "schema_version": "1.0", "card_type": "media_preview",
    "title": "英语听力 · Unit 5 课文朗读", "subtitle": "03:25",
    "description": "李老师 · 2026/06/10 上传 · MP3 · 128kbps · 4.2 MB · 英式发音原声朗读。",
    "button_text": "播放音频", "target_url": "https://media.example.com/audio/unit5",
    "theme": "video", "layout": {"variant": "media_preview", "icon": "audio"}
})

test("media_传内容_图片", {
    "schema_version": "1.0", "card_type": "media_preview",
    "title": "课堂板书 · 分数概念图解", "subtitle": "",
    "description": "王老师 · 2026/06/08 上传 · PNG · 2048×1536 · 2.1 MB · 课堂手写板书。",
    "button_text": "查看图片", "target_url": "https://media.example.com/image/board",
    "theme": "video", "layout": {"variant": "media_preview", "icon": "image"}
})

test("media_随机_文件", {
    "schema_version": "1.0", "card_type": "media_preview",
    "title": "教学资料 · 期末复习提纲", "subtitle": "",
    "description": "教务处 · 2026/06/01 上传 · PDF · 12页 · 3.5 MB · 三年级数学期末复习要点。",
    "button_text": "下载文件", "target_url": "https://media.example.com/file/review",
    "theme": "video", "layout": {"variant": "media_preview", "icon": "file"}
})

# ================================================================
# 8. english_word
# ================================================================
print("\n▶ english-word-card")

test("english_word_传内容_apple", {
    "schema_version": "1.0", "card_type": "english_word",
    "title": "ABC · 字母启蒙", "subtitle": "apple", "description": "苹果",
    "button_text": "单词发音",
    "target_url": "https://works.blazegraph.site/works/e02656c2-d563-4ca2-8406-33031d109b48/2-1-6-4-1779693877148/images/img16.png",
    "theme": "abc", "layout": {"variant": "english_word", "icon": "abc"}
})

test("english_word_传内容_banana", {
    "schema_version": "1.0", "card_type": "english_word",
    "title": "ABC · 字母启蒙", "subtitle": "banana", "description": "香蕉",
    "button_text": "单词发音",
    "theme": "abc", "layout": {"variant": "english_word", "icon": "abc"}
})

test("english_word_随机1_cat", {
    "schema_version": "1.0", "card_type": "english_word",
    "title": "ABC · 字母启蒙", "subtitle": "cat", "description": "猫",
    "button_text": "单词发音",
    "theme": "abc", "layout": {"variant": "english_word", "icon": "abc"}
})

test("english_word_随机2_rabbit", {
    "schema_version": "1.0", "card_type": "english_word",
    "title": "ABC · 字母启蒙", "subtitle": "rabbit", "description": "兔子",
    "button_text": "单词发音",
    "theme": "abc", "layout": {"variant": "english_word", "icon": "abc"}
})

test("english_word_随机3_sun", {
    "schema_version": "1.0", "card_type": "english_word",
    "title": "ABC · 字母启蒙", "subtitle": "sun", "description": "太阳",
    "button_text": "单词发音",
    "theme": "abc", "layout": {"variant": "english_word", "icon": "abc"}
})

# ================================================================
# 9. english_sentence
# ================================================================
print("\n▶ english-sentence-card")

test("english_sentence_传内容_励志", {
    "schema_version": "1.0", "card_type": "english_sentence",
    "title": "每日一句",
    "subtitle": "The best preparation for tomorrow is doing your best today.",
    "description": "为明天做的最好准备，就是今天做到最好。",
    "button_text": "句子发音",
    "theme": "sentence", "layout": {"variant": "english_sentence", "icon": "sentence"}
})

test("english_sentence_随机1_励志", {
    "schema_version": "1.0", "card_type": "english_sentence",
    "title": "每日一句",
    "subtitle": "Success is not final, failure is not fatal: it is the courage to continue that counts.",
    "description": "成功不是终点，失败也不是末日：重要的是继续前进的勇气。",
    "button_text": "句子发音",
    "theme": "sentence", "layout": {"variant": "english_sentence", "icon": "sentence"}
})

test("english_sentence_随机2_日常", {
    "schema_version": "1.0", "card_type": "english_sentence",
    "title": "每日一句",
    "subtitle": "A journey of a thousand miles begins with a single step.",
    "description": "千里之行，始于足下。",
    "button_text": "句子发音",
    "theme": "sentence", "layout": {"variant": "english_sentence", "icon": "sentence"}
})

test("english_sentence_随机3_校园", {
    "schema_version": "1.0", "card_type": "english_sentence",
    "title": "每日英语",
    "subtitle": "Reading books opens up a whole new world.",
    "description": "读书开启了一个全新的世界。",
    "button_text": "句子发音",
    "theme": "sentence", "layout": {"variant": "english_sentence", "icon": "sentence"}
})

# ================================================================
# 10. english_sentence_input
# ================================================================
print("\n▶ english-input-card")

test("english_input_传内容_预填", {
    "schema_version": "1.0", "card_type": "english_sentence_input",
    "title": "每日一句",
    "subtitle": "I have a dream that one day all children will have access to quality education.",
    "description": "我有一个梦想，有一天所有孩子都能获得优质教育。",
    "button_text": "句子发音",
    "theme": "sentence", "layout": {"variant": "english_sentence_input", "icon": "sentence"}
})

test("english_input_随机_空白v11", {
    "schema_version": "1.0", "card_type": "english_sentence_input",
    "button_text": "句子发音",
    "theme": "sentence", "layout": {"variant": "english_sentence_input", "icon": "sentence"}
})

# ================================================================
# 11. comic_strip
# ================================================================
print("\n▶ comic-card")

test("comic_传内容_Unit1_Pets", {
    "schema_version": "1.0", "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 1 Pets", "subtitle": "Meet My Little Friends",
    "description": "Watch the video and learn how to introduce your pets.",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit1_activity1.mp4",
    "theme": "comic",
    "frames": [
        {"image": "../assets/image/unit1_panel1.png", "texts": ["It's Yaya. It's my rabbit."]},
        {"image": "../assets/image/unit1_panel2.png", "texts": ["It's Maomao. It's my cat."]},
        {"image": "../assets/image/unit1_panel3.png", "texts": ["It's Dora. It's my bird."]},
        {"image": "../assets/image/unit1_panel4.png", "texts": ["It's Snowball. It's my dog."]},
        {"image": "../assets/image/unit1_panel5.png", "texts": ["And it's Orange. It's my fish!"]}
    ],
    "layout": {"variant": "comic_strip", "icon": "comic"}
})

test("comic_随机1_Unit3_Face", {
    "schema_version": "1.0", "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 3 Face", "subtitle": "We Are Twins!",
    "description": "Practice describing facial features with Meimei and Feifei!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit3_activity1.mp4",
    "theme": "comic",
    "frames": [
        {"image": "../assets/image/unit3_panel1.png", "texts": ["Hi, I'm Meimei.", "I'm Feifei.", "We are twins."]},
        {"image": "../assets/image/unit3_panel2.png", "texts": ["I have big eyes.", "Me too."]},
        {"image": "../assets/image/unit3_panel3.png", "texts": ["I have a small nose.", "Me too."]},
        {"image": "../assets/image/unit3_panel4.png", "texts": ["I have a small mouth.", "Me too."]},
        {"image": "../assets/image/unit3_panel5.png", "texts": ["We look the same.", "I know!"]},
        {"image": "../assets/image/unit3_panel6.png", "texts": ["Now I have long hair.", "I have short hair.", "We are different now."]}
    ],
    "layout": {"variant": "comic_strip", "icon": "comic"}
})

test("comic_随机2_Unit6_Time", {
    "schema_version": "1.0", "card_type": "comic_strip",
    "title": "PEP外研版 · Unit 6 Time", "subtitle": "Happy Birthday, Dad!",
    "description": "Learn to talk about daily routines and time. Join Toby!",
    "button_text": "查看完整内容",
    "video_url": "../assets/video/unit6_activity1.mp4",
    "theme": "comic",
    "frames": [
        {"image": "../assets/image/unit6_panel1.png", "texts": ["Happy birthday, Dad!", "Thank you, Toby!"]},
        {"image": "../assets/image/unit6_panel2.png", "texts": ["Let's have lunch together!", "OK!"]},
        {"image": "../assets/image/unit6_panel3.png", "texts": ["Dad is not home yet."]},
        {"image": "../assets/image/unit6_panel4.png", "texts": ["Dad is busy.", "I know!"]},
        {"image": "../assets/image/unit6_panel5.png", "texts": ["Thank you!"]}
    ],
    "layout": {"variant": "comic_strip", "icon": "comic"}
})

# ================================================================
# 12. qa_answer
# ================================================================
print("\n▶ answer-card")

test("qa_answer_传内容_建筑规范", {
    "schema_version": "1.0", "card_type": "qa_answer",
    "title": "公司餐补标准是多少？",
    "description": "根据公司《员工福利管理制度》第三章第2条规定，员工餐补标准为每人每天30元，按月随工资发放。出差期间餐补标准调整为每人每天80元，需提供有效发票报销。",
    "sources": ["[hr] 员工福利管理制度.docx", "[finance] 差旅费报销标准.pdf"]
})

test("qa_answer_传内容_技术问题", {
    "schema_version": "1.0", "card_type": "qa_answer",
    "title": "系统微服务架构是怎样的？",
    "description": "系统采用基于Spring Cloud的微服务架构，包含用户服务、订单服务、商品服务、支付服务四个核心模块。各服务通过API Gateway统一接入，使用Nacos作为注册中心和配置中心，通过Feign进行服务间调用。",
    "sources": ["[architecture] 系统架构设计.md", "[dev] API接口文档.pdf", "[ops] 部署运维手册.txt"]
})

test("qa_answer_随机_纯文本", {
    "schema_version": "1.0", "card_type": "qa_answer",
    "title": "什么是人工智能？",
    "description": "人工智能（Artificial Intelligence，简称AI）是计算机科学的一个分支，旨在创建能够模拟人类智能的系统。它包括机器学习、自然语言处理、计算机视觉等多个子领域，广泛应用于医疗诊断、自动驾驶、智能助手等场景。"
})

test("qa_answer_随机_空回答", {
    "schema_version": "1.0", "card_type": "qa_answer",
    "title": "公司今年的旅游计划是什么？"
})

# ================================================================
# Results
# ================================================================
print("\n" + "=" * 60)
print("  测试结果汇总")
print("=" * 60)
for r in results:
    print(f"  {r}")
print()
print(f"PASS: {PASS}  FAIL: {FAIL}  TOTAL: {PASS + FAIL}")
print(f"输出目录: {OUTDIR}")
print()

if FAIL > 0:
    print(f"❌ 有 {FAIL} 个测试失败")
    sys.exit(1)
else:
    print(f"✅ 全部 {PASS} 个测试通过")
    sys.exit(0)
