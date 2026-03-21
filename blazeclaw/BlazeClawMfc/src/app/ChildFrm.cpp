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
	ON_COMMAND(ID_FILE_PRINT, &CChildFrame::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CChildFrame::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CChildFrame::OnFilePrintPreview)
	ON_UPDATE_COMMAND_UI(ID_FILE_PRINT_PREVIEW, &CChildFrame::OnUpdateFilePrintPreview)
END_MESSAGE_MAP()

// CChildFrame construction/destruction

CChildFrame::CChildFrame() noexcept
{
	// TODO: add member initialization code here
}

CChildFrame::~CChildFrame()
{
}


BOOL CChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying the CREATESTRUCT cs
	if( !CMDIChildWndEx::PreCreateWindow(cs) )
		return FALSE;

	return TRUE;
}

BOOL CChildFrame::OnCreateClient(LPCREATESTRUCT lpcs, CCreateContext* pContext)
{
    // Create a static splitter with 1 row and 2 columns.
	if (!m_wndSplitter.CreateStatic(this, 1, 2))
	{
		TRACE0("Failed to create static splitter\n");
		return FALSE;
	}

  // Left pane: document view.
	if (!m_wndSplitter.CreateView(0, 0, RUNTIME_CLASS(CBlazeClawMFCView), CSize(100, 100), pContext))
	{
		TRACE0("Failed to create left pane view\n");
		return FALSE;
	}

  // Right pane: chat view.
	if (!m_wndSplitter.CreateView(0, 1, RUNTIME_CLASS(CChatView), CSize(100, 100), pContext))
	{
		TRACE0("Failed to create right pane view\n");
		return FALSE;
	}

	m_wndSplitter.SetColumnInfo(0, 1200, 20);
	m_wndSplitter.SetColumnInfo(1, 300, 20);
	m_wndSplitter.RecalcLayout();

	return TRUE;
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
