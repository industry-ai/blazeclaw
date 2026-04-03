// BlazeClawMarkdownView.h : interface of the CBlazeClawMarkdownView class
//

#pragma once

#include <string>
#include <cstdint>

class CBlazeClawMFCDoc;

class CBlazeClawMarkdownView : public CView
{
protected:
	CBlazeClawMarkdownView() noexcept;
	DECLARE_DYNCREATE(CBlazeClawMarkdownView)

// Attributes
public:
	CBlazeClawMFCDoc* GetDocument() const;

	static constexpr UINT_PTR kMarkdownUpdateTimerId = 0x4B22;
	static constexpr UINT kMarkdownUpdateTimerMs = 200;

// Operations
public:
	void RequestMarkdownUpdate();

// Overrides
public:
	virtual void OnDraw(CDC* pDC) override;
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo) override;
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo) override;
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo) override;

// Implementation
public:
	virtual ~CBlazeClawMarkdownView();
#ifdef _DEBUG
	virtual void AssertValid() const override;
	virtual void Dump(CDumpContext& dc) const override;
#endif

protected:
	std::wstring m_pendingMarkdownContent;
	std::wstring m_currentMarkdownContent;
	bool m_pendingUpdate = false;

	// 选中内容相关
	int m_selectionStart = -1;  // 字符索引
	int m_selectionEnd = -1;
	CPoint m_mouseDownPos;        // 鼠标按下位置
	bool m_isSelecting = false;

	// 获取渲染后的文本（用于复制）
	std::wstring GetRenderedText() const;
	std::wstring GetPlainTextContent() const;

	// 选中管理
	void ClearSelection();
	void UpdateSelection(CPoint point);
	CString GetSelectedText() const;
	CString GetAllText() const;
	bool HasSelection() const;

	void RenderMarkdown(CDC* pDC, const std::wstring& markdown, const CRect& rc);
	void RenderPlainText(CDC* pDC, const std::wstring& text, const CRect& rc);
	std::vector<std::wstring> SplitLines(const std::wstring& text);
	CRect DrawStyledText(CDC* pDC, const std::wstring& text, CRect rc, bool isBold, bool isItalic, bool isCode, bool isHeading, int headingLevel);

	struct TextStyle {
		bool bold = false;
		bool italic = false;
		bool code = false;
		bool heading = false;
		int headingLevel = 0;
	};

	TextStyle ParseLineStyle(const std::wstring& line);

// ============================================================================
// 通用占位符模板 - 可复用的 Markdown 渲染效果示例
// ============================================================================
public:
	void RenderPlaceholder(CDC* pDC, const CRect& rc);

protected:
	void DrawTextElement(CDC* pDC, CRect& rc, const wchar_t* text,
		COLORREF color, int fontSize, BOOL isBold, BOOL isItalic);
	void DrawParagraph(CDC* pDC, CRect& rc, const wchar_t* text);
	void DrawHorizLine(CDC* pDC, CRect& rc, COLORREF color);
	void DrawBulletList(CDC* pDC, CRect& rc,
		const wchar_t** items, int count, COLORREF bulletColor);
	void DrawCodeBlock(CDC* pDC, CRect& rc,
		const wchar_t* langLabel, const wchar_t** lines, int lineCount);
	void DrawQuoteBlock(CDC* pDC, CRect& rc,
		const wchar_t** lines, int lineCount, COLORREF lineColor);

// Generated message map functions
protected:
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnDocumentMarkdownUpdate(WPARAM wParam, LPARAM lParam);
	// 复制功能
	afx_msg void OnEditCopy();
	afx_msg void OnEditSelectAll();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()
public:
	virtual void OnInitialUpdate() override;
	afx_msg void OnSize(UINT nType, int cx, int cy);
};

#ifndef _DEBUG
inline CBlazeClawMFCDoc* CBlazeClawMarkdownView::GetDocument() const
{ return reinterpret_cast<CBlazeClawMFCDoc*>(m_pDocument); }
#endif
