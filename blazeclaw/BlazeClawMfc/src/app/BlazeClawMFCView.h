// This MFC Samples source code demonstrates using MFC Microsoft Office Fluent User Interface
// (the "Fluent UI") and is provided only as referential material to supplement the
// Microsoft Foundation Classes Reference and related electronic documentation
// included with the MFC C++ library software.
// License terms to copy, use or distribute the Fluent UI are available separately.
// To learn more about our Fluent UI licensing program, please visit
// https://go.microsoft.com/fwlink/?LinkId=238214.
//
// Copyright (C) Microsoft Corporation
// All rights reserved.

// BlazeClaw.MFCView.h : interface of the CBlazeClawMFCView class
//

#pragma once

#if defined(__has_include)
# if __has_include(<WebView2.h>)
# include <WebView2.h>
# define HAVE_WEBVIEW2_HEADER
# endif
#endif

#ifdef HAVE_WEBVIEW2_HEADER
# include <wrl.h>
# include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
// Forward declarations to avoid pulling in WebView2 headers
struct ICoreWebView2;
struct ICoreWebView2Controller;
#endif


class CBlazeClawMFCDoc;
class CBlazeClawMFCView : public CView
{
protected: // create from serialization only
	CBlazeClawMFCView() noexcept;
	DECLARE_DYNCREATE(CBlazeClawMFCView)

// Attributes
public:
	CBlazeClawMFCDoc* GetDocument() const;

// Operations
public:

// Overrides
public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

// Implementation
public:
	virtual ~CBlazeClawMFCView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	// WebView2 members
#ifdef HAVE_WEBVIEW2_HEADER
	ComPtr<ICoreWebView2Controller> m_webViewController;
	ComPtr<ICoreWebView2> m_webView;
#else
	// keep raw pointers when header not available
	ICoreWebView2Controller* m_webViewController = nullptr;
	ICoreWebView2* m_webView = nullptr;
#endif

// Generated message map functions
protected:
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	DECLARE_MESSAGE_MAP()
public:
	virtual void OnInitialUpdate();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
};

#ifndef _DEBUG  // debug version in BlazeClaw.MFCView.cpp
inline CBlazeClawMFCDoc* CBlazeClawMFCView::GetDocument() const
   { return reinterpret_cast<CBlazeClawMFCDoc*>(m_pDocument); }
#endif

