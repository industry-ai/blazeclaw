/* ================================================================
   提醒文本解析器（对齐 agent 项目 personalTaskService.ts）
   ----------------------------------------------------------------
   纯前端正则解析中文时间表达式，将用户消息解析为提醒任务草稿。
   例如 "1分钟后提醒我打卡" -> { title: "打卡", dueAt: 时间戳, delayLabel: "1分钟后" }

   不依赖后端，不依赖 AI，纯本地正则匹配。
   ================================================================ */

var MAX_PERSONAL_REMINDER_DELAY_MS = 1000 * 60 * 60 * 24 * 30; // 30天上限

// 关键词检测：消息中是否包含提醒意图
var READABLE_REMINDER_KEYWORD_RE = /(提醒|叫我|通知|告诉|闹钟|定时|remind me|timer)/iu;

// 群提醒/群任务排除（这些走群任务路径，不创建个人提醒）
var READABLE_GROUP_REMINDER_RE = /(大家|全群|所有人|同学们|家长们|全体|每个人|所有家长|所有同学)/u;
var READABLE_GROUP_TASK_RE = /(通知全群|群公告|全群(作业|活动|公告)|所有人.*(提交|上传|回执)|大家.*(提交|上传|回执))/u;

// 相对时间：1分钟后、半小时后、2小时后、3天后
var READABLE_RELATIVE_REMINDER_RE = /(\d+|半|[零一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后)/u;

// 绝对日期：今天、明天、后天、大后天
var READABLE_DAY_RE = /(今天|明天|后天|大后天)/u;

// 星期：本周三、下周五、周日
var READABLE_WEEK_RE = /(本周|这周|下周)?\s*(?:周|星期|礼拜)([一二三四五六日天1-7])/u;

// 时间：下午3点、晚上8点半、凌晨2点
var READABLE_CLOCK_RE = /(凌晨|早上|上午|中午|下午|晚上|夜里)?\s*(\d{1,2})(?:[：:点时](\d{1,2})?\s*分?)?/u;

var DEFAULT_ABSOLUTE_REMINDER_HOUR = 9;
var DEFAULT_ABSOLUTE_REMINDER_MINUTE = 0;

// ── 去除 @炎图AI助手 前缀（对齐 normalizeMentionText）──
function normalizeMentionText(value) {
  return String(value || '')
    .replace(/\u200B/g, '')
    .replace(/@?[小炎图]+AI助手/g, '')
    .replace(/@?炎图AI助手/g, '')
    .replace(/@?小炎AI助手/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

// ── 中文数字解析 ──
function parseChineseInteger(value) {
  var raw = String(value || '').trim();
  if (!raw) return undefined;
  if (/^\d+$/u.test(raw)) return Number(raw);
  if (raw === '半') return 0.5;

  var digits = { 零: 0, 一: 1, 二: 2, 两: 2, 三: 3, 四: 4, 五: 5, 六: 6, 七: 7, 八: 8, 九: 9 };
  if (raw === '十') return 10;
  var tenIdx = raw.indexOf('十');
  if (tenIdx >= 0) {
    var left = raw.slice(0, tenIdx);
    var right = raw.slice(tenIdx + 1);
    var tens = left ? digits[left] : 1;
    var ones = right ? digits[right] : 0;
    if (tens === undefined || ones === undefined) return undefined;
    return tens * 10 + ones;
  }
  return digits[raw];
}

// ── 时间单位转毫秒 ──
function delayMsForUnit(amount, unit) {
  if (unit.startsWith('秒')) return amount * 1000;
  if (unit === '分' || unit.startsWith('分钟')) return amount * 60 * 1000;
  if (unit === '时' || unit.startsWith('小时')) return amount * 60 * 60 * 1000;
  if (unit.startsWith('天')) return amount * 24 * 60 * 60 * 1000;
  return 0;
}

// ── 清理提醒标题：去掉时间表达式和提醒关键词，剩下纯任务标题 ──
function cleanReminderTitle(text, matchIndex, matchText) {
  var after = text.slice(matchIndex + matchText.length);
  var before = text.slice(0, matchIndex);
  return (after || before)
    .replace(/^(请|帮我|麻烦)?\s*(到时|到时候)?\s*(提醒|叫我|通知|告诉)\s*(一下|一声)?\s*(我)?/u, '')
    .replace(/(请|帮我|麻烦)?\s*(提醒|叫我|通知|告诉)\s*(一下|一声)?\s*(我)?$/u, '')
    .replace(/[，。！？,.、\s]+$/u, '')
    .replace(/^[，。！？,.、\s]+/u, '')
    .trim();
}

// ── 解析相对时间（1分钟后、半小时后）──
function tryParseRelativeReminder(text, nowMs) {
  var match = READABLE_RELATIVE_REMINDER_RE.exec(text);
  if (!match) return null;
  var amount = parseChineseInteger(match[1]);
  if (!amount || amount <= 0) return null;
  var delayMs = delayMsForUnit(amount, match[2]);
  if (!Number.isFinite(delayMs) || delayMs <= 0 || delayMs > MAX_PERSONAL_REMINDER_DELAY_MS) return null;
  var title = cleanReminderTitle(text, match.index, match[0]);
  return {
    title: title || '这件事',
    dueAt: nowMs + delayMs,
    delayMs: delayMs,
    delayLabel: match[0],
  };
}

// ── 解析绝对时间（今天/明天 + 下午3点）──
function parseClockText(text) {
  var match = READABLE_CLOCK_RE.exec(text);
  if (!match) return null;
  var hour = Number(match[2]);
  var minute = match[3] ? Number(match[3]) : 0;
  if (!Number.isFinite(hour) || !Number.isFinite(minute) || hour < 0 || hour > 24 || minute < 0 || minute > 59) return null;

  var period = match[1] || '';
  if ((period === '下午' || period === '晚上' || period === '夜里') && hour < 12) hour += 12;
  if (period === '中午' && hour < 11) hour += 12;
  if (period === '凌晨' && hour === 12) hour = 0;
  if (hour === 24) hour = 0;
  return { hour: hour, minute: minute, matchText: match[0] };
}

function cleanAbsoluteReminderTitle(text, dateText, timeText) {
  var title = text
    .replace(dateText, ' ')
    .replace(/^(请|帮我|麻烦)?\s*(提醒|叫我|通知|告诉)\s*(一下)?\s*我?/u, ' ')
    .replace(/(请|帮我|麻烦)?\s*(提醒|叫我|通知|告诉)\s*(一下)?\s*我?$/u, ' ');
  if (timeText) title = title.replace(timeText, ' ');
  return title.replace(/[，。！？?,.、\s]+$/u, '').replace(/^[，。！？?,.、\s]+/u, '').replace(/\s+/g, ' ').trim();
}

function tryParseAbsoluteReminder(text, nowMs) {
  var dayMatch = READABLE_DAY_RE.exec(text);
  var weekMatch = READABLE_WEEK_RE.exec(text);

  var now = new Date(nowMs);
  var targetDay = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  var dateText = '';

  if (dayMatch) {
    dateText = dayMatch[0];
    switch (dayMatch[1]) {
      case '今天': break;
      case '明天': targetDay.setDate(targetDay.getDate() + 1); break;
      case '后天': targetDay.setDate(targetDay.getDate() + 2); break;
      case '大后天': targetDay.setDate(targetDay.getDate() + 3); break;
    }
  } else if (weekMatch) {
    dateText = weekMatch[0];
    var weekdayMap = { '日': 0, '天': 0, '7': 0, '一': 1, '1': 1, '二': 2, '2': 2, '三': 3, '3': 3, '四': 4, '4': 4, '五': 5, '5': 5, '六': 6, '6': 6 };
    var targetWeekday = weekdayMap[weekMatch[2]];
    if (targetWeekday === undefined) return null;
    var currentWeekday = now.getDay();
    var diff = targetWeekday - currentWeekday;
    if (weekMatch[1] === '下周') diff += 7;
    if (diff <= 0 && !weekMatch[1]) diff += 7; // 本周但已过 -> 下周
    targetDay.setDate(targetDay.getDate() + diff);
  } else {
    return null;
  }

  var clock = parseClockText(text.replace(dateText, ' '));
  var due = new Date(targetDay);
  due.setHours(clock ? clock.hour : DEFAULT_ABSOLUTE_REMINDER_HOUR, clock ? clock.minute : DEFAULT_ABSOLUTE_REMINDER_MINUTE, 0, 0);
  if (due.getTime() <= nowMs) return null;

  var title = cleanAbsoluteReminderTitle(text, dateText, clock ? clock.matchText : null);
  var delayLabel = due.toLocaleString('zh-CN', { month: 'numeric', day: 'numeric', hour: '2-digit', minute: '2-digit' });
  return {
    title: title || '这件事',
    dueAt: due.getTime(),
    delayMs: due.getTime() - nowMs,
    delayLabel: delayLabel,
  };
}

// ── 主入口：解析消息文本，返回提醒草稿（或 null）──
export function tryParseReminderDraft(rawText, nowMs) {
  nowMs = nowMs || Date.now();
  var text = normalizeMentionText(rawText);
  if (!text || !READABLE_REMINDER_KEYWORD_RE.test(text)) return null;

  // 排除群提醒/群任务
  if (READABLE_GROUP_REMINDER_RE.test(text)) return null;
  if (READABLE_GROUP_TASK_RE.test(text)) return null;

  // 优先匹配相对时间
  var relative = tryParseRelativeReminder(text, nowMs);
  if (relative) return relative;

  // 绝对时间
  var absolute = tryParseAbsoluteReminder(text, nowMs);
  if (absolute) return absolute;

  return null;
}

// ── 检测消息是否包含提醒关键词（用于 sendUserMessage 中快速判断）──
export function hasReminderKeyword(text) {
  return READABLE_REMINDER_KEYWORD_RE.test(text);
}

export default { tryParseReminderDraft, hasReminderKeyword };
