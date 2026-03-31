// WebViewOnlyChildFrame.h : interface of the CWebViewOnlyChildFrame class
//

#pragma once

#include <afxext.h>

class CWebViewOnlyChildFrame : public CMDIChildWndEx
{
	DECLARE_DYNCREATE(CWebViewOnlyChildFrame)
public:
	CWebViewOnlyChildFrame() noexcept;

// Attributes
protected:
	CSplitterWndEx m_wndSplitter;
	BOOL m_bWebViewOnlyMode = TRUE;
public:

// Operations
public:

// Overrides
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL OnCreateClient(LPCREATESTRUCT* lpcs, CCreateContext* pContext);

// Implementation
public:
	virtual ~CWebViewOnlyChildFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

// Generated message map functions
protected:
	DECLARE_MESSAGE_MAP()
};

