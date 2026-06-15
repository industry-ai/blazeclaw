#pragma once
#include <afxwin.h>

class CFloatPaneTracker : public CMiniFrameWnd
{
	DECLARE_DYNAMIC(CFloatPaneTracker)
	
protected:
	afx_msg void	OnWindowPosChanging(WINDOWPOS* lpwndpos);
	afx_msg void	OnWindowPosChanged(WINDOWPOS* lpwndpos);
	DECLARE_MESSAGE_MAP()
};


