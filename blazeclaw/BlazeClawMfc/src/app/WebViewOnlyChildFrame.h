// WebViewOnlyChildFrame.h : interface of the CWebViewOnlyChildFrame class
//

#pragma once

#include <afxext.h>

class CWebViewOnlyChildFrame : public CMDIChildWndEx
{
	DECLARE_DYNCREATE(CWebViewOnlyChildFrame)
public:
	CWebViewOnlyChildFrame() noexcept;

// Overrides
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

// Implementation
public:
	virtual ~CWebViewOnlyChildFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	DECLARE_MESSAGE_MAP()
};
