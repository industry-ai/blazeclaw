// SharedTabsDocTemplate.cpp : Implementation for two MDI tabs sharing a single document
//

#include "pch.h"
#include "framework.h"
#include "SharedTabsDocTemplate.h"
#include "BlazeClawMFCApp.h"
#include "BlazeClawMFCDoc.h"
#include "BlazeClawMFCView.h"
#include "BlazeClawMarkdownView.h"
#include "BlazeClawAgentChatView.h"
#include "SharedDocMarkdownChildFrame.h"
#include "SharedDocAgentChatChildFrame.h"
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

    CDocument* pDoc = CreateNewDocument();
    if (!pDoc)
        return nullptr;

    CFrameWnd* pFrameWebView = CreateNewFrame(pDoc, nullptr);
    if (!pFrameWebView) {
        pDoc->OnCloseDocument();
        return nullptr;
    }
    pFrameWebView->InitialUpdateFrame(pDoc, bMakeVisible);

    CFrameWnd* pFrameRightPane = CreateRightPaneTab(pDoc, pMDIFrame);

    CMDIFrameWndEx* pMDIFrameEx = dynamic_cast<CMDIFrameWndEx*>(pMDIFrame);
    if (pMDIFrameEx && pFrameRightPane) {
        pMDIFrameEx->MDITabNewGroup(TRUE);
    }
    pMDIFrame->RecalcLayout();

    return pDoc;
}

CFrameWnd* CSharedTabsDocTemplate::CreateRightPaneTab(CDocument* pSharedDoc, CMDIFrameWnd* pMDIFrame)
{
    if (!pSharedDoc || !pMDIFrame || !m_pRightPaneFrameClass)
        return nullptr;

    CFrameWnd* pFrame = nullptr;
    CCreateContext context;
    context.m_pCurrentDoc = pSharedDoc;
    context.m_pNewDocTemplate = this;

    if (m_pRightPaneFrameClass == RUNTIME_CLASS(CSharedDocMarkdownChildFrame)) {
        context.m_pNewViewClass = RUNTIME_CLASS(CBlazeClawMarkdownView);
        auto* pMdFrame = static_cast<CSharedDocMarkdownChildFrame*>(
            m_pRightPaneFrameClass->CreateObject());
        if (!pMdFrame)
            return nullptr;
        if (!pMdFrame->Create(nullptr, _T("Markdown"), WS_CHILD | WS_VISIBLE,
            CRect(0, 0, 800, 600), pMDIFrame, &context)) {
            delete pMdFrame;
            return nullptr;
        }
        pFrame = pMdFrame;
    }
    else if (m_pRightPaneFrameClass == RUNTIME_CLASS(CSharedDocAgentChatChildFrame)) {
        context.m_pNewViewClass = RUNTIME_CLASS(CBlazeClawAgentChatView);
        auto* pAgentFrame = static_cast<CSharedDocAgentChatChildFrame*>(
            m_pRightPaneFrameClass->CreateObject());
        if (!pAgentFrame)
            return nullptr;
        if (!pAgentFrame->Create(nullptr, _T("Agent Chat"), WS_CHILD | WS_VISIBLE,
            CRect(0, 0, 800, 600), pMDIFrame, &context)) {
            delete pAgentFrame;
            return nullptr;
        }
        pFrame = pAgentFrame;
    }
    else {
        return nullptr;
    }

    pFrame->InitialUpdateFrame(pSharedDoc, FALSE);
    pFrame->ShowWindow(SW_SHOW);

    return pFrame;
}
