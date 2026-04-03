// SharedTabsDocTemplate.h : Document template for two MDI tabs sharing a single document
//

#pragma once

#include <afxwin.h>
#include "SharedDocWebViewChildFrame.h"
#include "SharedDocMarkdownChildFrame.h"
#include "BlazeClawMFCView.h"

class CMainFrame;

class CSharedTabsDocTemplate : public CMultiDocTemplate
{
    DECLARE_DYNAMIC(CSharedTabsDocTemplate)

public:
    CSharedTabsDocTemplate(
        UINT nIDResource,
        CRuntimeClass* pDocClass,
        CRuntimeClass* pWebViewFrameClass,
        CRuntimeClass* pMarkdownFrameClass)
        : CMultiDocTemplate(nIDResource, pDocClass, pWebViewFrameClass, RUNTIME_CLASS(CBlazeClawMFCView))
        , m_pMarkdownFrameClass(pMarkdownFrameClass)
    {
    }

    virtual ~CSharedTabsDocTemplate() = default;

    CDocument* CreateSharedTabs(CMDIFrameWnd* pMDIFrame, BOOL bMakeVisible = TRUE);
    CFrameWnd* CreateMarkdownTab(CDocument* pSharedDoc, CMDIFrameWnd* pMDIFrame);

    virtual CDocument* OpenDocumentFile(
        LPCTSTR lpszPathName,
        BOOL bMakeVisible = TRUE) override;

protected:
    CRuntimeClass* m_pMarkdownFrameClass = nullptr;
};
