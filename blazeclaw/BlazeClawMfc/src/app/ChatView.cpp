#include "pch.h"
#include "framework.h"
#include "ChatView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CChatView, CView)

BEGIN_MESSAGE_MAP(CChatView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_MEASUREITEM()
	ON_WM_DRAWITEM()
	ON_BN_CLICKED(1001, &CChatView::OnSendClicked)
END_MESSAGE_MAP()

CChatView::CChatView() noexcept
{
}

BOOL CChatView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

void CChatView::OnDraw(CDC* /*pDC*/)
{
	// 不需要额外绘制，由子控件负责显示
}

int CChatView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rcDummy(0, 0, 0, 0);

	// 消息列表
	if (!m_wndMsgList.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
		LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
		rcDummy, this, 1000))
	{
		TRACE0("Failed to create message list\n");
		return -1;
	}

	// 输入框
	if (!m_wndInput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
		ES_MULTILINE | ES_AUTOVSCROLL,
		rcDummy, this, 1002))
	{
		TRACE0("Failed to create input edit\n");
		return -1;
	}
	m_wndInput.m_pOwner = this;

	// 发送按钮
	if (!m_wndSend.Create(_T("发送"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		rcDummy, this, 1001))
	{
		TRACE0("Failed to create send button\n");
		return -1;
	}

	return 0;
}

void CChatView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);
	LayoutControls(cx, cy);
}

void CChatView::LayoutControls(int cx, int cy)
{
	if (!::IsWindow(m_wndMsgList.GetSafeHwnd()))
		return;

	const int nMargin = 8;
	const int nInputHeight = 64;
	const int nButtonWidth = 70;

	CRect rcClient(0, 0, cx, cy);

	// 消息列表区域（上方）
	CRect rcList = rcClient;
	rcList.DeflateRect(nMargin, nMargin, nMargin, 0);
	rcList.bottom -= (nMargin + nInputHeight + nMargin);
	if (rcList.bottom < rcList.top)
		rcList.bottom = rcList.top;

	m_wndMsgList.MoveWindow(rcList);

	// 输入框和按钮区域（下方）
	CRect rcBottom = rcClient;
	rcBottom.DeflateRect(nMargin, 0, nMargin, nMargin);
	rcBottom.top = rcBottom.bottom - nInputHeight;
	if (rcBottom.top < rcList.bottom + nMargin)
		rcBottom.top = rcList.bottom + nMargin;

	CRect rcButton = rcBottom;
	rcButton.left = rcButton.right - nButtonWidth;

	CRect rcEdit = rcBottom;
	rcEdit.right = rcButton.left - nMargin;

	m_wndInput.MoveWindow(rcEdit);
	m_wndSend.MoveWindow(rcButton);
}

void CChatView::OnSendClicked()
{
	CString strText;
	m_wndInput.GetWindowText(strText);
	strText.Trim();
	if (!strText.IsEmpty())
	{
		AppendMessage(strText, TRUE);
		m_wndInput.SetWindowText(_T(""));
		AppendMessage(_T("这是一个自动回复的消息示例。"), FALSE);
	}
}

void CChatView::AppendMessage(const CString& strText, BOOL bSelf)
{
	CHAT_ITEM item;
	item.text = strText;
	item.bSelf = bSelf;
	int nIndex = (int)m_items.Add(item);
	m_wndMsgList.AddString(_T(""));
	if (nIndex != LB_ERR && nIndex != LB_ERRSPACE)
	{
		// 触发重新测量高度并滚动到底部
		m_wndMsgList.Invalidate();
		m_wndMsgList.UpdateWindow();
		m_wndMsgList.SetCurSel(nIndex);
		m_wndMsgList.SetTopIndex(max(0, nIndex - 1));
	}
}

static void DrawBubble(CDC& dc, const CRect& rcBubble, bool bSelf)
{
	const COLORREF clrFill = bSelf ? RGB(149, 236, 105) : RGB(242, 242, 242);
	const COLORREF clrBorder = bSelf ? RGB(120, 210, 80) : RGB(220, 220, 220);

	CPen pen(PS_SOLID, 1, clrBorder);
	CBrush br(clrFill);
	CPen* pOldPen = dc.SelectObject(&pen);
	CBrush* pOldBrush = dc.SelectObject(&br);

	CRect rr = rcBubble;
	dc.RoundRect(rr, CPoint(10, 10));

	// 小尾巴（简化三角形）
	POINT pts[3]{};
	if (bSelf)
	{
		pts[0] = { rr.right, rr.top + 14 };
		pts[1] = { rr.right + 8, rr.top + 18 };
		pts[2] = { rr.right, rr.top + 22 };
	}
	else
	{
		pts[0] = { rr.left, rr.top + 14 };
		pts[1] = { rr.left - 8, rr.top + 18 };
		pts[2] = { rr.left, rr.top + 22 };
	}
	dc.Polygon(pts, 3);

	dc.SelectObject(pOldBrush);
	dc.SelectObject(pOldPen);
}

void CChatView::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMIS)
{
	if (nIDCtl != 1000 || lpMIS == nullptr)
	{
		CView::OnMeasureItem(nIDCtl, lpMIS);
		return;
	}

	if (lpMIS->itemID == (UINT)-1 || lpMIS->itemID >= (UINT)m_items.GetSize())
	{
		lpMIS->itemHeight = 24;
		return;
	}

	CRect rcClient;
	m_wndMsgList.GetClientRect(&rcClient);

	const int outerMarginH = 10;
	const int outerMarginV = 6;
	const int bubblePaddingH = 10;
	const int bubblePaddingV = 6;
	const int tail = 10;

	const int maxBubbleWidth = max(120, (int)(rcClient.Width() * 0.70f));
	const int maxTextWidth = maxBubbleWidth - 2 * bubblePaddingH;

	CDC* pDC = m_wndMsgList.GetDC();
	if (pDC == nullptr)
	{
		lpMIS->itemHeight = 32;
		return;
	}

	CFont* pOldFont = pDC->SelectObject(m_wndMsgList.GetFont());

	CRect rcCalc(0, 0, maxTextWidth, 0);
	pDC->DrawText(m_items[(int)lpMIS->itemID].text, rcCalc,
		DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);

	pDC->SelectObject(pOldFont);
	m_wndMsgList.ReleaseDC(pDC);

	const int bubbleH = rcCalc.Height() + 2 * bubblePaddingV;
	const int itemH = bubbleH + 2 * outerMarginV;
	lpMIS->itemHeight = max(28, itemH);
}

void CChatView::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS)
{
	if (nIDCtl != 1000 || lpDIS == nullptr)
	{
		CView::OnDrawItem(nIDCtl, lpDIS);
		return;
	}

	if (lpDIS->itemID == (UINT)-1 || lpDIS->itemID >= (UINT)m_items.GetSize())
		return;

	CDC dc;
	dc.Attach(lpDIS->hDC);

	CRect rcItem(lpDIS->rcItem);
	dc.FillSolidRect(rcItem, RGB(255, 255, 255));

	const CHAT_ITEM& item = m_items[(int)lpDIS->itemID];
	const bool bSelf = item.bSelf != FALSE;

	const int outerMarginH = 10;
	const int outerMarginV = 6;
	const int bubblePaddingH = 10;
	const int bubblePaddingV = 6;
	const int tail = 10;

	const int itemWidth = rcItem.Width();
	const int maxBubbleWidth = max(120, (int)(itemWidth * 0.70f));
	const int maxTextWidth = maxBubbleWidth - 2 * bubblePaddingH;

	CRect rcCalc(0, 0, maxTextWidth, 0);
	dc.SetBkMode(TRANSPARENT);
	CFont* pOldFont = dc.SelectObject(m_wndMsgList.GetFont());
	dc.DrawText(item.text, rcCalc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);

	const int bubbleW = min(maxBubbleWidth, rcCalc.Width() + 2 * bubblePaddingH);
	const int bubbleH = rcCalc.Height() + 2 * bubblePaddingV;

	CRect rcBubble;
	rcBubble.top = rcItem.top + outerMarginV;
	rcBubble.bottom = rcBubble.top + bubbleH;

	if (bSelf)
	{
		rcBubble.right = rcItem.right - outerMarginH - tail;
		rcBubble.left = rcBubble.right - bubbleW;
	}
	else
	{
		rcBubble.left = rcItem.left + outerMarginH + tail;
		rcBubble.right = rcBubble.left + bubbleW;
	}

	DrawBubble(dc, rcBubble, bSelf);

	CRect rcText = rcBubble;
	rcText.DeflateRect(bubblePaddingH, bubblePaddingV);
	dc.SetTextColor(RGB(0, 0, 0));
	dc.DrawText(item.text, rcText, DT_WORDBREAK | DT_NOPREFIX);

	dc.SelectObject(pOldFont);
	dc.Detach();
}

