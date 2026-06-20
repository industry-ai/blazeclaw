#include "pch.h"
#include "FloatPaneTracker.h"


IMPLEMENT_DYNAMIC(CFloatPaneTracker, CMiniFrameWnd)

BEGIN_MESSAGE_MAP(CFloatPaneTracker, CMiniFrameWnd)
//    ON_WM_WINDOWPOSCHANGED()
//	ON_WM_WINDOWPOSCHANGING()
END_MESSAGE_MAP()

void CFloatPaneTracker::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
    CMiniFrameWnd::OnWindowPosChanged(lpwndpos);

    // Ignore pure Z‑order or visibility changes
    if (lpwndpos->flags & SWP_NOMOVE)
        return;

    //CRect rcScreen;
    //SystemParametersInfo(SPI_GETWORKAREA, 0, &rcScreen, 0);
    //lpwndpos->x = max(rcScreen.left, min(lpwndpos->x, rcScreen.right - lpwndpos->cx));
    //lpwndpos->y = max(rcScreen.top, min(lpwndpos->y, rcScreen.bottom - lpwndpos->cy));

    CRect rc(
        lpwndpos->x, lpwndpos->y,
        lpwndpos->x + lpwndpos->cx,
        lpwndpos->y + lpwndpos->cy);

    // rc is the FINAL screen rect of the floating pane
    TRACE(_T("Floated to (%d,%d) size %dx%d\n"),
        rc.left, rc.top, rc.Width(), rc.Height());

    // Example: notify owning pane
    CDockablePane* pPane =
        DYNAMIC_DOWNCAST(CDockablePane, GetWindow(GW_CHILD));

    if (pPane)
    {
        // Store final position, update UI, etc.
    }
}

void CFloatPaneTracker::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
    CMiniFrameWnd::OnWindowPosChanging(lpwndpos);

    // Dragging happens here
    int x = lpwndpos->x;
    int y = lpwndpos->y;
}
