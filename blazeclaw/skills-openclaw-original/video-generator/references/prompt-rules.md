# 视频提示词生成规则

## 核心要求

1. **原始实拍素材风格** — 模拟手持摄像机拍摄的真实素材，有轻微抖动、对焦呼吸、自然光线变化等真实摄影特征
2. **聚焦动作描写** — 重点描述主体的动作和行为，动作必须符合真实动物行为学
3. **主体互动** — 如果输入是单个主体（如"老虎"），必须生成多个主体互动的场景（如"两只老虎搏斗"）
4. **声音描述** — 末尾包含动物的声音描述（咆哮、嘶吼、低吼等）
5. **字数限制** — 提示词控制在 100 词以内，简洁有力

## 真实摄影特征（必须包含）

每条提示词必须包含以下真实摄影特征中的 2-3 个，以消除 AI 生成感：
- **手持镜头**：轻微抖动、不稳定、跟拍晃动（handheld, slightly shaky）
- **自然光线**：非打光效果，有光斑、阴影、逆光等自然光照（natural lighting, ambient light）
- **浅景深**：背景虚化、焦点跟随主体（shallow depth of field, bokeh）
- **不完美构图**：主体偶尔偏离中心、画面有呼吸感（imperfect framing）
- **环境噪点**：阴天或弱光下的画面颗粒感（grainy texture in low light）
- **色温自然**：偏冷或偏暖的自然色温，非后期调色（natural color temperature）

## 避免的词汇

不要使用：fantasy, dreamy, magical, perfect, flawless, stunning, epic, cinematic, CGI, digital art, illustration

## 按类别约束

### 动物/生物
- 使用具体的动物亚种名称（如"Siberian Tiger"而非"tiger"）
- 场景必须为自然环境，禁止出现人类、建筑、汽车等非自然元素
- 描述具体的环境状态（天气、时间）
- 场景要有趣味性和戏剧性，避免平淡

### 风景/自然
- 描述完整环境，包含云层、光线、水面、植被等元素
- 注重光影变化和动态效果

### 城市/建筑
- 描述建筑、街道、灯光、交通等元素
- 可以包含行人，但不要特写面部

### 人物
- 描述姿态、服装、动作、表情、场景

### 抽象概念
- 用色彩、光影、直观符号来表达

## 通用约束

1. 主体始终保持在画面中心
2. 直接输出英文提示词，不要解释，不要换行

## 使用示例

| 输入 | LLM 生成的提示词 |
|------|-----------------|
| 老虎 | Two adult Siberian tigers fighting in a rain-soaked jungle, handheld camera with slight shake. Fur muddy and wet, biting and swiping with low growls, mud splashing. Dappled natural light through canopy, shallow depth of field with blurred background. Accompanied by deep roars. |
| 熊猫 | Two giant pandas tumbling in a bamboo forest, handheld tracking with slight wobble. Fur glistening with dew, visible muscle movement during rolls, morning light filtering through bamboo leaves creating light spots. Shallow depth of field, warm natural color temperature. Low grunts and panting. |
| 城市 | Handheld shot of a city street at dusk, wet pavement reflecting streetlights, traffic light trails, slight camera shake. Overcast natural light, cool color temperature, no post-processing. |
| 山川风景 | Handheld wide shot of mountain range, backlit sunset with mist rising, waterfall splashing water droplets, slight breathing motion in frame. Natural light shifts, warm color temperature, visible grain texture. |
