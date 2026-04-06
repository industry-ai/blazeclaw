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

// BlazeClaw.MFCDoc.cpp : implementation of the CBlazeClawMFCDoc class
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "BlazeClawMFCApp.h"
#endif

#include "BlazeClawMFCDoc.h"

#include <propkey.h>

#include <filesystem>
#include <fstream>
#include <cctype>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

	std::filesystem::path ResolveDocConfigRoot()
	{
		wchar_t profilePath[MAX_PATH]{};
		const DWORD chars = GetEnvironmentVariableW(
			L"USERPROFILE",
			profilePath,
			MAX_PATH);
		if (chars > 0 && chars < MAX_PATH)
		{
			return std::filesystem::path(profilePath) /
				L".config" /
				L"imap-smtp-email";
		}

		return std::filesystem::current_path() /
			L"blazeclaw" /
			L"skills" /
			L"imap-smtp-email";
	}

	std::wstring MakeDocConfigFileName()
	{
		return L".env";
	}

	std::string NormalizeSkillKeyForConfigPath(const std::string& skillKey)
	{
		std::string normalized;
		normalized.reserve(skillKey.size());
		for (const char ch : skillKey)
		{
			const unsigned char raw = static_cast<unsigned char>(ch);
			if (std::isalnum(raw) != 0 || raw == '_')
			{
				normalized.push_back(static_cast<char>(
					std::tolower(raw)));
				continue;
			}

			if (ch == '-' || ch == '.' || ch == '/' || ch == '\\')
			{
				normalized.push_back('-');
			}
		}

		while (!normalized.empty() && normalized.front() == '-')
		{
			normalized.erase(normalized.begin());
		}
		while (!normalized.empty() && normalized.back() == '-')
		{
			normalized.pop_back();
		}

		if (normalized.empty())
		{
			return "unknown-skill";
		}

		return normalized;
	}

	bool SaveConfigFile(
		const std::filesystem::path& path,
		const std::string& content,
		std::string& error)
	{
		error.clear();
		if (content.empty())
		{
			error = "env content is empty";
			return false;
		}

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		std::ofstream output(path, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!output.is_open())
		{
			error = "failed to open config file for writing";
			return false;
		}

		output.write(content.data(), static_cast<std::streamsize>(content.size()));
		if (!output.good())
		{
			error = "failed to write config file";
			return false;
		}

		return true;
	}

	bool LoadConfigFile(
		const std::filesystem::path& path,
		std::string& content,
		std::string& error)
	{
		content.clear();
		error.clear();

		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec)
		{
			error = "config file does not exist";
			return false;
		}

		std::ifstream input(path, std::ios::in | std::ios::binary);
		if (!input.is_open())
		{
			error = "failed to open config file for reading";
			return false;
		}

		content.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!input.good() && !input.eof())
		{
			error = "failed to read config file";
			content.clear();
			return false;
		}

		if (content.empty())
		{
			error = "config file is empty";
			return false;
		}

		return true;
	}

}

// CBlazeClawMFCDoc

IMPLEMENT_DYNCREATE(CBlazeClawMFCDoc, CDocument)

BEGIN_MESSAGE_MAP(CBlazeClawMFCDoc, CDocument)
END_MESSAGE_MAP()


// CBlazeClawMFCDoc construction/destruction

CBlazeClawMFCDoc::CBlazeClawMFCDoc() noexcept
{}

CBlazeClawMFCDoc::~CBlazeClawMFCDoc()
{}

void CBlazeClawMFCDoc::SetMarkdownContent(const std::wstring& content)
{
	m_markdownContent = content;
	UpdateAllViews(nullptr, WM_DOC_MARKDOWN_UPDATE, nullptr);
}

BOOL CBlazeClawMFCDoc::OnNewDocument()
{
	if (!CDocument::OnNewDocument())
		return FALSE;

	const std::filesystem::path root = ResolveDocConfigRoot();
	std::error_code ec;
	std::filesystem::create_directories(root, ec);
	m_emailSkillConfigPath = root / MakeDocConfigFileName();

	// TODO: add reinitialization code here
	// (SDI documents will reuse this document)

	return TRUE;
}

bool CBlazeClawMFCDoc::SaveEmailSkillConfigEnv(
	const std::string& envContent,
	std::string& error)
{
	std::filesystem::path savedPath;
	const bool saved = SaveSkillConfigEnv(
		"imap-smtp-email",
		envContent,
		error,
		&savedPath);
	if (saved)
	{
		m_emailSkillConfigPath = savedPath;
	}

	return saved;
}

bool CBlazeClawMFCDoc::LoadEmailSkillConfigEnv(
	std::string& envContent,
	std::string& error) const
{
	return LoadSkillConfigEnv("imap-smtp-email", envContent, error, nullptr);
}

std::filesystem::path CBlazeClawMFCDoc::GetSkillConfigPath(
	const std::string& skillKey) const
{
	const std::filesystem::path root = ResolveDocConfigRoot().parent_path() /
		std::filesystem::path(NormalizeSkillKeyForConfigPath(skillKey).c_str());
	return root / MakeDocConfigFileName();
}

bool CBlazeClawMFCDoc::SaveSkillConfigEnv(
	const std::string& skillKey,
	const std::string& envContent,
	std::string& error,
	std::filesystem::path* savedPath)
{
	const auto path = GetSkillConfigPath(skillKey);
	if (!SaveConfigFile(path, envContent, error))
	{
		return false;
	}

	if (savedPath != nullptr)
	{
		*savedPath = path;
	}

	if (NormalizeSkillKeyForConfigPath(skillKey) == "imap-smtp-email")
	{
		m_emailSkillConfigPath = path;
	}

	SetModifiedFlag(TRUE);
	return true;
}

bool CBlazeClawMFCDoc::LoadSkillConfigEnv(
	const std::string& skillKey,
	std::string& envContent,
	std::string& error,
	std::filesystem::path* loadedPath) const
{
	std::filesystem::path primaryPath = GetSkillConfigPath(skillKey);
	std::filesystem::path fallbackPath;
	if (NormalizeSkillKeyForConfigPath(skillKey) == "imap-smtp-email")
	{
		fallbackPath = ResolveDocConfigRoot() / MakeDocConfigFileName();
	}

	if (LoadConfigFile(primaryPath, envContent, error))
	{
		if (loadedPath != nullptr)
		{
			*loadedPath = primaryPath;
		}
		return true;
	}

	if (!fallbackPath.empty() && fallbackPath != primaryPath)
	{
		std::string fallbackError;
		if (LoadConfigFile(fallbackPath, envContent, fallbackError))
		{
			if (loadedPath != nullptr)
			{
				*loadedPath = fallbackPath;
			}
			error.clear();
			return true;
		}

		error = fallbackError;
	}

	return false;
}




// CBlazeClawMFCDoc serialization

void CBlazeClawMFCDoc::Serialize(CArchive& ar)
{
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
}

#ifdef SHARED_HANDLERS

// Support for thumbnails
void CBlazeClawMFCDoc::OnDrawThumbnail(CDC& dc, LPRECT lprcBounds)
{
	// Modify this code to draw the document's data
	dc.FillSolidRect(lprcBounds, RGB(255, 255, 255));

	CString strText = _T("TODO: implement thumbnail drawing here");
	LOGFONT lf;

	CFont* pDefaultGUIFont = CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT));
	pDefaultGUIFont->GetLogFont(&lf);
	lf.lfHeight = 36;

	CFont fontDraw;
	fontDraw.CreateFontIndirect(&lf);

	CFont* pOldFont = dc.SelectObject(&fontDraw);
	dc.DrawText(strText, lprcBounds, DT_CENTER | DT_WORDBREAK);
	dc.SelectObject(pOldFont);
}

// Support for Search Handlers
void CBlazeClawMFCDoc::InitializeSearchContent()
{
	CString strSearchContent;
	// Set search contents from document's data.
	// The content parts should be separated by ";"

	// For example:  strSearchContent = _T("point;rectangle;circle;ole object;");
	SetSearchContent(strSearchContent);
}

void CBlazeClawMFCDoc::SetSearchContent(const CString& value)
{
	if (value.IsEmpty())
	{
		RemoveChunk(PKEY_Search_Contents.fmtid, PKEY_Search_Contents.pid);
	}
	else
	{
		CMFCFilterChunkValueImpl* pChunk = nullptr;
		ATLTRY(pChunk = new CMFCFilterChunkValueImpl);
		if (pChunk != nullptr)
		{
			pChunk->SetTextValue(PKEY_Search_Contents, value, CHUNK_TEXT);
			SetChunkValue(pChunk);
		}
	}
}

#endif // SHARED_HANDLERS

// CBlazeClawMFCDoc diagnostics

#ifdef _DEBUG
void CBlazeClawMFCDoc::AssertValid() const
{
	CDocument::AssertValid();
}

void CBlazeClawMFCDoc::Dump(CDumpContext& dc) const
{
	CDocument::Dump(dc);
}
#endif //_DEBUG


// CBlazeClawMFCDoc commands
