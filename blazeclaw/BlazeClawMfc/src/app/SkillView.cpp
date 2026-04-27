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

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <vector>
#include <nlohmann/json.hpp>

namespace {
	using Json = nlohmann::json;

	std::string NormalizeSkillKeyForDedup(const std::string& value)
	{
		std::string normalized;
		normalized.reserve(value.size());
		for (const char ch : value)
		{
			if (ch == '-')
			{
				normalized.push_back('_');
				continue;
			}

			normalized.push_back(static_cast<char>(
				std::tolower(static_cast<unsigned char>(ch))));
		}

		return normalized;
	}

	std::string BuildCanonicalSkillPayload(
		const std::string& skillKey,
		const std::string& sourcePayloadJson,
		const std::string& defaultSource,
		const std::string& defaultInstallKind,
		const std::string& defaultDescription)
	{
		Json payload = Json::object();
		if (!sourcePayloadJson.empty())
		{
			payload = Json::parse(sourcePayloadJson, nullptr, false);
			if (payload.is_discarded() || !payload.is_object())
			{
				payload = Json::object();
			}
		}

		if (!payload.contains("name") || !payload["name"].is_string() || payload["name"].get<std::string>().empty())
		{
			payload["name"] = skillKey;
		}
		if (!payload.contains("skillKey") || !payload["skillKey"].is_string() || payload["skillKey"].get<std::string>().empty())
		{
			payload["skillKey"] = skillKey;
		}
		if (!payload.contains("source") || !payload["source"].is_string())
		{
			payload["source"] = defaultSource;
		}
		if (!payload.contains("installKind") || !payload["installKind"].is_string())
		{
			payload["installKind"] = defaultInstallKind;
		}
		if (!payload.contains("description") || !payload["description"].is_string())
		{
			payload["description"] = defaultDescription;
		}

		if (!payload.contains("primaryEnv") || !payload["primaryEnv"].is_string())
		{
			payload["primaryEnv"] = "";
		}
		if (!payload.contains("requiresEnv") || !payload["requiresEnv"].is_array())
		{
			payload["requiresEnv"] = Json::array();
		}
		if (!payload.contains("requiresConfig") || !payload["requiresConfig"].is_array())
		{
			payload["requiresConfig"] = Json::array();
		}
		if (!payload.contains("configPathHints") || !payload["configPathHints"].is_array())
		{
			payload["configPathHints"] = Json::array();
		}
		if (!payload.contains("openclawOriginalActivationState") || !payload["openclawOriginalActivationState"].is_string())
		{
			payload["openclawOriginalActivationState"] = "";
		}
		if (!payload.contains("openclawOriginalImportDiagnostics") || !payload["openclawOriginalImportDiagnostics"].is_array())
		{
			payload["openclawOriginalImportDiagnostics"] = Json::array();
		}
		if (!payload.contains("openclawOriginalMissingToolManifest") || !payload["openclawOriginalMissingToolManifest"].is_boolean())
		{
			payload["openclawOriginalMissingToolManifest"] = false;
		}
		if (!payload.contains("openclawOriginalMetadataConvertedFromClawdbot") || !payload["openclawOriginalMetadataConvertedFromClawdbot"].is_boolean())
		{
			payload["openclawOriginalMetadataConvertedFromClawdbot"] = false;
		}

		return payload.dump();
	}

	std::string ResolveOpenClawOriginalGroupFromPayload(const std::string& payloadJson)
	{
		Json payload = Json::parse(payloadJson, nullptr, false);
		if (payload.is_discarded() || !payload.is_object())
		{
			return "needs-porting";
		}

		const std::string activation = payload.value("openclawOriginalActivationState", "");
		if (activation == "failed")
		{
			return "failed";
		}
		if (activation == "tool_enabled")
		{
			return "enabled";
		}
		return "needs-porting";
	}

	std::string BuildOpenClawOriginalDisplayName(
		const std::string& skillKey,
		const std::string& payloadJson)
	{
		Json payload = Json::parse(payloadJson, nullptr, false);
		if (payload.is_discarded() || !payload.is_object())
		{
			return skillKey;
		}

		std::string label = skillKey;
		if (payload.value("openclawOriginalMissingToolManifest", false))
		{
			label += " [missing tool-manifest.json]";
		}
		if (payload.value("openclawOriginalMetadataConvertedFromClawdbot", false))
		{
			label += " [metadata converted from clawdbot]";
		}
		return label;
	}

	std::vector<std::string> SplitTopLevelObjects(const std::string& arrayJson)
	{
		std::vector<std::string> objects;
		const std::string trimmed = blazeclaw::gateway::json::Trim(arrayJson);
		if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
		{
			return objects;
		}

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
						objects.push_back(trimmed.substr(begin, cursor - begin + 1));
						++cursor;
						break;
					}
				}
			}
		}

		return objects;
	}

	std::optional<std::filesystem::path> ResolveSkillsRoot()
	{
		std::error_code ec;
		std::filesystem::path cursor = std::filesystem::current_path(ec);
		if (ec)
		{
			return std::nullopt;
		}

		while (!cursor.empty())
		{
			const auto directSkills = cursor / "skills";
			if (std::filesystem::exists(directSkills, ec) &&
				std::filesystem::is_directory(directSkills, ec))
			{
				return directSkills;
			}

			const auto nestedSkills = cursor / "blazeclaw" / "skills";
			if (std::filesystem::exists(nestedSkills, ec) &&
				std::filesystem::is_directory(nestedSkills, ec))
			{
				return nestedSkills;
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}

	std::optional<std::filesystem::path> ResolveOpenClawSkillsRoot()
	{
		std::error_code ec;
		std::filesystem::path cursor = std::filesystem::current_path(ec);
		if (ec)
		{
			return std::nullopt;
		}

		while (!cursor.empty())
		{
			const auto directOpenClawSkills =
				cursor / "blazeclaw" / "skills-openclaw-original";
			if (std::filesystem::exists(directOpenClawSkills, ec) &&
				std::filesystem::is_directory(directOpenClawSkills, ec))
			{
				return directOpenClawSkills;
			}

			const auto nestedOpenClawSkills =
				cursor / "skills-openclaw-original";
			if (std::filesystem::exists(nestedOpenClawSkills, ec) &&
				std::filesystem::is_directory(nestedOpenClawSkills, ec))
			{
				return nestedOpenClawSkills;
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}
}

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
		TRACE0("Failed to create Skill View\n");
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

	const auto response = app->RouteGatewayRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "skill-view.skills.list",
			.method = "gateway.skills.list",
			.paramsJson = std::string("{\"includeInvalid\":true}"),
		});

	HTREEITEM hRoot = m_wndSkillView.InsertItem(_T("Registered skills"), 0, 0);
	m_wndSkillView.SetItemState(hRoot, TVIS_BOLD, TVIS_BOLD);

	std::string skillsRaw;
	if (response.ok &&
		response.payloadJson.has_value())
	{
		blazeclaw::gateway::json::FindRawField(
			response.payloadJson.value(),
			"skills",
			skillsRaw);
	}

	std::vector<std::string> skillEntries = SplitTopLevelObjects(skillsRaw);
	std::unordered_map<std::string, std::string> catalogPayloadBySkillKey;

	std::map<std::string, HTREEITEM> categoryItems;
	std::set<std::string> knownSkillKeys;
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
		const std::string normalizedSkillKey = NormalizeSkillKeyForDedup(skillKey);
		knownSkillKeys.insert(normalizedSkillKey);
		catalogPayloadBySkillKey.insert_or_assign(
			normalizedSkillKey,
			BuildCanonicalSkillPayload(
				skillKey,
				entryJson,
				"gateway.skills.list",
				"runtime-registered",
				"Discovered from runtime skills catalog."));

		std::string category;
		blazeclaw::gateway::json::FindStringField(entryJson, "installKind", category);
		std::string sourceKind;
		blazeclaw::gateway::json::FindStringField(entryJson, "source", sourceKind);
		const bool isOpenClawOriginal = sourceKind == "openclaw-original";
		if (isOpenClawOriginal)
		{
			const std::string parentCategory = "openclaw-original";
			auto parentCategoryIt = categoryItems.find(parentCategory);
			HTREEITEM parentCategoryNode = nullptr;
			if (parentCategoryIt == categoryItems.end())
			{
				parentCategoryNode = m_wndSkillView.InsertItem(
					_T("openclaw-original"),
					1,
					1,
					hRoot);
				categoryItems.insert_or_assign(parentCategory, parentCategoryNode);
			}
			else
			{
				parentCategoryNode = parentCategoryIt->second;
			}

			const auto catalogPayloadIt = catalogPayloadBySkillKey.find(normalizedSkillKey);
			const std::string payload = BuildCanonicalSkillPayload(
				skillKey,
				catalogPayloadIt != catalogPayloadBySkillKey.end()
				? catalogPayloadIt->second
				: entryJson,
				"gateway.skills.list",
				"openclaw-original",
				"Discovered from openclaw-original skills catalog.");
			const std::string openClawGroup =
				ResolveOpenClawOriginalGroupFromPayload(payload);
			const std::string openClawGroupKey =
				std::string("openclaw-original/") + openClawGroup;
			auto groupIt = categoryItems.find(openClawGroupKey);
			HTREEITEM groupNode = nullptr;
			if (groupIt == categoryItems.end())
			{
				groupNode = m_wndSkillView.InsertItem(
					CString(CA2W(openClawGroup.c_str(), CP_UTF8)),
					1,
					1,
					parentCategoryNode);
				categoryItems.insert_or_assign(openClawGroupKey, groupNode);
			}
			else
			{
				groupNode = groupIt->second;
			}

			const std::string displayName =
				BuildOpenClawOriginalDisplayName(skillKey, payload);
			const HTREEITEM skillNode = m_wndSkillView.InsertItem(
				CString(CA2W(displayName.c_str(), CP_UTF8)),
				2,
				2,
				groupNode);
			m_skillItemPayloadByTreeItem.insert_or_assign(skillNode, payload);
			continue;
		}
		if (category.empty() || category == "general")
		{
			const std::string runtimeCategory = "runtime-registered";
			auto runtimeCategoryIt = categoryItems.find(runtimeCategory);
			HTREEITEM runtimeCategoryNode = nullptr;
			if (runtimeCategoryIt == categoryItems.end())
			{
				runtimeCategoryNode = m_wndSkillView.InsertItem(
					_T("runtime-registered"),
					1,
					1,
					hRoot);
				categoryItems.insert_or_assign(runtimeCategory, runtimeCategoryNode);
			}
			else
			{
				runtimeCategoryNode = runtimeCategoryIt->second;
			}

			const std::string generalCategory = "runtime-registered/general";
			auto generalCategoryIt = categoryItems.find(generalCategory);
			HTREEITEM generalCategoryNode = nullptr;
			if (generalCategoryIt == categoryItems.end())
			{
				generalCategoryNode = m_wndSkillView.InsertItem(
					_T("general"),
					1,
					1,
					runtimeCategoryNode);
				categoryItems.insert_or_assign(generalCategory, generalCategoryNode);
			}
			else
			{
				generalCategoryNode = generalCategoryIt->second;
			}

			const HTREEITEM skillNode = m_wndSkillView.InsertItem(
				CString(CA2W(skillKey.c_str(), CP_UTF8)),
				2,
				2,
				generalCategoryNode);
			m_skillItemPayloadByTreeItem.insert_or_assign(
				skillNode,
				BuildCanonicalSkillPayload(
					skillKey,
					entryJson,
					"gateway.skills.list",
					"runtime-registered/general",
					"Discovered from runtime skills catalog."));
			continue;
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
		m_skillItemPayloadByTreeItem.insert_or_assign(
			skillNode,
			BuildCanonicalSkillPayload(
				skillKey,
				entryJson,
				"gateway.skills.list",
				category,
				"Discovered from runtime skills catalog."));
	}

	const auto toolsResponse = app->RouteGatewayRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "skill-view.tools.list",
			.method = "gateway.tools.list",
			.paramsJson = std::nullopt,
		});
	if (!toolsResponse.ok)
	{
		const auto toolsCatalogResponse = app->RouteGatewayRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "skill-view.tools.catalog",
				.method = "gateway.tools.catalog",
				.paramsJson = std::nullopt,
			});
		if (toolsCatalogResponse.ok)
		{
			std::string toolsRaw;
			if (toolsCatalogResponse.payloadJson.has_value() &&
				blazeclaw::gateway::json::FindRawField(
					toolsCatalogResponse.payloadJson.value(),
					"tools",
					toolsRaw))
			{
				for (const auto& toolJson : SplitTopLevelObjects(toolsRaw))
				{
					std::string toolId;
					if (!blazeclaw::gateway::json::FindStringField(
						toolJson,
						"id",
						toolId) ||
						toolId.empty())
					{
						continue;
					}

					std::string skillKey = toolId;
					const auto dot = toolId.find('.');
					if (dot != std::string::npos && dot > 0)
					{
						skillKey = toolId.substr(0, dot);
					}

					const std::string dedupKey =
						NormalizeSkillKeyForDedup(skillKey);
					if (knownSkillKeys.find(dedupKey) != knownSkillKeys.end())
					{
						continue;
					}

					knownSkillKeys.insert(dedupKey);
					const std::string category = "runtime-registered";
					auto categoryIt = categoryItems.find(category);
					HTREEITEM categoryNode = nullptr;
					if (categoryIt == categoryItems.end())
					{
						categoryNode = m_wndSkillView.InsertItem(
							_T("runtime-registered"),
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
					const auto catalogPayloadIt = catalogPayloadBySkillKey.find(dedupKey);
					const std::string payload = BuildCanonicalSkillPayload(
						skillKey,
						catalogPayloadIt != catalogPayloadBySkillKey.end()
						? catalogPayloadIt->second
						: std::string(),
						"gateway.tools.catalog",
						"runtime-registered",
						"Derived from runtime tool registry");
					m_skillItemPayloadByTreeItem.insert_or_assign(skillNode, payload);
				}
			}

			const auto openClawSkillsRoot = ResolveOpenClawSkillsRoot();
			if (openClawSkillsRoot.has_value())
			{
				std::error_code ec;
				for (const auto& entry : std::filesystem::directory_iterator(openClawSkillsRoot.value(), ec))
				{
					if (ec || !entry.is_directory())
					{
						continue;
					}

					const auto skillDir = entry.path();
					const auto toolManifestPath = skillDir / "tool-manifest.json";
					const auto skillDocPath = skillDir / "SKILL.md";
					if (!std::filesystem::exists(toolManifestPath, ec) &&
						!std::filesystem::exists(skillDocPath, ec))
					{
						continue;
					}

					std::string skillKey = skillDir.filename().string();
					if (std::filesystem::exists(toolManifestPath, ec))
					{
						std::ifstream manifestIn(toolManifestPath, std::ios::binary);
						if (manifestIn.is_open())
						{
							std::string manifestText(
								(std::istreambuf_iterator<char>(manifestIn)),
								std::istreambuf_iterator<char>());
							std::string manifestNamespace;
							if (blazeclaw::gateway::json::FindStringField(
								manifestText,
								"namespace",
								manifestNamespace) &&
								!manifestNamespace.empty())
							{
								skillKey = manifestNamespace;
							}
						}
					}

					if (knownSkillKeys.find(NormalizeSkillKeyForDedup(skillKey)) != knownSkillKeys.end())
					{
						continue;
					}

					knownSkillKeys.insert(NormalizeSkillKeyForDedup(skillKey));
					const std::string parentCategory = "openclaw-original";
					auto parentCategoryIt = categoryItems.find(parentCategory);
					HTREEITEM parentCategoryNode = nullptr;
					if (parentCategoryIt == categoryItems.end())
					{
						parentCategoryNode = m_wndSkillView.InsertItem(
							_T("openclaw-original"),
							1,
							1,
							hRoot);
						categoryItems.insert_or_assign(parentCategory, parentCategoryNode);
					}
					else
					{
						parentCategoryNode = parentCategoryIt->second;
					}

					const auto catalogPayloadIt = catalogPayloadBySkillKey.find(
						NormalizeSkillKeyForDedup(skillKey));
					const std::string payload = BuildCanonicalSkillPayload(
						skillKey,
						catalogPayloadIt != catalogPayloadBySkillKey.end()
						? catalogPayloadIt->second
						: std::string(),
						"openclaw.filesystem",
						"openclaw-original",
						"Discovered from openclaw/skills with original assets");
					const std::string openClawGroup =
						ResolveOpenClawOriginalGroupFromPayload(payload);
					const std::string openClawGroupKey =
						std::string("openclaw-original/") + openClawGroup;
					auto groupIt = categoryItems.find(openClawGroupKey);
					HTREEITEM groupNode = nullptr;
					if (groupIt == categoryItems.end())
					{
						groupNode = m_wndSkillView.InsertItem(
							CString(CA2W(openClawGroup.c_str(), CP_UTF8)),
							1,
							1,
							parentCategoryNode);
						categoryItems.insert_or_assign(openClawGroupKey, groupNode);
					}
					else
					{
						groupNode = groupIt->second;
					}

					const std::string displayName =
						BuildOpenClawOriginalDisplayName(skillKey, payload);
					const HTREEITEM skillNode = m_wndSkillView.InsertItem(
						CString(CA2W(displayName.c_str(), CP_UTF8)),
						2,
						2,
						groupNode);
					m_skillItemPayloadByTreeItem.insert_or_assign(skillNode, payload);
				}
			}
		}
	}
	if (toolsResponse.ok && toolsResponse.payloadJson.has_value())
	{
		std::string toolsRaw;
		if (blazeclaw::gateway::json::FindRawField(
			toolsResponse.payloadJson.value(),
			"tools",
			toolsRaw))
		{
			for (const auto& toolJson : SplitTopLevelObjects(toolsRaw))
			{
				std::string toolId;
				if (!blazeclaw::gateway::json::FindStringField(
					toolJson,
					"id",
					toolId) ||
					toolId.empty())
				{
					continue;
				}

				std::string skillKey = toolId;
				const auto dot = toolId.find('.');
				if (dot != std::string::npos && dot > 0)
				{
					skillKey = toolId.substr(0, dot);
				}

				const std::string dedupKey =
					NormalizeSkillKeyForDedup(skillKey);
				if (knownSkillKeys.find(dedupKey) != knownSkillKeys.end())
				{
					continue;
				}

				knownSkillKeys.insert(dedupKey);
				const std::string category = "runtime-registered";
				auto categoryIt = categoryItems.find(category);
				HTREEITEM categoryNode = nullptr;
				if (categoryIt == categoryItems.end())
				{
					categoryNode = m_wndSkillView.InsertItem(
						_T("runtime-registered"),
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
				const auto catalogPayloadIt = catalogPayloadBySkillKey.find(dedupKey);
				const std::string payload = BuildCanonicalSkillPayload(
					skillKey,
					catalogPayloadIt != catalogPayloadBySkillKey.end()
					? catalogPayloadIt->second
					: std::string(),
					"gateway.tools.list",
					"runtime-registered",
					"Derived from runtime tool registry");
				m_skillItemPayloadByTreeItem.insert_or_assign(skillNode, payload);
			}
		}
	}

	const auto skillsRoot = ResolveSkillsRoot();
	if (skillsRoot.has_value())
	{
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(skillsRoot.value(), ec))
		{
			if (ec || !entry.is_directory())
			{
				continue;
			}

			const auto skillDir = entry.path();
			const auto toolManifestPath = skillDir / "tool-manifest.json";
			const auto skillDocPath = skillDir / "SKILL.md";
			if (!std::filesystem::exists(toolManifestPath, ec) &&
				!std::filesystem::exists(skillDocPath, ec))
			{
				continue;
			}

			std::string skillKey = skillDir.filename().string();
			if (std::filesystem::exists(toolManifestPath, ec))
			{
				std::ifstream manifestIn(toolManifestPath, std::ios::binary);
				if (manifestIn.is_open())
				{
					std::string manifestText(
						(std::istreambuf_iterator<char>(manifestIn)),
						std::istreambuf_iterator<char>());
					std::string manifestNamespace;
					if (blazeclaw::gateway::json::FindStringField(
						manifestText,
						"namespace",
						manifestNamespace) &&
						!manifestNamespace.empty())
					{
						skillKey = manifestNamespace;
					}
				}
			}

			if (knownSkillKeys.find(NormalizeSkillKeyForDedup(skillKey)) != knownSkillKeys.end())
			{
				continue;
			}

			knownSkillKeys.insert(NormalizeSkillKeyForDedup(skillKey));
			const std::string category = "implemented";
			auto categoryIt = categoryItems.find(category);
			HTREEITEM categoryNode = nullptr;
			if (categoryIt == categoryItems.end())
			{
				categoryNode = m_wndSkillView.InsertItem(
					_T("implemented"),
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
			const auto catalogPayloadIt = catalogPayloadBySkillKey.find(
				NormalizeSkillKeyForDedup(skillKey));
			const std::string payload = BuildCanonicalSkillPayload(
				skillKey,
				catalogPayloadIt != catalogPayloadBySkillKey.end()
				? catalogPayloadIt->second
				: std::string(),
				"filesystem",
				"implemented",
				"Discovered from local skills directory");
			m_skillItemPayloadByTreeItem.insert_or_assign(skillNode, payload);
		}
	}

	if (!response.ok)
	{
		m_wndSkillView.InsertItem(
			_T("(runtime skills unavailable; showing local skills only)"),
			4,
			4,
			hRoot);
	}

	if (m_skillItemPayloadByTreeItem.empty())
	{
		m_wndSkillView.InsertItem(_T("(no registered or implemented skills found)"), 4, 4, hRoot);
	}

	for (const auto& categoryEntry : categoryItems)
	{
		m_wndSkillView.Expand(categoryEntry.second, TVE_EXPAND);
	}
	m_wndSkillView.Expand(hRoot, TVE_EXPAND);
}

void CSkillView::RefreshSkills()
{
	FillSkillView();
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

	// Use OpenSkillViewTab to create a new tab with ChatView hidden
	mainFrame->OpenSkillViewTab(skillKey, payloadIt->second);
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
	// Don't open a new tab on double-click, single click already opened one
	// Just focus the existing tab
	auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (mainFrame != nullptr)
	{
		CMDIChildWndEx* activeChild = DYNAMIC_DOWNCAST(CMDIChildWndEx, mainFrame->MDIGetActive());
		if (activeChild != nullptr)
		{
			activeChild->BringWindowToTop();
		}
	}
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
