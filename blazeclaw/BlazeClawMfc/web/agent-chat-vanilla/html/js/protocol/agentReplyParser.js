/* ================================================================
   AI 回复解析模块
   ----------------------------------------------------------------
   对齐 agent 项目：
   - openclawAgentProvider.ts: URL 提取、JSON 解析、附件去重
   - inlineWebviewAttachments.ts: 内联 URL 附件提取与文本清理
   - localAgentRequestService.ts: sanitizeAgentVisibleReply 回复清洗

   核心职责：
   1. 从 AI 回复文本中提取 URL（markdown 链接 + 裸链接）作为附件
   2. 从回复文本中移除已提取为附件的 URL，保持文本干净
   3. 清洗 AI 回复文本（去除思考过程、工具细节、乱码等）
   ================================================================ */

// ── URL 工具函数（对齐 cleanRawUrl.ts）──

/**
 * 清理从 AI 文本中提取的原始 URL：
 * 去除 markdown 强调标记、尾部标点，提取 .html URL
 */
export function cleanRawUrl(rawUrl) {
  const trimmed = String(rawUrl || '')
    .replace(/^[*~_`]+/, '')
    .replace(/[)\].,，。！？、`*~_]+$/u, '')
    .trim();
  const htmlMatch = trimmed.match(/^https?:\/\/.+?\.html?(?:[?#][^\s<>"']*)?/iu);
  if (htmlMatch) return htmlMatch[0];
  return trimmed;
}

// ── URL 类型检测（对齐 openclawAgentProvider.ts）──

export function isHttpUrl(value) {
  if (!/^https?:\/\/[^\s<>"'，。；、）)]+$/iu.test(value)) return false;
  try {
    const parsed = new URL(value);
    const hostname = parsed.hostname.toLowerCase();
    if (hostname === 'localhost') return true;
    if (/^\d{1,3}(?:\.\d{1,3}){3}$/.test(hostname)) return true;
    if (hostname.includes(':')) return true;
    return hostname.includes('.');
  } catch {
    return false;
  }
}

export function isImageUrl(url) {
  try {
    return /\.(?:png|jpe?g|gif|webp|svg|bmp|avif)$/i.test(new URL(url).pathname);
  } catch {
    return false;
  }
}

export function isHtmlUrl(url) {
  try {
    return /\.html?$/i.test(new URL(url).pathname);
  } catch {
    return false;
  }
}

export function isGeneratedHtmlCardUrl(url) {
  try {
    const parsed = new URL(url);
    const fileName = decodeURIComponent(parsed.pathname.split('/').filter(Boolean).pop() ?? '').toLowerCase();
    return /\.html?$/.test(fileName) && /(?:^|[_-])card(?:[_-]|\.|$)/.test(fileName);
  } catch {
    return false;
  }
}

export function isGeneratedH5CardUrl(url) {
  try {
    const parsed = new URL(url);
    const path = decodeURIComponent(parsed.pathname).toLowerCase();
    const fileName = path.split('/').filter(Boolean).pop() ?? '';
    if (/\.html?$/.test(fileName) && /(?:^|[_-])card(?:[_-]|\.|$)/.test(fileName)) return true;
    return /\/(?:homework|assignment|h5|card)(?:\/|-|_)/i.test(path) && /\.html?$/.test(fileName);
  } catch {
    return false;
  }
}

export function titleFromUrl(url) {
  if (!url) return '生成结果';
  try {
    const parsed = new URL(url);
    const fileName = parsed.pathname.split('/').filter(Boolean).pop();
    return fileName ? decodeURIComponent(fileName) : parsed.hostname;
  } catch {
    return '生成结果';
  }
}

// ── 附件去重（对齐 openclawAgentProvider.ts dedupeAttachments）──

export function normalizeUrlForDedup(url) {
  try {
    const parsed = new URL(url);
    parsed.hash = '';
    return parsed.toString().replace(/[*~_]+$/, '');
  } catch {
    return String(url || '').trim().replace(/[*~_]+$/, '');
  }
}

export function dedupeAttachments(attachments) {
  const seen = new Set();
  return (attachments || []).filter((att) => {
    const key = (att.type === 'webview' || att.type === 'image')
      ? `resource:${normalizeUrlForDedup(att.url)}`
      : att.type === 'native_post'
        ? `native_post:${att.postId}`
        : `${att.type}:${att.url || att.title || ''}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

// ── 正则常量（对齐 inlineWebviewAttachments.ts）──

// Markdown 图片链接: ![label](url)
const MARKDOWN_IMAGE_RE = /!\[([^\]\n]{0,80})\]\((https?:\/\/[^\s)]+)\)/gi;
// Markdown 链接 [label](url) 或裸 URL
const MARKDOWN_LINK_RE = /\[([^\]\n]{1,80})\]\((https?:\/\/[^\s)]+)\)|https?:\/\/[^\s<>)，。！？、\u3000-\u303f一-鿿]+/gi;

// ── H5 卡片检测（对齐 inlineWebviewAttachments.ts）──

function looksLikeH5CardText(text) {
  return /(?:H5\s*)?卡片|作业|homework|assignment|card/i.test(String(text || ''));
}

function isGeneratedH5Attachment(url, title, context) {
  if (isGeneratedH5CardUrl(url)) return true;
  return isHtmlUrl(url) && looksLikeH5CardText(`${title}\n${context}`);
}

// ── 从上下文文本提取标题（对齐 openclawAgentProvider.ts titleFromReplyLink / titleFromGeneratedCardContext）──

/**
 * 从 URL 前方文本中提取生成卡片的标题
 * 匹配模式如："樱花大冒险卡片已生成"、"最终路演卡片"等
 */
function titleFromGeneratedCardContext(beforeUrl, url) {
  const fileName = titleFromUrl(url).toLowerCase();
  if (fileName !== 'index.html' && !isGeneratedHtmlCardUrl(url)) return '';

  const lines = beforeUrl
    .split(/\r?\n/)
    .map((line) => line.trim().replace(/[：:，,。.!！?？]+$/u, ''))
    .filter(Boolean);

  for (const line of [...lines].reverse()) {
    // 匹配 "XX卡片已生成/已完成" 模式
    const explicit = line.match(/([一-鿿A-Za-z0-9 _-]{2,40}(?:H5\s*)?卡片)(?:已|已经|生成|可访问|完成)/u);
    if (explicit?.[1]) return explicit[1].trim();
    // 匹配 "最终XX卡片" 模式
    const finalCard = line.match(/最终([一-鿿A-Za-z0-9 _-]{2,32}卡片)/u);
    if (finalCard?.[1]) return finalCard[1].trim();
  }

  return '';
}

/**
 * 从 URL 前方文本中提取游戏/活动名称
 * 匹配模式如："已为您打开樱花大冒险"、"樱花大冒险已打开"等
 */
function titleFromGameContext(beforeUrl) {
  const lines = beforeUrl
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);

  for (const line of [...lines].reverse()) {
    // 匹配 "已为您打开XX" / "已打开XX" / "为您打开XX"
    const openPattern = line.match(/(?:已为您|已|为您)?打开([一-鿿A-Za-z0-9 _·-]{2,40})(?:[，。！？,!?]|$|，请|，可|，访问)/u);
    if (openPattern?.[1]) return openPattern[1].trim();
    // 匹配 "XX已打开" / "XX已生成" / "XX已开启"
    const donePattern = line.match(/([一-鿿A-Za-z0-9 _·-]{2,40})(?:已|已经)(?:打开|生成|开启|启动)/u);
    if (donePattern?.[1]) return donePattern[1].trim();
    // 匹配 "为您启动XX" / "已启动XX"
    const startPattern = line.match(/(?:已为您|已|为您)?(?:启动|开启|生成)([一-鿿A-Za-z0-9 _·-]{2,40})(?:[，。！？,!?]|$|，请|，可|，访问)/u);
    if (startPattern?.[1]) return startPattern[1].trim();
  }

  return '';
}

/**
 * 从 URL 前方文本中提取引号内的标题
 * 匹配模式如："樱花大冒险"、"「樱花大冒险」等
 */
function titleFromQuotedText(beforeUrl) {
  const quotedTitles = [...beforeUrl.matchAll(/[""'「]([^""'「\n]{2,48})[""'」]/g)]
    .map((match) => match[1].trim())
    .filter((value) => value && !/^https?:\/\//i.test(value));
  return quotedTitles.length ? quotedTitles[quotedTitles.length - 1] : '';
}

/**
 * 从回复文本中推导 URL 附件标题（对齐 openclawAgentProvider.ts titleFromReplyLink）
 *
 * 优先级：
 * 1. markdown 链接的 label 文本
 * 2. URL 前方文本中的卡片生成上下文（"XX卡片已生成"）
 * 3. URL 前方文本中的游戏/活动上下文（"已为您打开XX"）
 * 4. URL 前方文本中的引号标题（"XX"）
 * 5. 从 URL 路径提取文件名作为标题
 *
 * @param {string|undefined} label - markdown 链接的 label
 * @param {string} url - 附件 URL
 * @param {string} fullText - 完整回复文本
 * @param {number} matchIndex - URL 在文本中的位置
 * @returns {string} 标题
 */
function titleFromReplyLink(label, url, fullText, matchIndex) {
  // 1. markdown label
  const normalizedLabel = String(label ?? '').trim();
  if (normalizedLabel) return normalizedLabel;

  const before = fullText.slice(Math.max(0, matchIndex - 180), matchIndex);

  // 2. 卡片生成上下文
  const generatedCardTitle = titleFromGeneratedCardContext(before, url);
  if (generatedCardTitle) return generatedCardTitle;

  // 3. 游戏/活动上下文（如 "已为您打开樱花大冒险"）
  const gameTitle = titleFromGameContext(before);
  if (gameTitle) return gameTitle;

  // 4. 引号标题
  const quotedTitle = titleFromQuotedText(before);
  if (quotedTitle) return quotedTitle;

  // 5. 从 URL 文件名提取标题
  return titleFromUrl(url);
}

function resourceUrls(attachments) {
  return new Set((attachments || []).flatMap((att) => {
    if (att.type !== 'webview' && att.type !== 'image') return [];
    const dedupKey = normalizeUrlForDedup(att.url);
    return dedupKey ? [dedupKey] : [];
  }));
}

// ── 内联 URL 附件提取（对齐 appendInlineWebviewAttachments）──

/**
 * 从文本中提取 markdown 链接和裸 URL 作为附件
 * 提取后追加到 attachments 数组（原地修改）
 * @param {string} content - 原始文本
 * @param {array} attachments - 附件数组（原地追加）
 */
export function appendInlineAttachments(content, attachments) {
  const text = String(content ?? '');
  if (!text) return;
  const existingUrls = resourceUrls(attachments);

  for (const match of text.matchAll(MARKDOWN_LINK_RE)) {
    let rawUrl = match[2] || match[0] || '';
    const matchEnd = (match.index ?? 0) + match[0].length;
    const after = text.slice(matchEnd);
    // 检测 URL 后紧跟的 CJK 扩展名（如中文后面的 .png）
    const cjkExt = after.match(/^([一-鿿\w-]*\.(?:png|jpe?g|gif|webp|svg|ico|pdf|txt|wav|mp3|mp4))/iu);
    if (cjkExt) rawUrl = rawUrl + cjkExt[1];
    const url = cleanRawUrl(rawUrl);
    const dedupUrl = normalizeUrlForDedup(url);
    if (!isHttpUrl(url) || existingUrls.has(dedupUrl)) continue;

    const context = text.slice(Math.max(0, (match.index ?? 0) - 180), match.index ?? 0);
    const title = titleFromReplyLink(match[1], url, text, match.index ?? 0);
    const image = isImageUrl(url);
    const isH5Card = isGeneratedH5Attachment(url, title, context);

    attachments.push({
      type: image ? 'image' : 'webview',
      url,
      title,
      summary: image ? undefined : isH5Card ? '点击查看生成的 H5 卡片' : '点击在当前项目内打开',
      sourceSkillId: isH5Card ? 'h5-cards' : undefined,
      objectKind: isH5Card ? 'h5_card' : undefined,
      artifactType: image ? 'image' : isH5Card ? 'html' : undefined,
      mimeType: image ? undefined : isH5Card ? 'text/html' : undefined,
    });
    existingUrls.add(dedupUrl);
  }
}

// ── 从文本中移除已提取为附件的 URL（对齐 stripAttachedInlineLinks）──

/**
 * 从文本中移除已作为附件提取的 URL（markdown 链接和裸 URL）
 * 清理孤立标签、空行、反引号残留
 * @param {string} content - 原始文本
 * @param {array} attachments - 附件数组
 * @returns {string} 清理后的文本
 */
export function stripInlineLinks(content, attachments) {
  const urls = resourceUrls(attachments);
  if (!content || urls.size === 0) return content;

  // 1. 移除 markdown 图片链接 ![label](url)
  const withoutImages = content.replace(MARKDOWN_IMAGE_RE, (match, _label, markdownUrl) => {
    const url = cleanRawUrl(markdownUrl || '');
    return urls.has(normalizeUrlForDedup(url)) ? '' : match;
  });

  // 2. 移除 markdown 链接 [label](url) 和裸 URL
  //    对于 markdown 链接：保留 label 文本，移除 URL 部分
  //    对于裸 URL：完全移除
  const stripped = withoutImages.replace(MARKDOWN_LINK_RE, (match, label, markdownUrl) => {
    const url = cleanRawUrl(markdownUrl || match || '');
    if (!urls.has(normalizeUrlForDedup(url))) return match;
    const normalizedLabel = String(label ?? '').trim();
    return normalizedLabel && normalizedLabel !== url ? normalizedLabel : '';
  });

  // 3. 清理残留的孤立行（"链接:" 等标签行）
  const cleanedLines = stripped
    .split(/\r?\n/)
    .map((line) => line.trimEnd())
    .filter((line) => !/^(?:🔗\s*)?(?:链接|地址|入口|游戏入口在这里|资源地址|访问地址)[:：]?\s*$/u.test(line.trim()));

  // 4. 清理孤立反引号、多余空行
  const cleaned = cleanedLines.join('\n')
    .replace(/\n{3,}/g, '\n\n')
    .replace(/^`+$/gm, '')
    .replace(/\n`+\n/g, '\n')
    .replace(/\n{3,}/g, '\n\n')
    .trim();

  return cleaned || '已处理完成，请查看下方资源。';
}

// ── 组合：提取附件 + 清理文本（对齐 hydrateInlineWebviewAttachments）──

/**
 * 一步完成：从文本中提取 URL 附件并清理文本
 * @param {string} content - 原始文本
 * @param {array} sourceAttachments - 已有附件（可选）
 * @returns {{ content: string, attachments: array }}
 */
export function hydrateInlineAttachments(content, sourceAttachments = []) {
  const attachments = [...sourceAttachments];
  appendInlineAttachments(content, attachments);
  return {
    content: stripInlineLinks(content, attachments),
    attachments,
  };
}

// ── AI 回复清洗（对齐 localAgentRequestService.ts sanitizeAgentVisibleReply）──

// 乱码检测
const MOJIBAKE_RE = /(锛|銆|鍔|鑰|绋|嬫|傛|笉|闂|鍚|搴|勭|堕|硅|秴|嬪|鐨|浠|诲|殑|夸|娆|�)/;

// AI 协议泄露检测
const AI_SKILL_RESULT_LEAK_RE = /\bAI_SKILL_RESULT\b|"(?:event|providerId|skillId|workspaceId|bizPayload|skillName|conversationId|taskNo|outputs|summary|status|error)"\s*:/i;

// 执行细节泄露检测
const EXECUTION_LEAK_RE = /\b(base64|delivery\s+mode|webhook|cron\s+job|croncreate|powershell|invoke-restmethod|agentchat_push_ok|let\s+me|now\s+let\s+me|tool|execute|none|[\w-]+\.py)\b/i;

// 英文思考过程检测
const THINKING_PROCESS_RE = /\b(let me|i need to|i should|i will|i'll|first,?\s*i|now,?\s*i|next,?\s*i|then,?\s*i|finally,?\s*i|step \d|the user|the assistant|to answer this|to respond|my response|i can help|i would|i think|in this case|based on the|looking at|let's|we need to|we should|we can|okay,?\s*|alright,?\s*|hmm,?\s*|well,?\s*i|so,?\s*i|actually,?\s*i)\b/i;

// 中文思考过程标记
const CN_THINKING_MARKERS = /(\u6211\u9700\u8981|\u8ba9\u6211\u60f3\u60f3|\u6211\u6765\u5206\u6790|\u5148\u770b\u4e00\u4e0b|\u9996\u5148|\u7b2c\u4e00\u6b65|\u63a5\u4e0b\u6765|\u7136\u540e\u6211\u4f1a|\u6700\u540e|\u7528\u6237\u60f3\u8981|\u7528\u6237\u7684\u95ee\u9898|\u7528\u6237\u7684\u9700\u6c42|\u5206\u6790\u4e00\u4e0b|\u7406\u89e3\u4e00\u4e0b|\u8fd9\u662f\u4e00\u4e2a|\u8fd9\u4e2a\u95ee\u9898|\u6839\u636e\u8981\u6c42|\u8ba9\u6211\u6765|\u6211\u5e94\u8be5|\u6211\u53ef\u4ee5|\u6211\u4eec\u9700|\u6211\u4eec\u5148|\u7b2c\u4e00\u6b65\u662f|\u7b2c\u4e8c\u6b65|\u7b2c\u4e09\u6b65|\u65b9\u6848\u5982\u4e0b|\u5177\u4f53\u6b65\u9aa4|\u64cd\u4f5c\u6b65\u9aa4|\u6211\u4f1a\u6309\u7167|\u6309\u4ee5\u4e0b\u6b65\u9aa4|\u9700\u8981\u5148\u786e\u8ba4|\u9700\u8981\u5148\u4e86\u89e3|\u8ba9\u6211\u7406\u89e3|\u5148\u7406\u6e05|\u68b3\u7406\u4e00\u4e0b)/u;

function isLowInformationAgentReply(content) {
  return /^(已处理完成，请查看当前结果。?|已处理完成。?|处理完成。?|已完成。?|完成。?)$/u.test(content.trim());
}

function hasMojibakeText(content) {
  const matches = content.match(new RegExp(MOJIBAKE_RE.source, 'g'));
  return (matches?.length ?? 0) >= 2;
}

function hasAiProtocolLeak(content) {
  return AI_SKILL_RESULT_LEAK_RE.test(content);
}

function isMostlyEnglishText(content) {
  const withoutUrls = content.replace(/https?:\/\/\S+/gi, '');
  const latinCount = (withoutUrls.match(/[a-z]/gi) ?? []).length;
  const chineseCount = (withoutUrls.match(/[\u4e00-\u9fff]/g) ?? []).length;
  return latinCount > 30 && latinCount > chineseCount * 1.5;
}

function looksLikeThinkingProcess(content) {
  // 英文思考检测
  const enMatch = content.match(THINKING_PROCESS_RE);
  if (enMatch) {
    const withoutUrls = content.replace(/https?:\/\/\S+/gi, '');
    const latinCount = (withoutUrls.match(/[a-z]/gi) ?? []).length;
    const chineseCount = (withoutUrls.match(/[\u4e00-\u9fff]/g) ?? []).length;
    if (latinCount > 80 && (chineseCount === 0 || latinCount > chineseCount * 0.5)) return true;
  }

  // 中文思考检测
  const cnMatches = content.match(CN_THINKING_MARKERS);
  if (cnMatches && cnMatches.length >= 2) {
    const chineseCount = (content.match(/[\u4e00-\u9fff]/g) ?? []).length;
    if (chineseCount > 60) {
      const firstMarkerPos = content.search(CN_THINKING_MARKERS);
      if (firstMarkerPos >= 0 && firstMarkerPos < content.length * 0.6) return true;
    }
  }

  return false;
}

function extractFinalAnswer(content) {
  const cleaned = content.replace(/^\u2705\s*/u, '').trim();

  // 策略 A: 查找结构化分隔（空行 + 编号/标题）
  const structuralBreak = cleaned.search(/\n\n(?:[\u4e00\u4e8c\u4e09\u56db\u4e94\u516d\u4e03\u516b\u4e5d\u5341]\u3001|[0-9]+[\.\u3001]|#{1,3}\s|[\u4e00-\u9fff]{4,})/u);
  if (structuralBreak >= 0) {
    const answer = cleaned.slice(structuralBreak).trim();
    if (answer && !isLowInformationAgentReply(answer) && answer.length >= 10) return answer;
  }

  // 策略 B: 查找第一个完整中文句子
  const chineseSentenceStart = cleaned.search(/[\u4e00-\u9fff]{4,}/u);
  if (chineseSentenceStart < 0) return '';

  let start = chineseSentenceStart;
  const prefix = cleaned.slice(0, chineseSentenceStart);
  const lastBreak = Math.max(
    prefix.lastIndexOf('\n'),
    prefix.lastIndexOf('. '),
    prefix.lastIndexOf('! '),
    prefix.lastIndexOf('? '),
    prefix.lastIndexOf('\u3002'),
    prefix.lastIndexOf('\uff01'),
    prefix.lastIndexOf('\uff1f'),
  );
  if (lastBreak >= 0 && chineseSentenceStart - lastBreak < 200) {
    start = lastBreak + 1;
  }

  const answer = cleaned.slice(start).trim();
  if (!answer || isLowInformationAgentReply(answer)) return '';

  // 策略 C: 查找结论标记
  const conclusionRe = /(?:\u4ee5\u4e0b\u662f|\u6700\u7ec8\u7ed3\u679c|\u603b\u7ed3\u5982\u4e0b|\u4f5c\u4e1a\u5185\u5bb9\u5982\u4e0b|\u5177\u4f53\u5982\u4e0b|\u56de\u590d\u5982\u4e0b|\u7b54\u6848\u5982\u4e0b|\u5361\u7247\u5982\u4e0b|\u5185\u5bb9\u5982\u4e0b|\u5982\u4e0b[\uff1a:])/u;
  const conclusionMatch = conclusionRe.exec(answer);
  if (conclusionMatch && conclusionMatch.index >= 0 && answer.length - conclusionMatch.index > 20) {
    return answer.slice(conclusionMatch.index).trim();
  }

  return answer;
}

function stripLeadingEnglishPreamble(content) {
  if (!/^[A-Za-z]/.test(content) || !/[\u4e00-\u9fff]/.test(content)) return content;
  return content.replace(/^[A-Za-z][A-Za-z\s.''-]{4,80}(?=[\u4e00-\u9fff])/u, '');
}

function hasVisibleAttachment(attachments) {
  return Boolean((attachments || []).some((att) => (
    att.type === 'image' || att.type === 'webview' ||
    att.type === 'native_post'
  )));
}

function hasH5Card(attachments) {
  return Boolean((attachments || []).some((a) => (
    a.type === 'webview' &&
    (a.objectKind === 'h5_card' || a.sourceSkillId === 'h5-cards' || a.artifactType === 'html')
  )));
}

function safeReplyFallback(attachments) {
  if (hasH5Card(attachments)) return 'H5 卡片已生成，请查看下方卡片。';
  if (hasVisibleAttachment(attachments)) return '已处理完成，请查看下方卡片。';
  return '已处理完成，请查看当前结果。';
}

/**
 * 清洗 AI 可见回复文本
 * 对齐 localAgentRequestService.ts sanitizeAgentVisibleReply
 *
 * 处理内容：
 * - 低信息回复 -> 替换为安全回退文本
 * - 乱码 -> 替换为安全回退文本
 * - AI 协议泄露 -> 替换为安全回退文本
 * - LLM 思考过程泄露 -> 尝试提取最终答案，失败则回退
 * - 英文前导语 -> 去除
 * - 执行细节泄露 -> 清理
 *
 * @param {string} text - AI 回复文本
 * @param {array} attachments - 附件数组（用于生成回退文本）
 * @returns {string} 清洗后的文本
 */
export function sanitizeAgentReply(text, attachments = []) {
  const content = String(text || '').trim();
  if (!content) return content;

  const fallback = safeReplyFallback(attachments);

  // 低信息/乱码/协议泄露 -> 回退
  if (isLowInformationAgentReply(content)) return fallback;
  if (hasMojibakeText(content)) return fallback;
  if (hasAiProtocolLeak(content)) return fallback;

  // 思考过程泄露 -> 尝试提取最终答案
  if (looksLikeThinkingProcess(content)) {
    const finalAnswer = extractFinalAnswer(content);
    if (finalAnswer && finalAnswer.length >= 6 && !isLowInformationAgentReply(finalAnswer)) {
      return finalAnswer;
    }
    return fallback;
  }

  // 去除英文前导语
  let cleaned = stripLeadingEnglishPreamble(content);

  // 执行细节泄露 -> 尝试从中文起始处截取
  if (EXECUTION_LEAK_RE.test(cleaned)) {
    const chineseStart = cleaned.search(/(?:✅\s*)?(已为您|已设置|已创建|已生成|已发布|已安排|已加入)/u);
    if (chineseStart >= 0) cleaned = cleaned.slice(chineseStart);
    else return fallback;
  }

  // 清理残留的泄露关键词
  cleaned = cleaned
    .replace(/\b(?:base64|delivery\s+mode|webhook|cron\s+job|powershell|invoke-restmethod|agentchat_push_ok)\b/gi, '')
    .replace(/^✅\s*/u, '')
    .replace(/[ \t]+\n/g, '\n')
    .trim();

  if (!cleaned || hasMojibakeText(cleaned) || isMostlyEnglishText(cleaned)) return fallback;
  return cleaned;
}

export default {
  cleanRawUrl,
  isHttpUrl,
  isImageUrl,
  isHtmlUrl,
  isGeneratedHtmlCardUrl,
  isGeneratedH5CardUrl,
  titleFromUrl,
  normalizeUrlForDedup,
  dedupeAttachments,
  appendInlineAttachments,
  stripInlineLinks,
  hydrateInlineAttachments,
  sanitizeAgentReply,
};
