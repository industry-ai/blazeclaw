// SharedTabsDocTemplate.cpp : Implementation for two MDI tabs sharing a single document
//

#include "pch.h"
#include "framework.h"
#include "SharedTabsDocTemplate.h"
#include "BlazeClawMFCApp.h"
#include "BlazeClawMFCDoc.h"
#include "BlazeClawMFCView.h"
#include "BlazeClawMarkdownView.h"
#include "SharedDocMarkdownChildFrame.h"
#include "MainFrame.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CSharedTabsDocTemplate, CMultiDocTemplate)

CDocument* CSharedTabsDocTemplate::OpenDocumentFile(
    LPCTSTR lpszPathName,
    BOOL bMakeVisible)
{
    auto* pMainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
    if (!pMainFrame) {
        return CMultiDocTemplate::OpenDocumentFile(lpszPathName, bMakeVisible);
    }
    return CreateSharedTabs(pMainFrame, bMakeVisible);
}

CDocument* CSharedTabsDocTemplate::CreateSharedTabs(CMDIFrameWnd* pMDIFrame, BOOL bMakeVisible)
{
    if (!pMDIFrame)
        return nullptr;

    // Step 1: Create the shared document
    CDocument* pDoc = CreateNewDocument();
    if (!pDoc)
        return nullptr;

    // Step 2: Create WebView tab (first tab) using standard MFC pattern
    // CreateNewFrame(pDoc, pOther) - pOther=null means create new frame
    CFrameWnd* pFrameWebView = CreateNewFrame(pDoc, nullptr);
    if (!pFrameWebView) {
        pDoc->OnCloseDocument();
        return nullptr;
    }
    pFrameWebView->InitialUpdateFrame(pDoc, bMakeVisible);

    // Step 3: Create Markdown tab (second tab) - share the same document
    CFrameWnd* pFrameMd = CreateMarkdownTab(pDoc, pMDIFrame);
    if (!pFrameMd) {
        // Continue anyway, WebView tab was created successfully
    }

    // Step 4: Move the Markdown tab to a new tab group (side-by-side)
    CMDIFrameWndEx* pMDIFrameEx = dynamic_cast<CMDIFrameWndEx*>(pMDIFrame);
    if (pMDIFrameEx && pFrameMd) {
        pMDIFrameEx->MDITabNewGroup(TRUE);
    }
    pMDIFrame->RecalcLayout();

    return pDoc;
}

CFrameWnd* CSharedTabsDocTemplate::CreateMarkdownTab(CDocument* pSharedDoc, CMDIFrameWnd* pMDIFrame)
{
    if (!pSharedDoc || !pMDIFrame || !m_pMarkdownFrameClass)
        return nullptr;

    // Create the Markdown frame
    CSharedDocMarkdownChildFrame* pFrame = dynamic_cast<CSharedDocMarkdownChildFrame*>(
        m_pMarkdownFrameClass->CreateObject());
    if (!pFrame)
        return nullptr;

    // Create context for the markdown view
    CCreateContext context;
    context.m_pCurrentDoc = pSharedDoc;
    context.m_pNewDocTemplate = this;
    context.m_pNewViewClass = RUNTIME_CLASS(CBlazeClawMarkdownView);

    // CMDIChildWnd::Create: BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName,
    //     DWORD dwStyle, const RECT& rect, CMDIFrameWnd* pParentWnd, CCreateContext* pContext = NULL)
    if (!pFrame->Create(nullptr, _T("Markdown"), WS_CHILD | WS_VISIBLE,
        CRect(0, 0, 800, 600), pMDIFrame, &context)) {
        delete pFrame;
        return nullptr;
    }

    pFrame->InitialUpdateFrame(pSharedDoc, FALSE);
    pFrame->ShowWindow(SW_SHOW);

    return pFrame;
}
