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

// ChildFrm.cpp : implementation of the CChildFrame class
//

#include "pch.h"
#include "framework.h"
#include "BlazeClawMFCApp.h"

#include "ChildFrm.h"
#include "BlazeClawMFCView.h"
#include "ChatView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CChildFrame

IMPLEMENT_DYNCREATE(CChildFrame, CMDIChildWndEx)

BEGIN_MESSAGE_MAP(CChildFrame, CMDIChildWndEx)
	ON_WM_SIZE()
	ON_COMMAND(ID_FILE_PRINT, &CChildFrame::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CChildFrame::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CChildFrame::OnFilePrintPreview)
	ON_UPDATE_COMMAND_UI(ID_FILE_PRINT_PREVIEW, &CChildFrame::OnUpdateFilePrintPreview)
END_MESSAGE_MAP()

// CChildFrame construction/destruction

CChildFrame::CChildFrame() noexcept
{
}

CChildFrame::~CChildFrame()
{
}


BOOL CChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	TRACE(_T("[CChildFrame::PreCreateWindow] ENTER\n"));
	if (!CMDIChildWndEx::PreCreateWindow(cs))
		return FALSE;

	TRACE(_T("[CChildFrame::PreCreateWindow] EXIT\n"));
	return TRUE;
}

BOOL CChildFrame::OnCreateClient(LPCREATESTRUCT /*lpcs*/, CCreateContext* pContext)
{
	// Must match CMDIChildWndEx::OnCreateClient(LPCREATESTRUCT, CCreateContext*) — not LPCREATESTRUCT*.
	// Build the WebView + Chat layout here so the framework never creates a lone template view
	// that we later destroy (that path crashes inside MFC doc/view / MDI activation).
	if (pContext == nullptr || pContext->m_pCurrentDoc == nullptr)
		return FALSE;

	if (!m_wndSplitter.CreateStatic(this, 1, 2))
	{
		TRACE0("[CChildFrame] OnCreateClient: CreateStatic failed\n");
		return FALSE;
	}

	if (!m_wndSplitter.CreateView(0, 0, RUNTIME_CLASS(CBlazeClawMFCView), CSize(100, 100), pContext))
	{
		TRACE0("[CChildFrame] OnCreateClient: CreateView(WebView) failed\n");
		m_wndSplitter.DestroyWindow();
		return FALSE;
	}

	if (!m_wndSplitter.CreateView(0, 1, RUNTIME_CLASS(CChatView), CSize(100, 100), pContext))
	{
		TRACE0("[CChildFrame] OnCreateClient: CreateView(Chat) failed\n");
		m_wndSplitter.DestroyWindow();
		return FALSE;
	}

	m_wndSplitter.SetColumnInfo(0, 700, 100);
	m_wndSplitter.SetColumnInfo(1, 320, 240);
	// Do NOT call RecalcLayout here — it asserts inside winsplit.cpp (line 2350) when called
	// during CreateView WM_SIZE re-entrancy before the splitter HWND is fully initialized.
	// The framework calls MoveWindow on the client area after OnCreateClient returns, which
	// triggers a proper RecalcLayout at that point.
	m_bSplitterReady = TRUE;
	return TRUE;
}

void CChildFrame::OnSize(UINT nType, int cx, int cy)
{
	TRACE(_T("[CChildFrame::OnSize] nType=%d cx=%d cy=%d\n"), nType, cx, cy);
	CMDIChildWndEx::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED)
		return;
	// Guard against re-entrancy during OnCreateClient (splitter still being built) and
	// against calls after the child frame is being destroyed (splitter HWND gone).
	if (!m_bSplitterReady)
		return;
	if (cx <= 0)
		return;
	if (!::IsWindow(m_wndSplitter.m_hWnd))
		return;
	if (m_wndSplitter.GetPane(0, 0) == nullptr || m_wndSplitter.GetPane(0, 1) == nullptr)
		return;

	const int minWeb = 120;
	const int minChat = 240;
	int chatW = cx / 3;
	if (chatW < minChat)
		chatW = minChat;
	if (chatW > cx - minWeb)
		chatW = (cx > minWeb + minChat) ? (cx - minWeb) : minChat;
	int webW = cx - chatW;
	if (webW < minWeb)
	{
		webW = minWeb;
		chatW = cx - webW;
		if (chatW < minChat && cx >= minWeb + minChat)
			chatW = minChat;
	}
	m_wndSplitter.SetColumnInfo(0, webW, minWeb);
	m_wndSplitter.SetColumnInfo(1, chatW, minChat);
	m_wndSplitter.RecalcLayout();
}

// CChildFrame diagnostics

#ifdef _DEBUG
void CChildFrame::AssertValid() const
{
	CMDIChildWndEx::AssertValid();
}

void CChildFrame::Dump(CDumpContext& dc) const
{
	CMDIChildWndEx::Dump(dc);
}
#endif //_DEBUG

// CChildFrame message handlers

void CChildFrame::OnFilePrint()
{
	if (m_dockManager.IsPrintPreviewValid())
	{
		PostMessage(WM_COMMAND, AFX_ID_PREVIEW_PRINT);
	}
}

void CChildFrame::OnFilePrintPreview()
{
	if (m_dockManager.IsPrintPreviewValid())
	{
		PostMessage(WM_COMMAND, AFX_ID_PREVIEW_CLOSE);  // force Print Preview mode closed
	}
}

void CChildFrame::OnUpdateFilePrintPreview(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_dockManager.IsPrintPreviewValid());
}
