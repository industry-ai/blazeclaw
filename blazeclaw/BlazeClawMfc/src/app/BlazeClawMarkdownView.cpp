// BlazeClawMarkdownView.cpp : implementation of the CBlazeClawMarkdownView class
//

#include "pch.h"
#include "framework.h"
#include <atlbase.h>
#include <atlstr.h>
#include <strsafe.h>
#include "BlazeClawMFCApp.h"
#include "BlazeClawMarkdownView.h"
#include "BlazeClawMFCDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

constexpr COLORREF kHeadingColor = RGB(30, 60, 114);
constexpr COLORREF kCodeBlockBg = RGB(245, 245, 245);
constexpr COLORREF kCodeTextColor = RGB(163, 21, 21);
constexpr COLORREF kBoldTextColor = RGB(0, 0, 0);
constexpr COLORREF kNormalTextColor = RGB(51, 51, 51);
constexpr COLORREF kLinkColor = RGB(0, 102, 204);

COLORREF GetHeadingColor(int level) {
	switch (level) {
	case 1: return RGB(30, 60, 114);
	case 2: return RGB(57, 86, 138);
	case 3: return RGB(84, 112, 161);
	case 4: return RGB(111, 138, 185);
	default: return RGB(138, 165, 208);
	}
}

} // namespace


// CBlazeClawMarkdownView

IMPLEMENT_DYNCREATE(CBlazeClawMarkdownView, CView)

BEGIN_MESSAGE_MAP(CBlazeClawMarkdownView, CView)
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CBlazeClawMarkdownView::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_SETCURSOR()
	ON_COMMAND(ID_EDIT_COPY, &CBlazeClawMarkdownView::OnEditCopy)
	ON_COMMAND(ID_EDIT_SELECT_ALL, &CBlazeClawMarkdownView::OnEditSelectAll)
	ON_MESSAGE(WM_DOC_MARKDOWN_UPDATE, &CBlazeClawMarkdownView::OnDocumentMarkdownUpdate)
END_MESSAGE_MAP()

// CBlazeClawMarkdownView construction/destruction

CBlazeClawMarkdownView::CBlazeClawMarkdownView() noexcept
{
	EnableActiveAccessibility();
}

CBlazeClawMarkdownView::~CBlazeClawMarkdownView()
{
}

BOOL CBlazeClawMarkdownView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

// CBlazeClawMarkdownView drawing

void CBlazeClawMarkdownView::OnDraw(CDC* pDC)
{
	if (!pDC)
		return;

	CRect rc;
	GetClientRect(&rc);

	if (m_currentMarkdownContent.empty()) {
		RenderPlaceholder(pDC, rc);
		return;
	}

	RenderMarkdown(pDC, m_currentMarkdownContent, rc);
}

void CBlazeClawMarkdownView::RenderMarkdown(CDC* pDC, const std::wstring& markdown, const CRect& rc)
{
	pDC->FillSolidRect(&rc, RGB(255, 255, 255));

	CRect contentRc = rc;
	contentRc.DeflateRect(16, 16);

	auto lines = SplitLines(markdown);
	CFont* pOldFont = pDC->SelectObject(&afxGlobalData.fontRegular);

	for (const auto& line : lines) {
		if (contentRc.top > contentRc.bottom)
			break;

		auto style = ParseLineStyle(line);
		std::wstring text = line;

		if (style.code && !line.empty() && line[0] == '`') {
			text = line.substr(1, line.length() - 2);
			if (!text.empty() && text[0] == '`')
				text = text.substr(1);
			if (!text.empty() && text[text.length() - 1] == '`')
				text = text.substr(0, text.length() - 1);
		}

		contentRc = DrawStyledText(pDC, text, contentRc,
			style.bold, style.italic, style.code,
			style.heading, style.headingLevel);

		if (style.heading && style.headingLevel <= 2) {
			contentRc.top += 8;
		}
	}

	pDC->SelectObject(pOldFont);
}

void CBlazeClawMarkdownView::RenderPlainText(CDC* pDC, const std::wstring& text, const CRect& rc)
{
	pDC->FillSolidRect(&rc, RGB(255, 255, 255));
	pDC->SetTextColor(kNormalTextColor);
	CString strText(text.c_str());
	CRect rcCopy = rc;
	pDC->DrawText(strText, rcCopy, DT_WORDBREAK | DT_LEFT);
}

// ============================================================================
// 通用占位符模板渲染 - 可复用的 Markdown 渲染效果示例
// ============================================================================

void CBlazeClawMarkdownView::RenderPlaceholder(CDC* pDC, const CRect& rc)
{
	pDC->FillSolidRect(&rc, RGB(250, 250, 252));

	CPen borderPen(PS_SOLID, 1, RGB(220, 220, 230));
	CPen* pOldPen = pDC->SelectObject(&borderPen);
	pDC->Rectangle(&rc);
	pDC->SelectObject(pOldPen);

	CRect contentRc = rc;
	contentRc.DeflateRect(24, 24, 16, 24);

	// 标题行
	DrawTextElement(pDC, contentRc, L"Markdown Rendering Demo",
		RGB(30, 60, 114), 28, TRUE, FALSE);

	// 分隔线
	DrawHorizLine(pDC, contentRc, RGB(200, 200, 210));

	// H2 标题
	DrawTextElement(pDC, contentRc, L"Features Supported",
		RGB(57, 86, 138), 20, TRUE, FALSE);

	// 正文段落
	DrawParagraph(pDC, contentRc,
		L"This view renders Markdown content with various formatting styles "
		L"including bold, italic, code blocks, and multiple heading levels.");

	// 无序列表
	const wchar_t* listItems[] = {
		L"Headings: H1 ~ H6 with distinct sizes",
		L"Inline code: `C++`, `Python`, `JavaScript`",
		L"Code blocks: Syntax highlighted blocks",
		L"Real-time updates: 200ms debounced rendering"
	};
	DrawBulletList(pDC, contentRc, listItems, 4, RGB(30, 60, 114));

	// H3 标题
	DrawTextElement(pDC, contentRc, L"Code Block Example",
		RGB(84, 112, 161), 17, TRUE, FALSE);

	// 代码块
	const wchar_t* codeLines[] = {
		L"void OnDraw(CDC* pDC)",
		L"{",
		L"    pDC->TextOut(10,10,L\"Hello\");",
		L"}"
	};
	DrawCodeBlock(pDC, contentRc, L"C++", codeLines, 4);

	// 引用块
	const wchar_t* quoteLines[] = {
		L"Markdown content will be rendered here.",
		L"Click to load content."
	};
	DrawQuoteBlock(pDC, contentRc, quoteLines, 2, RGB(30, 60, 114));

	// 底部状态
	DrawTextElement(pDC, contentRc, L"Waiting for content... | Updates every 200ms",
		RGB(150, 150, 150), 11, FALSE, FALSE);
}

// ---------------------------------------------------------------------------
// 基础绘制元素
// ---------------------------------------------------------------------------

void CBlazeClawMarkdownView::DrawTextElement(CDC* pDC, CRect& rc,
	const wchar_t* text, COLORREF color, int fontSize,
	BOOL isBold, BOOL isItalic)
{
	CFont font;
	LOGFONT lf = { 0 };
	(isBold ? afxGlobalData.fontBold : afxGlobalData.fontRegular).GetLogFont(&lf);
	lf.lfHeight = fontSize;
	lf.lfWeight = isBold ? FW_BOLD : FW_NORMAL;
	lf.lfItalic = isItalic ? TRUE : FALSE;
	font.CreateFontIndirect(&lf);

	CFont* pOldFont = pDC->SelectObject(&font);
	pDC->SetTextColor(color);
	pDC->SetBkMode(TRANSPARENT);
	pDC->DrawText(CString(text), rc, DT_SINGLELINE | DT_LEFT);
	rc.top += fontSize + 8;

	pDC->SelectObject(pOldFont);
	font.DeleteObject();
}

void CBlazeClawMarkdownView::DrawParagraph(CDC* pDC, CRect& rc, const wchar_t* text)
{
	CFont font;
	LOGFONT lf = { 0 };
	afxGlobalData.fontRegular.GetLogFont(&lf);
	lf.lfHeight = 14;
	font.CreateFontIndirect(&lf);

	CFont* pOldFont = pDC->SelectObject(&font);
	pDC->SetTextColor(RGB(51, 51, 51));
	CRect paraRc = rc;
	paraRc.right = rc.right;
	pDC->DrawText(CString(text), paraRc, DT_WORDBREAK | DT_LEFT);
	rc.top += 36;

	pDC->SelectObject(pOldFont);
	font.DeleteObject();
}

void CBlazeClawMarkdownView::DrawHorizLine(CDC* pDC, CRect& rc, COLORREF color)
{
	CPen linePen(PS_SOLID, 1, color);
	pDC->SelectObject(&linePen);
	pDC->MoveTo(rc.left, rc.top);
	pDC->LineTo(rc.right, rc.top);
	rc.top += 12;
}

void CBlazeClawMarkdownView::DrawBulletList(CDC* pDC, CRect& rc,
	const wchar_t** items, int count, COLORREF bulletColor)
{
	CFont font;
	LOGFONT lf = { 0 };
	afxGlobalData.fontRegular.GetLogFont(&lf);
	lf.lfHeight = 14;
	font.CreateFontIndirect(&lf);

	CFont* pOldFont = pDC->SelectObject(&font);
	pDC->SetTextColor(RGB(51, 51, 51));

	for (int i = 0; i < count; ++i) {
		CPen bulletPen(PS_SOLID, 1, bulletColor);
		pDC->SelectObject(&bulletPen);
		pDC->Ellipse(rc.left + 4, rc.top + 5, rc.left + 10, rc.top + 11);

		pDC->SelectObject(&font);
		pDC->DrawText(CString(items[i]),
			CRect(rc.left + 20, rc.top, rc.right, rc.top + 20),
			DT_SINGLELINE | DT_LEFT);
		rc.top += 22;
	}

	pDC->SelectObject(pOldFont);
	font.DeleteObject();
}

void CBlazeClawMarkdownView::DrawCodeBlock(CDC* pDC, CRect& rc,
	const wchar_t* langLabel, const wchar_t** lines, int lineCount)
{
	int maxWidth = rc.Width() - 48;
	if (maxWidth > 360) maxWidth = 360;

	// 计算代码块高度
	int lineHeight = 18;
	int codeHeight = lineHeight * lineCount + 20;
	CRect codeBgRc(rc.left, rc.top, rc.left + maxWidth, rc.top + codeHeight);

	pDC->FillSolidRect(&codeBgRc, RGB(245, 245, 245));
	CPen codeBorderPen(PS_SOLID, 1, RGB(200, 200, 200));
	pDC->SelectObject(&codeBorderPen);
	pDC->Rectangle(&codeBgRc);

	// 语言标签
	CFont labelFont;
	LOGFONT lfLabel = { 0 };
	afxGlobalData.fontBold.GetLogFont(&lfLabel);
	lfLabel.lfHeight = 11;
	labelFont.CreateFontIndirect(&lfLabel);
	pDC->SelectObject(&labelFont);
	pDC->SetTextColor(RGB(130, 130, 130));
	pDC->DrawText(CString(langLabel),
		CRect(codeBgRc.left + 8, codeBgRc.top + 4, codeBgRc.right - 8, codeBgRc.top + 18),
		DT_SINGLELINE | DT_LEFT);

	// 代码内容
	CFont codeFont;
	LOGFONT lfCode = lfLabel;
	lfCode.lfHeight = 12;
	lfCode.lfWeight = FW_NORMAL;
	memset(lfCode.lfFaceName, 0, sizeof(lfCode.lfFaceName));
	lfCode.lfFaceName[0] = L'C'; lfCode.lfFaceName[1] = L'o';
	lfCode.lfFaceName[2] = L'n'; lfCode.lfFaceName[3] = L's';
	lfCode.lfFaceName[4] = L'o'; lfCode.lfFaceName[5] = L'l';
	lfCode.lfFaceName[6] = L'a'; lfCode.lfFaceName[7] = L's';
	codeFont.CreateFontIndirect(&lfCode);
	pDC->SelectObject(&codeFont);
	pDC->SetTextColor(RGB(163, 21, 21));

	CRect codeTextRc(codeBgRc.left + 8, codeBgRc.top + 20,
		codeBgRc.right - 8, codeBgRc.bottom - 4);
	std::wstring codeText;
	for (int i = 0; i < lineCount; ++i) {
		codeText += lines[i];
		if (i < lineCount - 1) codeText += L"\n";
	}
	pDC->DrawText(CString(codeText.c_str()), codeTextRc, DT_WORDBREAK | DT_LEFT);

	pDC->SelectObject(&labelFont);
	labelFont.DeleteObject();
	pDC->SelectObject(&codeFont);
	codeFont.DeleteObject();

	rc.top += codeHeight + 8;
}

void CBlazeClawMarkdownView::DrawQuoteBlock(CDC* pDC, CRect& rc,
	const wchar_t** lines, int lineCount, COLORREF lineColor)
{
	int quoteHeight = 18 * lineCount + 12;

	CPen quotePen(PS_SOLID, 3, lineColor);
	pDC->SelectObject(&quotePen);
	pDC->MoveTo(rc.left, rc.top);
	pDC->LineTo(rc.left, rc.top + quoteHeight);

	CFont quoteFont;
	LOGFONT lfQuote = { 0 };
	afxGlobalData.fontRegular.GetLogFont(&lfQuote);
	lfQuote.lfHeight = 13;
	lfQuote.lfItalic = TRUE;
	quoteFont.CreateFontIndirect(&lfQuote);

	CFont* pOldFont = pDC->SelectObject(&quoteFont);
	pDC->SetTextColor(RGB(100, 100, 100));

	std::wstring quoteText;
	for (int i = 0; i < lineCount; ++i) {
		quoteText += lines[i];
		if (i < lineCount - 1) quoteText += L"\n";
	}

	pDC->DrawText(CString(quoteText.c_str()),
		CRect(rc.left + 12, rc.top, rc.right, rc.top + quoteHeight),
		DT_WORDBREAK | DT_LEFT);

	pDC->SelectObject(pOldFont);
	quoteFont.DeleteObject();
	rc.top += quoteHeight + 8;
}

std::vector<std::wstring> CBlazeClawMarkdownView::SplitLines(const std::wstring& text)
{
	std::vector<std::wstring> lines;
	std::wstring current;

	for (wchar_t ch : text) {
		if (ch == L'\n') {
			lines.push_back(current);
			current.clear();
		}
		else if (ch != L'\r') {
			current.push_back(ch);
		}
	}

	if (!current.empty() || !lines.empty()) {
		lines.push_back(current);
	}

	return lines;
}

CRect CBlazeClawMarkdownView::DrawStyledText(
	CDC* pDC,
	const std::wstring& text,
	CRect rc,
	bool isBold,
	bool isItalic,
	bool isCode,
	bool isHeading,
	int headingLevel)
{
	CRect lineRc = rc;
	int lineHeight = 20;

	LOGFONT lf;
	pDC->GetCurrentFont()->GetLogFont(&lf);

	if (isHeading) {
		lf.lfHeight = 24 - (headingLevel * 2);
		lf.lfWeight = FW_BOLD;
	}
	else if (isCode) {
		lf.lfHeight = -13;
		lf.lfWeight = FW_NORMAL;
	}
	else {
		lf.lfHeight = -14;
		lf.lfWeight = isBold ? FW_BOLD : FW_NORMAL;
	}

	if (isItalic) {
		lf.lfItalic = TRUE;
	}

	CFont font;
	font.CreateFontIndirect(&lf);
	CFont* pOldFont = pDC->SelectObject(&font);

	if (isCode) {
		CString codeText(text.c_str());
		CSize codeSize = pDC->GetTextExtent(codeText);
		CRect bgRc = lineRc;
		bgRc.right = lineRc.left + codeSize.cx + 16;
		bgRc.bottom = lineRc.top + codeSize.cy + 8;
		pDC->FillSolidRect(bgRc, kCodeBlockBg);
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(kCodeTextColor);
		bgRc.DeflateRect(8, 4);
		pDC->DrawText(codeText, bgRc, DT_SINGLELINE | DT_LEFT);
		lineRc.top = bgRc.bottom + 8;
	}
	else {
		COLORREF textColor = isHeading ? GetHeadingColor(headingLevel) : kNormalTextColor;
		pDC->SetTextColor(textColor);
		pDC->SetBkMode(TRANSPARENT);

		DWORD dtFlags = DT_WORDBREAK | DT_LEFT;
		if (isHeading) {
			dtFlags = DT_SINGLELINE | DT_LEFT;
		}

		CString bodyText(text.c_str());
		CRect textRc = lineRc;
		pDC->DrawText(bodyText, textRc, dtFlags);
		lineRc.top += lineHeight + 4;
	}

	pDC->SelectObject(pOldFont);
	font.DeleteObject();

	return lineRc;
}

CBlazeClawMarkdownView::TextStyle CBlazeClawMarkdownView::ParseLineStyle(const std::wstring& line)
{
	TextStyle style;

	if (line.empty())
		return style;

	if (line[0] == '#') {
		style.heading = true;
		int hashCount = 0;
		for (wchar_t ch : line) {
			if (ch == '#')
				hashCount++;
			else if (ch == ' ')
				break;
			else
				break;
		}
		style.headingLevel = (std::min)(hashCount, 6);
		return style;
	}

	if (line.length() >= 2) {
		if (line[0] == '`' && line[1] == '`') {
			style.code = true;
			return style;
		}
	}

	if (line[0] == '*' || line[0] == '-') {
		style.bold = true;
	}

	if (line.length() >= 3 && line.substr(0, 3) == L"**") {
		style.bold = true;
	}

	return style;
}

// CBlazeClawMarkdownView printing

void CBlazeClawMarkdownView::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

BOOL CBlazeClawMarkdownView::OnPreparePrinting(CPrintInfo* pInfo)
{
	return DoPreparePrinting(pInfo);
}

void CBlazeClawMarkdownView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
}

void CBlazeClawMarkdownView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
}

void CBlazeClawMarkdownView::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	ClientToScreen(&point);
	OnContextMenu(this, point);
}

void CBlazeClawMarkdownView::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}

// CBlazeClawMarkdownView diagnostics

#ifdef _DEBUG
void CBlazeClawMarkdownView::AssertValid() const
{
	CView::AssertValid();
}

void CBlazeClawMarkdownView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CBlazeClawMFCDoc* CBlazeClawMarkdownView::GetDocument() const
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CBlazeClawMFCDoc)));
	return (CBlazeClawMFCDoc*)m_pDocument;
}
#endif //_DEBUG

// CBlazeClawMarkdownView message handlers

void CBlazeClawMarkdownView::OnInitialUpdate()
{
	CView::OnInitialUpdate();

	SetTimer(kMarkdownUpdateTimerId, kMarkdownUpdateTimerMs, nullptr);
}

void CBlazeClawMarkdownView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);
	Invalidate();
}

void CBlazeClawMarkdownView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kMarkdownUpdateTimerId && m_pendingUpdate) {
		m_pendingUpdate = false;
		m_currentMarkdownContent = m_pendingMarkdownContent;
		Invalidate();
	}

	CView::OnTimer(nIDEvent);
}

void CBlazeClawMarkdownView::RequestMarkdownUpdate()
{
	m_pendingUpdate = true;
}

LRESULT CBlazeClawMarkdownView::OnDocumentMarkdownUpdate(WPARAM wParam, LPARAM lParam)
{
	wParam;
	lParam;

	auto* pDoc = GetDocument();
	if (pDoc) {
		m_pendingMarkdownContent = pDoc->GetMarkdownContent();
		m_pendingUpdate = true;
	}

	return 0;
}

// ============================================================================
// 复制和选择功能实现
// ============================================================================

std::wstring CBlazeClawMarkdownView::GetRenderedText() const
{
	return m_currentMarkdownContent;
}

std::wstring CBlazeClawMarkdownView::GetPlainTextContent() const
{
	// 如果没有内容，返回占位符文本
	if (m_currentMarkdownContent.empty()) {
		return L"Markdown Rendering Demo\n\n"
			L"Features Supported\n"
			L"This view renders Markdown content with various formatting styles including bold, italic, code blocks, and multiple heading levels.\n\n"
			L"- Headings: H1 ~ H6 with distinct sizes\n"
			L"- Inline code: `C++`, `Python`, `JavaScript`\n"
			L"- Code blocks: Syntax highlighted blocks\n"
			L"- Real-time updates: 200ms debounced rendering\n\n"
			L"Code Block Example\n"
			L"```cpp\n"
			L"void OnDraw(CDC* pDC)\n"
			L"{\n"
			L"    pDC->TextOut(10,10,L\"Hello\");\n"
			L"}\n"
			L"```\n\n"
			L"> Markdown content will be rendered here.\n"
			L"> Click to load content.\n\n"
			L"Waiting for content... | Updates every 200ms";
	}
	return m_currentMarkdownContent;
}

bool CBlazeClawMarkdownView::HasSelection() const
{
	return m_selectionStart >= 0 && m_selectionEnd >= 0 && m_selectionStart < m_selectionEnd;
}

void CBlazeClawMarkdownView::ClearSelection()
{
	m_selectionStart = -1;
	m_selectionEnd = -1;
	m_isSelecting = false;
}

CString CBlazeClawMarkdownView::GetAllText() const
{
	return CString(GetPlainTextContent().c_str());
}

CString CBlazeClawMarkdownView::GetSelectedText() const
{
	if (!HasSelection()) {
		return CString();
	}

	std::wstring fullText = GetPlainTextContent();
	int start = m_selectionStart;
	int end = m_selectionEnd;

	// 边界检查
	if (start < 0) start = 0;
	if (end > (int)fullText.length()) end = (int)fullText.length();

	if (start >= end || start >= (int)fullText.length()) {
		return CString();
	}

	return CString(fullText.substr(start, end - start).c_str());
}

void CBlazeClawMarkdownView::UpdateSelection(CPoint point)
{
	CRect rc;
	GetClientRect(&rc);

	// 计算点击位置对应的字符索引
	std::wstring text = GetPlainTextContent();
	int charIndex = 0;
	int lineHeight = 20;
	int top = 16;

	// 简单的按行计算
	auto lines = SplitLines(text);
	for (const auto& line : lines) {
		int lineBottom = top + lineHeight;
		if (point.y >= top && point.y <= lineBottom) {
			// 在这一行，计算具体字符位置
			CDC* pDC = GetDC();
			if (pDC) {
				int charWidth = pDC->GetTextExtent(CString(line.c_str())).cx;
				if (charWidth > 0) {
					float ratio = (float)(point.x - 16) / charWidth;
					charIndex += (int)(ratio * line.length());
					if (charIndex < 0) charIndex = 0;
					if (charIndex > (int)line.length()) charIndex = (int)line.length();
				}
				ReleaseDC(pDC);
			}
			break;
		}
		top = lineBottom;
		charIndex += (int)line.length() + 1; // +1 for newline
	}

	if (m_selectionStart < 0) {
		m_selectionStart = charIndex;
	}
	m_selectionEnd = charIndex;

	// 交换如果反向选择
	if (m_selectionStart > m_selectionEnd) {
		std::swap(m_selectionStart, m_selectionEnd);
	}

	Invalidate();
}

void CBlazeClawMarkdownView::OnLButtonDown(UINT nFlags, CPoint point)
{
	ClearSelection();
	m_mouseDownPos = point;
	m_isSelecting = true;
	m_selectionStart = 0; // 暂时从0开始
	SetCapture();
	UpdateSelection(point);
	CView::OnLButtonDown(nFlags, point);
}

void CBlazeClawMarkdownView::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_isSelecting) {
		UpdateSelection(point);
		m_isSelecting = false;
	}
	ReleaseCapture();
	CView::OnLButtonUp(nFlags, point);
}

void CBlazeClawMarkdownView::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_isSelecting && (nFlags & MK_LBUTTON)) {
		UpdateSelection(point);
	}
	CView::OnMouseMove(nFlags, point);
}

BOOL CBlazeClawMarkdownView::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
		return TRUE;
	}
	return CView::OnSetCursor(pWnd, nHitTest, message);
}

void CBlazeClawMarkdownView::OnEditCopy()
{
	if (!OpenClipboard()) {
		return;
	}

	EmptyClipboard();

	CString textToCopy;
	if (HasSelection()) {
		textToCopy = GetSelectedText();
	} else {
		// 如果没有选中，复制全部
		textToCopy = GetAllText();
	}

	// 复制为 Unicode 文本
	size_t bufSize = (textToCopy.GetLength() + 1) * sizeof(wchar_t);
	HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, bufSize);
	if (hGlob) {
		wchar_t* pGlob = (wchar_t*)GlobalLock(hGlob);
		if (pGlob) {
			memset(pGlob, 0, bufSize);
			for (int i = 0; i < textToCopy.GetLength(); ++i) {
				pGlob[i] = textToCopy[i];
			}
			GlobalUnlock(hGlob);
			SetClipboardData(CF_UNICODETEXT, hGlob);
		}
	}
	CloseClipboard();
}

void CBlazeClawMarkdownView::OnEditSelectAll()
{
	// 全选所有文本
	std::wstring text = GetPlainTextContent();
	m_selectionStart = 0;
	m_selectionEnd = (int)text.length();
	Invalidate();
}
