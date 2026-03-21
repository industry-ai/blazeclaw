#pragma once

#include "pch.h"
#include "ChatInputEdit.h"

class CChatView : public CView
{
protected:
	CChatView() noexcept;
	DECLARE_DYNCREATE(CChatView)

// Controls
protected:
	CListBox m_wndMsgList;
	CChatInputEdit m_wndInput;
	CButton  m_wndSend;

	struct CHAT_ITEM
	{
		CString text;
       BOOL bSelf = FALSE; // TRUE=self (right), FALSE=peer (left)
	};
	CArray<CHAT_ITEM, CHAT_ITEM&> m_items;

// Overrides
public:
	virtual void OnDraw(CDC* /*pDC*/);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSendClicked();
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	DECLARE_MESSAGE_MAP()

	void LayoutControls(int cx, int cy);
	void AppendMessage(const CString& strText, BOOL bSelf);
};

