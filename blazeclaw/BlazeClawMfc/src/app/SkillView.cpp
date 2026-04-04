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

#include "pch.h"
#include "framework.h"
#include "MainFrame.h"
#include "SkillView.h"
#include "Resource.h"
#include "BlazeClawMFCApp.h"
#include "../gateway/GatewayJsonUtils.h"

#include <map>

class CSkillViewMenuButton : public CMFCToolBarMenuButton
{
	friend class CSkillView;

	DECLARE_SERIAL(CSkillViewMenuButton)

public:
	CSkillViewMenuButton(HMENU hMenu = nullptr) noexcept : CMFCToolBarMenuButton((UINT)-1, hMenu, -1)
	{}

	virtual void OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz = TRUE,
		BOOL bCustomizeMode = FALSE, BOOL bHighlight = FALSE, BOOL bDrawBorder = TRUE, BOOL bGrayDisabledButtons = TRUE)
	{
		pImages = CMFCToolBar::GetImages();

		CAfxDrawState ds;
		pImages->PrepareDrawImage(ds);

		CMFCToolBarMenuButton::OnDraw(pDC, rect, pImages, bHorz, bCustomizeMode, bHighlight, bDrawBorder, bGrayDisabledButtons);

		pImages->EndDrawImage(ds);
	}
};

IMPLEMENT_SERIAL(CSkillViewMenuButton, CMFCToolBarMenuButton, 1)

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CSkillView::CSkillView() noexcept
{
	m_nCurrSort = ID_SORTING_GROUPBYTYPE;
}

CSkillView::~CSkillView()
{}

BEGIN_MESSAGE_MAP(CSkillView, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_CONTEXTMENU()
	ON_NOTIFY(TVN_SELCHANGED, 2, OnTreeSelectionChanged)
	ON_NOTIFY(NM_DBLCLK, 2, OnTreeItemDoubleClick)
	ON_COMMAND(ID_CLASS_ADD_MEMBER_FUNCTION, OnClassAddMemberFunction)
	ON_COMMAND(ID_CLASS_ADD_MEMBER_VARIABLE, OnClassAddMemberVariable)
	ON_COMMAND(ID_CLASS_DEFINITION, OnClassDefinition)
	ON_COMMAND(ID_CLASS_PROPERTIES, OnClassProperties)
	ON_COMMAND(ID_NEW_FOLDER, OnNewFolder)
	ON_WM_PAINT()
	ON_WM_SETFOCUS()
	ON_COMMAND_RANGE(ID_SORTING_GROUPBYTYPE, ID_SORTING_SORTBYACCESS, OnSort)
	ON_UPDATE_COMMAND_UI_RANGE(ID_SORTING_GROUPBYTYPE, ID_SORTING_SORTBYACCESS, OnUpdateSort)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CSkillView message handlers

int CSkillView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rectDummy;
	rectDummy.SetRectEmpty();

	// Create views:
	const DWORD dwViewStyle = WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

	if (!m_wndSkillView.Create(dwViewStyle, rectDummy, this, 2))
	{
		TRACE0("Failed to create Class View\n");
		return -1;      // fail to create
	}

	// Load images:
	m_wndToolBar.Create(this, AFX_DEFAULT_TOOLBAR_STYLE, IDR_SORT);
	m_wndToolBar.LoadToolBar(IDR_SORT, 0, 0, TRUE /* Is locked */);

	OnChangeVisualStyle();

	m_wndToolBar.SetPaneStyle(m_wndToolBar.GetPaneStyle() | CBRS_TOOLTIPS | CBRS_FLYBY);
	m_wndToolBar.SetPaneStyle(m_wndToolBar.GetPaneStyle() & ~(CBRS_GRIPPER | CBRS_SIZE_DYNAMIC | CBRS_BORDER_TOP | CBRS_BORDER_BOTTOM | CBRS_BORDER_LEFT | CBRS_BORDER_RIGHT));

	m_wndToolBar.SetOwner(this);

	// All commands will be routed via this control , not via the parent frame:
	m_wndToolBar.SetRouteCommandsViaFrame(FALSE);

	CMenu menuSort;
	menuSort.LoadMenu(IDR_POPUP_SORT);

	m_wndToolBar.ReplaceButton(ID_SORT_MENU, CSkillViewMenuButton(menuSort.GetSubMenu(0)->GetSafeHmenu()));

	CSkillViewMenuButton* pButton = DYNAMIC_DOWNCAST(CSkillViewMenuButton, m_wndToolBar.GetButton(0));

	if (pButton != nullptr)
	{
		pButton->m_bText = FALSE;
		pButton->m_bImage = TRUE;
		pButton->SetImage(GetCmdMgr()->GetCmdImage(m_nCurrSort));
		pButton->SetMessageWnd(this);
	}

	// Fill in some static tree view data (dummy code, nothing magic here)
	FillSkillView();

	return 0;
}

void CSkillView::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	AdjustLayout();
}

void CSkillView::FillSkillView()
{
	m_wndSkillView.DeleteAllItems();
	m_skillItemPayloadByTreeItem.clear();

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		return;
	}

	const auto response = app->Services().RouteGatewayRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "skill-view.skills.list",
			.method = "gateway.skills.list",
			.paramsJson = std::string("{\"includeInvalid\":true}"),
		});

	HTREEITEM hRoot = m_wndSkillView.InsertItem(_T("Registered skills"), 0, 0);
	m_wndSkillView.SetItemState(hRoot, TVIS_BOLD, TVIS_BOLD);

	if (!response.ok || !response.payloadJson.has_value())
	{
		m_wndSkillView.InsertItem(_T("(failed to load skills)"), 4, 4, hRoot);
		m_wndSkillView.Expand(hRoot, TVE_EXPAND);
		return;
	}

	std::string skillsRaw;
	if (!blazeclaw::gateway::json::FindRawField(
		response.payloadJson.value(),
		"skills",
		skillsRaw))
	{
		m_wndSkillView.InsertItem(_T("(no skills payload)"), 4, 4, hRoot);
		m_wndSkillView.Expand(hRoot, TVE_EXPAND);
		return;
	}

	const std::string trimmed = blazeclaw::gateway::json::Trim(skillsRaw);
	if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
	{
		m_wndSkillView.InsertItem(_T("(invalid skills format)"), 4, 4, hRoot);
		m_wndSkillView.Expand(hRoot, TVE_EXPAND);
		return;
	}

	std::vector<std::string> skillEntries;
	std::size_t cursor = 1;
	while (cursor + 1 < trimmed.size())
	{
		while (cursor + 1 < trimmed.size() &&
			(std::isspace(static_cast<unsigned char>(trimmed[cursor])) != 0 ||
				trimmed[cursor] == ','))
		{
			++cursor;
		}

		if (cursor + 1 >= trimmed.size() || trimmed[cursor] != '{')
		{
			break;
		}

		const std::size_t begin = cursor;
		int depth = 0;
		bool inString = false;
		for (; cursor < trimmed.size(); ++cursor)
		{
			const char ch = trimmed[cursor];
			if (inString)
			{
				if (ch == '\\')
				{
					++cursor;
					continue;
				}

				if (ch == '"')
				{
					inString = false;
				}
				continue;
			}

			if (ch == '"')
			{
				inString = true;
				continue;
			}

			if (ch == '{')
			{
				++depth;
			}
			else if (ch == '}')
			{
				--depth;
				if (depth == 0)
				{
					skillEntries.push_back(
						trimmed.substr(begin, cursor - begin + 1));
					++cursor;
					break;
				}
			}
		}
	}

	std::map<std::string, HTREEITEM> categoryItems;
	for (const auto& entryJson : skillEntries)
	{
		std::string skillKey;
		blazeclaw::gateway::json::FindStringField(entryJson, "skillKey", skillKey);
		if (skillKey.empty())
		{
			blazeclaw::gateway::json::FindStringField(entryJson, "name", skillKey);
		}
		if (skillKey.empty())
		{
			continue;
		}

		std::string category;
		blazeclaw::gateway::json::FindStringField(entryJson, "installKind", category);
		if (category.empty())
		{
			category = "general";
		}

		auto categoryIt = categoryItems.find(category);
		HTREEITEM categoryNode = nullptr;
		if (categoryIt == categoryItems.end())
		{
			categoryNode = m_wndSkillView.InsertItem(
				CString(CA2W(category.c_str(), CP_UTF8)),
				1,
				1,
				hRoot);
			categoryItems.insert_or_assign(category, categoryNode);
		}
		else
		{
			categoryNode = categoryIt->second;
		}

		const HTREEITEM skillNode = m_wndSkillView.InsertItem(
			CString(CA2W(skillKey.c_str(), CP_UTF8)),
			2,
			2,
			categoryNode);
		m_skillItemPayloadByTreeItem.insert_or_assign(skillNode, entryJson);
	}

	for (const auto& categoryEntry : categoryItems)
	{
		m_wndSkillView.Expand(categoryEntry.second, TVE_EXPAND);
	}
	m_wndSkillView.Expand(hRoot, TVE_EXPAND);
}

void CSkillView::NotifySelectionToChatView(HTREEITEM selectedItem)
{
	if (selectedItem == nullptr)
	{
		return;
	}

	const auto payloadIt = m_skillItemPayloadByTreeItem.find(selectedItem);
	if (payloadIt == m_skillItemPayloadByTreeItem.end())
	{
		return;
	}

	std::string skillKey;
	blazeclaw::gateway::json::FindStringField(
		payloadIt->second,
		"skillKey",
		skillKey);
	if (skillKey.empty())
	{
		blazeclaw::gateway::json::FindStringField(
			payloadIt->second,
			"name",
			skillKey);
	}

	auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (mainFrame == nullptr)
	{
		return;
	}

	mainFrame->ShowSkillSelectionInActiveView(skillKey, payloadIt->second);
}

void CSkillView::OnContextMenu(CWnd* pWnd, CPoint point)
{
	CTreeCtrl* pWndTree = (CTreeCtrl*)&m_wndSkillView;
	ASSERT_VALID(pWndTree);

	if (pWnd != pWndTree)
	{
		CDockablePane::OnContextMenu(pWnd, point);
		return;
	}

	if (point != CPoint(-1, -1))
	{
		// Select clicked item:
		CPoint ptTree = point;
		pWndTree->ScreenToClient(&ptTree);

		UINT flags = 0;
		HTREEITEM hTreeItem = pWndTree->HitTest(ptTree, &flags);
		if (hTreeItem != nullptr)
		{
			pWndTree->SelectItem(hTreeItem);
		}
	}

	pWndTree->SetFocus();
	CMenu menu;
	menu.LoadMenu(IDR_POPUP_SORT);

	CMenu* pSumMenu = menu.GetSubMenu(0);

	if (AfxGetMainWnd()->IsKindOf(RUNTIME_CLASS(CMDIFrameWndEx)))
	{
		CMFCPopupMenu* pPopupMenu = new CMFCPopupMenu;

		if (!pPopupMenu->Create(this, point.x, point.y, (HMENU)pSumMenu->m_hMenu, FALSE, TRUE))
			return;

		((CMDIFrameWndEx*)AfxGetMainWnd())->OnShowPopupMenu(pPopupMenu);
		UpdateDialogControls(this, FALSE);
	}
}

void CSkillView::AdjustLayout()
{
	if (GetSafeHwnd() == nullptr)
	{
		return;
	}

	CRect rectClient;
	GetClientRect(rectClient);

	int cyTlb = m_wndToolBar.CalcFixedLayout(FALSE, TRUE).cy;

	m_wndToolBar.SetWindowPos(nullptr, rectClient.left, rectClient.top, rectClient.Width(), cyTlb, SWP_NOACTIVATE | SWP_NOZORDER);
	m_wndSkillView.SetWindowPos(nullptr, rectClient.left + 1, rectClient.top + cyTlb + 1, rectClient.Width() - 2, rectClient.Height() - cyTlb - 2, SWP_NOACTIVATE | SWP_NOZORDER);
}

BOOL CSkillView::PreTranslateMessage(MSG* pMsg)
{
	return CDockablePane::PreTranslateMessage(pMsg);
}

void CSkillView::OnSort(UINT id)
{
	if (m_nCurrSort == id)
	{
		return;
	}

	m_nCurrSort = id;

	CSkillViewMenuButton* pButton = DYNAMIC_DOWNCAST(CSkillViewMenuButton, m_wndToolBar.GetButton(0));

	if (pButton != nullptr)
	{
		pButton->SetImage(GetCmdMgr()->GetCmdImage(id));
		m_wndToolBar.Invalidate();
		m_wndToolBar.UpdateWindow();
	}
}

void CSkillView::OnUpdateSort(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(pCmdUI->m_nID == m_nCurrSort);
}

void CSkillView::OnClassAddMemberFunction()
{
	AfxMessageBox(_T("Add member function..."));
}

void CSkillView::OnClassAddMemberVariable()
{
	// TODO: Add your command handler code here
}

void CSkillView::OnClassDefinition()
{
	// TODO: Add your command handler code here
}

void CSkillView::OnClassProperties()
{
	// TODO: Add your command handler code here
}

void CSkillView::OnNewFolder()
{
	AfxMessageBox(_T("New Folder..."));
}

void CSkillView::OnPaint()
{
	CPaintDC dc(this); // device context for painting

	CRect rectTree;
	m_wndSkillView.GetWindowRect(rectTree);
	ScreenToClient(rectTree);

	rectTree.InflateRect(1, 1);
	dc.Draw3dRect(rectTree, ::GetSysColor(COLOR_3DSHADOW), ::GetSysColor(COLOR_3DSHADOW));
}

void CSkillView::OnSetFocus(CWnd* pOldWnd)
{
	CDockablePane::OnSetFocus(pOldWnd);

	m_wndSkillView.SetFocus();
}

void CSkillView::OnTreeSelectionChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	NotifySelectionToChatView(m_wndSkillView.GetSelectedItem());
	if (pResult != nullptr)
	{
		*pResult = 0;
	}
}

void CSkillView::OnTreeItemDoubleClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(pNMHDR);
	NotifySelectionToChatView(m_wndSkillView.GetSelectedItem());
	if (pResult != nullptr)
	{
		*pResult = 0;
	}
}

void CSkillView::OnChangeVisualStyle()
{
	m_SkillViewImages.DeleteImageList();

	UINT uiBmpId = theApp.m_bHiColorIcons ? IDB_CLASS_VIEW_24 : IDB_CLASS_VIEW;

	CBitmap bmp;
	if (!bmp.LoadBitmap(uiBmpId))
	{
		TRACE(_T("Can't load bitmap: %x\n"), uiBmpId);
		ASSERT(FALSE);
		return;
	}

	BITMAP bmpObj;
	bmp.GetBitmap(&bmpObj);

	UINT nFlags = ILC_MASK;

	nFlags |= (theApp.m_bHiColorIcons) ? ILC_COLOR24 : ILC_COLOR4;

	m_SkillViewImages.Create(16, bmpObj.bmHeight, nFlags, 0, 0);
	m_SkillViewImages.Add(&bmp, RGB(255, 0, 0));

	m_wndSkillView.SetImageList(&m_SkillViewImages, TVSIL_NORMAL);

	m_wndToolBar.CleanUpLockedImages();
	m_wndToolBar.LoadBitmap(theApp.m_bHiColorIcons ? IDB_SORT_24 : IDR_SORT, 0, 0, TRUE /* Locked */);
}
