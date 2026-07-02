# Card Schema Reference — 卡片 JSON 构造规则

每种 `card_type` 的 JSON 模板、字段说明和构造规则。

---

## text-card 系列

适用于 `h5_entry`、`assistant_welcome`、`recommendation`、`task`、`health_advice`，共享同一 JSON 结构。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "<按意图填入>",
  "title": "<卡片标题>",
  "subtitle": "<灰色副标题，可省略>",
  "description": "<卡片正文描述>",
  "button_text": "<按钮文案>",
  "target_url": "<点击跳转 URL>",
  "theme": "<主题名>",
  "layout": {
    "variant": "<同 card_type>",
    "icon": "<图标名>"
  }
}
```

### card_type → theme/icon 对照表

| card_type | theme | icon | 按钮默认文案 | 典型场景 |
|-----------|-------|------|------------|---------|
| `h5_entry` | `general` | `ai` 或 `link` | 打开页面 | H5 页面入口、作文范文链接 |
| `assistant_welcome` | `ai` | `ai` | 开始对话 | AI 助手欢迎页（教育/医疗等） |
| `recommendation` | `recommendation` | `audio` 或 `link` | 查看详情 | 内容推荐（英语口语、文章等） |
| `task` | `task` | `task` | 查看任务 | 任务协作（周报汇总、班级通知等） |
| `health_advice` | `health` | `health` | 查看建议 | 健康建议（用药提醒、饮食建议等） |

### 构造规则

- `title`、`subtitle`、`description`、`button_text`：根据用户上下文动态生成，贴合实际场景
- `target_url`：如果有具体链接则填入，否则使用占位 URL `https://www.baidu.com`
- `layout.variant`：与 `card_type` 保持一致
- `layout.icon`：按上表选择

---

## homework-card（homework_reminder）

作业提醒卡片，科目色横幅 + 作业详情。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "homework_reminder",
  "title": "<科目 · 作业标题>",
  "subtitle": "<老师 · 班级>",
  "description": "<作业详细说明>",
  "button_text": "查看作业",
  "target_url": "<作业链接>",
  "theme": "<chinese | math | english>",
  "layout": {
    "variant": "homework_reminder",
    "icon": "<chinese | math | english>"
  }
}
```

### theme/icon → 科目对照表

| theme | 科目 | 品牌色 | 作业示例标题 |
|-------|------|--------|------------|
| `chinese` | 语文 | 红 #dc2626 | 语文 · 阅读理解训练 |
| `math` | 数学 | 蓝 #3b82f6 | 数学 · 第三章分数练习 |
| `english` | 英语 | 绿 #16a34a | 英语 · Unit 5 My Day |

### 构造规则

- `title`：格式 `<科目> · <具体作业名称>`，如"语文 · 课文《荷花》赏析"
- `subtitle`：格式 `<老师姓氏>老师 · <班级>`，如"张老师 · 三年级二班"
- `description`：作业的具体要求，2-3 句话描述
- `theme` 和 `layout.icon`：根据科目选择
- 缺少信息时合理编造典型内容

---

## media-card（media_preview）

媒体预览卡片，深色预览区 + 播放按钮 + 时长标签 + 媒体信息。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "media_preview",
  "title": "<媒体标题>",
  "subtitle": "<时长，格式 MM:SS>",
  "description": "<上传者 · 日期 · 格式 · 大小 · 其他信息>",
  "button_text": "<播放按钮文案>",
  "target_url": "<媒体资源 URL>",
  "theme": "video",
  "layout": {
    "variant": "media_preview",
    "icon": "<video | audio | image | file>"
  }
}
```

### icon → 媒体类型对照表

| icon | 媒体类型 | 类型标签 | 按钮文案 |
|------|---------|---------|---------|
| `video` | 视频 | 视频 | 播放视频 |
| `audio` | 音频 | 音频 | 播放音频 |
| `image` | 图片 | 图片 | 查看图片 |
| `file` | 文件 | 文件 | 下载文件 |

### 构造规则

- `theme` 固定 `"video"`（所有媒体类型通用 indigo 配色）
- `subtitle` 用于显示时长标签，格式 `MM:SS`（视频/音频），图片/文件可省略
- `description`：格式 `<上传者> · <日期> · <格式> · <大小> · <描述>`，中间用 ` · ` 分隔
- `target_url`：媒体资源地址，如无可使用示例视频 `https://media.bjnews.com.cn/video/out/2025/09/03/5627281659826089570.m3u8`

---

## english-word-card（english_word）

英语单词启蒙卡片，笔记本横线纸背景 + 便利贴卡片（胶带 + 缎带装饰），左侧大字母 + 右侧单词，点击发音。

### 词汇表

```
apple, banana, cat, dog, egg, fish, girl, house, ice, jump,
kite, lion, milk, nose, orange, pig, queen, rabbit, sun, tree,
umbrella, van, water, box, yellow, zoo, book, bird, cake, duck,
eye, foot, goat, hand, key, lamp, moon, pen, rain, star, toy
```

### 中文释义对照

```
apple→苹果, banana→香蕉, cat→猫, dog→狗, egg→鸡蛋, fish→鱼,
girl→女孩, house→房子, ice→冰, jump→跳, kite→风筝, lion→狮子,
milk→牛奶, nose→鼻子, orange→橙子, pig→猪, queen→女王,
rabbit→兔子, sun→太阳, tree→树, umbrella→雨伞, van→货车,
water→水, box→盒子, yellow→黄色, zoo→动物园, book→书,
bird→鸟, cake→蛋糕, duck→鸭子, eye→眼睛, foot→脚, goat→山羊,
hand→手, key→钥匙, lamp→灯, moon→月亮, pen→笔, rain→雨,
star→星星, toy→玩具
```

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "english_word",
  "title": "ABC · 字母启蒙",
  "subtitle": "<英文单词>",
  "description": "<中文释义>",
  "button_text": "单词发音",
  "target_url": "<图片 URL，由 image-generator 生成>",
  "theme": "abc",
  "layout": {
    "variant": "english_word",
    "icon": "abc"
  }
}
```

### 构造规则

- `subtitle`：填入英文单词本身（如 "apple"），渲染时首字母自动放大为左侧大字母
- `description`：填入中文释义（如 "苹果"）
- **用户指定单词**：当用户说"学习XX单词的卡片"、"XX单词卡片"时，直接提取指定单词（如"苹果"→apple、"猫"→cat），按释义表匹配，必须在词汇表中
- **未指定单词**：从词汇表中随机选取一个
- `title`：固定 `"ABC · 字母启蒙"`
- `theme`：固定 `"abc"`
- `target_url`：**必须先调用 image-generator 生成单词图片**（见 SKILL.md 步骤 0），不可省略或使用占位图

---

## english-sentence-card（english_sentence）

英语句子展示卡片，便利贴卡片 + 缎带装饰，英文句子 + 中文翻译（点击显示），支持发音和跟读评分。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "english_sentence",
  "title": "<缎带标题>",
  "subtitle": "<英文句子>",
  "description": "<中文翻译>",
  "button_text": "句子发音",
  "theme": "sentence",
  "layout": {
    "variant": "english_sentence",
    "icon": "sentence"
  }
}
```

### 构造规则

- `title`：缎带标题，默认 `"每日一句"`，也可用 `"今日英语"`、`"每日英语"` 等
- `subtitle`：英文句子（渲染为卡片主体英文文本，左对齐，可多行）
- `description`：中文翻译，默认隐藏，点击后淡入显示 3 秒
- `button_text`：默认 `"句子发音"`
- `theme`：固定 `"sentence"`（蓝色 #2563eb）

### 英语句子素材库（随机选取或根据场景生成）

**励志类：**
```
The best preparation for tomorrow is doing your best today. → 为明天做的最好准备，就是今天做到最好。
The only way to do great work is to love what you do. → 成就伟大的唯一途径是热爱你所做的事。
Don't watch the clock; do what it does. Keep going. → 不要盯着时钟看，要像它一样永不停歇。
Success is not final, failure is not fatal: it is the courage to continue that counts. → 成功不是终点，失败也不是末日：重要的是继续前进的勇气。
```

**日常类：**
```
A journey of a thousand miles begins with a single step. → 千里之行，始于足下。
Practice makes perfect. → 熟能生巧。
Where there is a will, there is a way. → 有志者事竟成。
Every cloud has a silver lining. → 黑暗中总有一线光明。
Actions speak louder than words. → 行动胜于言语。
Knowledge is power. → 知识就是力量。
```

**校园类：**
```
Let's be friends forever. → 让我们永远做朋友吧。
I like playing basketball with my classmates. → 我喜欢和同学们一起打篮球。
Reading books opens up a whole new world. → 读书开启了一个全新的世界。
My favorite subject is science because it's fun. → 我最喜欢的科目是科学，因为它很有趣。
```

---

## english-input-card（english_sentence_input）

英语句子输入卡片，可编辑文本框 + 实时 AI 翻译（DeepSeek API），支持发音和跟读评分。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "english_sentence_input",
  "button_text": "句子发音",
  "theme": "sentence",
  "layout": {
    "variant": "english_sentence_input",
    "icon": "sentence"
  }
}
```

### 构造规则

- v1.1 版本中 `title`、`subtitle`、`description` 均已废弃（缎带已移除，文本框空白由用户输入）
- `button_text`：默认 `"句子发音"`
- `theme`：固定 `"sentence"`（橙色 #ea580c）
- 翻译由前端 DeepSeek API 实时完成，无需在 JSON 中预填内容
- **重要**：即使传入 `title`/`subtitle`/`description` 也会被忽略

---

## comic-card（comic_strip）

漫画卡片，6 个单元标签页 + 视频区 + 分页漫画面板 + 气泡对话 + 触摸/键盘导航。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "comic_strip",
  "title": "<单元标题>",
  "subtitle": "<单元副标题>",
  "description": "<单元简介>",
  "button_text": "开始阅读",
  "video_url": "<单元活动视频 URL>",
  "theme": "comic",
  "frames": [
    {
      "image": "<漫画帧图片 URL>",
      "texts": ["<气泡1>", "<气泡2>", "<气泡3>"]
    }
  ],
  "layout": {
    "variant": "comic_strip",
    "icon": "comic"
  }
}
```

### 可用单元（PEP 外研版，从 h5-cards/templates/comic-card/data.json 选取）

| Unit | 标题 | 主题 | 帧数 |
|------|------|------|------|
| 1 | Meet My Little Friends | 宠物 | 5 |
| 2 | A Day at the Zoo | 动物 | 6 |
| 3 | We Are Twins! | 脸部 | 6 |
| 4 | My Robot Monster | 身体 | 4 |
| 5 | Tidy Up Time | 家庭 | 6 |
| 6 | Happy Birthday, Dad! | 时间 | 5 |

### 构造规则

- 读取 `h5-cards/templates/comic-card/data.json`，该文件是包含 6 个单元对象的 JSON 数组
- 根据用户意图选取对应单元（如用户说"动物漫画"选 Unit 2，"家庭漫画"选 Unit 5）
- 用户未指定时随机选取一个单元
- 将选中单元对象直接作为卡片 JSON（所有字段已就绪）
- `frames[].texts` 数组：0-3 个对话字符串
  - `texts[0]` → 左上蓝色气泡
  - `texts[1]` → 右上粉色气泡
  - `texts[2]` → 底部绿色气泡

---

## answer-card（qa_answer）

知识问答结果卡片，精简版，无头部/无输入栏，仅展示 AI 回答和引用来源。

### JSON 模板

```json
{
  "schema_version": "1.0",
  "card_type": "qa_answer",
  "title": "<问题标题>",
  "description": "<AI 回答正文>",
  "sources": ["<分类> 文件名.扩展名"]
}
```

### 构造规则

- `title`：用户提出的问题，用作卡片标题
- `description`：AI 基于知识库生成的回答正文，支持多段落
- `sources`（可选）：引用来源数组，格式 `[分类] 文件名.扩展名`，如 `["[architecture] 系统架构设计.md", "[hr] 员工手册.docx"]`
- 如果回答内容为空，卡片将展示"未找到相关内容"
- 回答来源文件按扩展名着色（md→蓝, docx→青, pptx→黄, pdf→红, xlsx→绿, txt→灰）
