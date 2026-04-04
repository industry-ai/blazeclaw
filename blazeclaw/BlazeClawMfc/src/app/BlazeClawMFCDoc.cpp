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
	error.clear();
	if (envContent.empty())
	{
		error = "env content is empty";
		return false;
	}

	if (m_emailSkillConfigPath.empty())
	{
		const std::filesystem::path root = ResolveDocConfigRoot();
		std::error_code ec;
		std::filesystem::create_directories(root, ec);
		m_emailSkillConfigPath = root / MakeDocConfigFileName();
	}

	std::error_code ec;
	std::filesystem::create_directories(m_emailSkillConfigPath.parent_path(), ec);

	std::ofstream output(
		m_emailSkillConfigPath,
		std::ios::out | std::ios::trunc | std::ios::binary);
	if (!output.is_open())
	{
		error = "failed to open email config file for writing";
		return false;
	}

	output.write(envContent.data(), static_cast<std::streamsize>(envContent.size()));
	if (!output.good())
	{
		error = "failed to write email config file";
		return false;
	}

	SetModifiedFlag(TRUE);
	return true;
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
