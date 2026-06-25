/* ================================================================
   AgentChat HTML 版 - 简易 Markdown 渲染
   生产环境建议替换为 marked 库
   ================================================================ */

const MarkdownRenderer = {
  /**
   * 将 markdown 文本转为安全的 HTML 字符串
   * 覆盖基础格式：**粗体**、*斜体*、`代码`、链接、换行
   */
  render(text) {
    if (!text) return '';
    let html = text;

    // 转义 HTML
    html = html.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

    // 代码块 ```
    html = html.replace(/```(\w*)\n?([\s\S]*?)```/g, '<pre><code>$2</code></pre>');

    // 行内代码 `code`
    html = html.replace(/`([^`]+)`/g, '<code class="inline-code">$1</code>');

    // 粗体 **
    html = html.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');

    // 斜体 *
    html = html.replace(/\*(.+?)\*/g, '<em>$1</em>');

    // 链接 [text](url)
    html = html.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');

    // 换行
    html = html.replace(/\n/g, '<br>');

    return html;
  },
};

export default MarkdownRenderer;
