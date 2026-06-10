#pragma once

class CDashboardToolBar : public CMFCToolBar
{
public:
	virtual void OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL bDisableIfNoHndler)
	{
		CMFCToolBar::OnUpdateCmdUI((CFrameWnd*) GetOwner(), bDisableIfNoHndler);
	}

	virtual BOOL AllowShowOnList() const { return FALSE; }
};

class CDashboardWnd : public CDockablePane
{
// Construction
public:
	CDashboardWnd() noexcept;

	virtual ~CDashboardWnd();
};

