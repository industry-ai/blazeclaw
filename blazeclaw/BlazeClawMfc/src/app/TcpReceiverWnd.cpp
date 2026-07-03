#include "pch.h"
#include "framework.h"

#include "Resource.h"
#include "TcpReceiverWnd.h"
#include <Richedit.h>

IMPLEMENT_DYNAMIC(CTcpReceiverWnd, CDockablePane)

CTcpReceiverWnd::CTcpReceiverWnd() noexcept
	: m_currentMatchIndex(-1)
	, m_highlightSearch(false)
{
}

CTcpReceiverWnd::~CTcpReceiverWnd()
{
	ClearData();
}

BEGIN_MESSAGE_MAP(CTcpReceiverWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
	ON_WM_COPYDATA()
	ON_MESSAGE(WM_TCP_RECEIVER_DATA, &CTcpReceiverWnd::OnTcpDataReceived)
	ON_MESSAGE(WM_TCP_RECEIVER_STATUS, &CTcpReceiverWnd::OnTcpStatusChanged)
	ON_COMMAND(ID_TCPRECEIVER_CLEAR, &CTcpReceiverWnd::OnClear)
	ON_UPDATE_COMMAND_UI(ID_TCPRECEIVER_CLEAR, &CTcpReceiverWnd::OnUpdateClear)
	ON_EN_CHANGE(IDC_TCPRECEIVER_SEARCH, &CTcpReceiverWnd::OnSearchTextChanged)
	ON_COMMAND(ID_EDIT_COPY, &CTcpReceiverWnd::OnEditCopy)
END_MESSAGE_MAP()

int CTcpReceiverWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
		return -1;

	if (!CreateControls())
		return -1;

	return 0;
}

BOOL CTcpReceiverWnd::CreateControls()
{
	const int toolbarHeight = 28;
	const int spacing = 4;
	const int btnWidth = 60;
	const int btnHeight = 24;
	const int labelWidth = 35;
	const int searchWidth = 150;

	// 创建富文本编辑框（支持无限长度文本显示）
	m_logEdit.Create(
		WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
		WS_VSCROLL | WS_HSCROLL | ES_NOHIDESEL,
		CRect(0, toolbarHeight, 400, 400), this, (UINT)-1);
	m_logEdit.SetFont(&afxGlobalData.fontRegular);

	// 设置富文本编辑器支持更多文本
	// 使用 EM_LIMITTEXT 设置最大文本长度（0 = 无限制）
	m_logEdit.LimitText(0);

	// 清空按钮
	m_clearBtn.Create(_T("清空"), BS_PUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		CRect(spacing, spacing, spacing + btnWidth, spacing + btnHeight), this, ID_TCPRECEIVER_CLEAR);
	m_clearBtn.SetFont(&afxGlobalData.fontRegular);

	// 搜索标签
	int labelLeft = spacing + btnWidth + spacing;
	m_searchLabel.Create(_T("搜索:"), WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
		CRect(labelLeft, spacing, labelLeft + labelWidth, spacing + btnHeight), this);
	m_searchLabel.SetFont(&afxGlobalData.fontRegular);

	// 搜索框
	int searchLeft = labelLeft + labelWidth + spacing;
	m_searchEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL | WS_TABSTOP,
		CRect(searchLeft, spacing, searchLeft + searchWidth, spacing + btnHeight), this, IDC_TCPRECEIVER_SEARCH);
	m_searchEdit.SetFont(&afxGlobalData.fontRegular);

	return TRUE;
}

void CTcpReceiverWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);

	const int toolbarHeight = 28;
	const int spacing = 4;
	const int btnWidth = 60;
	const int btnHeight = 24;
	const int labelWidth = 35;
	const int searchWidth = 150;

	if (m_clearBtn.m_hWnd)
		m_clearBtn.SetWindowPos(nullptr, spacing, spacing, btnWidth, btnHeight, SWP_NOZORDER);

	int labelLeft = spacing + btnWidth + spacing;
	if (m_searchLabel.m_hWnd)
		m_searchLabel.SetWindowPos(nullptr, labelLeft, spacing, labelWidth, btnHeight, SWP_NOZORDER);

	int searchLeft = labelLeft + labelWidth + spacing;
	if (m_searchEdit.m_hWnd)
		m_searchEdit.SetWindowPos(nullptr, searchLeft, spacing, searchWidth, btnHeight, SWP_NOZORDER);

	if (m_logEdit.m_hWnd)
	{
		m_logEdit.SetWindowPos(nullptr, 0, toolbarHeight, cx, cy - toolbarHeight, SWP_NOZORDER);
	}
}

void CTcpReceiverWnd::OnDestroy()
{
	ClearData();
	CDockablePane::OnDestroy();
}

BOOL CTcpReceiverWnd::OnEraseBkgnd(CDC* pDC)
{
	CRect rc;
	GetClientRect(rc);
	pDC->FillSolidRect(rc, ::GetSysColor(COLOR_WINDOW));
	return TRUE;
}

BOOL CTcpReceiverWnd::OnCopyData(CWnd* /*pWnd*/, COPYDATASTRUCT* pCopyDataStruct)
{
	if (pCopyDataStruct == nullptr || pCopyDataStruct->dwData != 0xACDC)
		return FALSE;

	const wchar_t* data = static_cast<const wchar_t*>(pCopyDataStruct->lpData);
	const int dataLen = pCopyDataStruct->cbData / sizeof(wchar_t);
	if (data == nullptr || dataLen <= 0)
		return FALSE;

	CString wstr(data, dataLen);

	{
		std::lock_guard<std::mutex> lock(m_bufferMutex);
		TcpDataItem item;
		item.isIncoming = true;
		item.sourceIp = _T("chat-bridge");
		item.dataLength = static_cast<int>(wstr.GetLength());

		SYSTEMTIME st;
		GetLocalTime(&st);
		item.timestamp.Format(_T("%02d:%02d:%02d.%03d"),
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		item.data = wstr;

		m_dataBuffer.push_back(item);
		while (m_dataBuffer.size() > MAX_LOG_ITEMS)
			m_dataBuffer.pop_front();
	}

	AddItemToRichEdit(wstr);

	return TRUE;
}

void CTcpReceiverWnd::AddItemToRichEdit(const CString& data)
{
	if (!m_logEdit.m_hWnd) return;

	// 构建显示行：[时间] 数据
	SYSTEMTIME st;
	GetLocalTime(&st);
	CString timeStr;
	timeStr.Format(_T("%02d:%02d:%02d.%03d"), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	CString displayLine;
	displayLine.Format(_T("[%s] %s\r\n"), timeStr.GetString(), data.GetString());

	// 检查是否匹配搜索文本
	bool isHighlight = false;
	if (!m_searchText.IsEmpty())
	{
		if (data.Find(m_searchText) >= 0)
			isHighlight = true;
	}

	AppendLogLine(displayLine, isHighlight, true);
}

void CTcpReceiverWnd::AppendLogLine(const CString& line, bool isHighlight, bool scrollToBottom)
{
	if (!m_logEdit.m_hWnd) return;

	// 获取当前文本长度
	long oldLen = m_logEdit.GetTextLength();

	// 设置字符格式
	CHARFORMAT cf;
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_COLOR | CFM_BOLD;
	if (isHighlight)
	{
		cf.crTextColor = RGB(255, 0, 0);  // 红色高亮
		cf.dwEffects = CFE_BOLD;
	}
	else
	{
		cf.crTextColor = RGB(0, 0, 0);  // 黑色
		cf.dwEffects = 0;
	}

	// 添加文本
	int newLen = oldLen + line.GetLength();
	m_logEdit.SetSel(oldLen, oldLen);
	m_logEdit.SetSelectionCharFormat(cf);
	m_logEdit.ReplaceSel(line);

	// 如果行数超过限制，删除顶部旧内容（保留最后 MAX_LOG_ITEMS 条）
	TrimRichEditToMaxLines();

	// 自动滚动到底部
	if (scrollToBottom)
		m_logEdit.LineScroll(m_logEdit.GetLineCount());

//#ifdef _DEBUG
//	OutputDebugString(CString(_T("[TcpRcv] Append line len=")) + std::to_wstring(line.GetLength()).c_str() + _T("\n"));
//#endif
}

void CTcpReceiverWnd::TrimRichEditToMaxLines()
{
	if (!m_logEdit.m_hWnd)
		return;

	int lineCount = m_logEdit.GetLineCount();
	if (lineCount <= MAX_LOG_ITEMS)
		return;

	int excessLines = lineCount - MAX_LOG_ITEMS;
	int firstVisibleLine = m_logEdit.GetFirstVisibleLine();
	int lastChar = m_logEdit.LineIndex(excessLines);
	if (lastChar <= 0)
		return;

	m_logEdit.SetRedraw(FALSE);
	::SendMessage(m_logEdit.m_hWnd, EM_SETSEL, 0, lastChar);
	::SendMessage(m_logEdit.m_hWnd, EM_REPLACESEL, FALSE, (LPARAM)_T(""));

	int newFirstVisible = firstVisibleLine - excessLines;
	if (newFirstVisible < 0)
		newFirstVisible = 0;
	m_logEdit.LineScroll(newFirstVisible - m_logEdit.GetFirstVisibleLine());

	m_logEdit.SetRedraw(TRUE);
	m_logEdit.Invalidate(FALSE);
}

LRESULT CTcpReceiverWnd::OnTcpDataReceived(WPARAM wParam, LPARAM lParam)
{
	auto* item = reinterpret_cast<TcpDataItem*>(lParam);
	if (!item) return 0;

	AddItemToRichEdit(item->data);

	delete item;
	return 0;
}

LRESULT CTcpReceiverWnd::OnTcpStatusChanged(WPARAM wParam, LPARAM lParam)
{
	const UINT statusType = static_cast<UINT>(wParam);
	const wchar_t* msg = reinterpret_cast<wchar_t*>(lParam);

	CString logLine;
	CString msgStr(msg ? msg : L"");
	switch (statusType)
	{
	case 0: logLine.Format(_T("[INFO] %s"), (LPCTSTR)msgStr); break;
	case 1: logLine.Format(_T("[WARN] %s"), (LPCTSTR)msgStr); break;
	case 2: logLine.Format(_T("[ERROR] %s"), (LPCTSTR)msgStr); break;
	case 3: logLine.Format(_T("[CONN] %s"), (LPCTSTR)msgStr); break;
	default: logLine = msgStr; break;
	}

	AddStatusLog(logLine);

	if (msg) delete[] msg;
	return 0;
}

void CTcpReceiverWnd::AddStatusLog(const CString& line)
{
	if (!m_logEdit.m_hWnd) return;

	// 添加状态日志行
	AppendLogLine(line + _T("\r\n"), false, true);
}

void CTcpReceiverWnd::OnClear()
{
	std::lock_guard<std::mutex> lock(m_bufferMutex);
	m_dataBuffer.clear();

	if (m_logEdit.m_hWnd)
	{
		m_logEdit.SetWindowText(_T(""));
	}

	m_matchedLines.clear();
	m_currentMatchIndex = -1;
}

void CTcpReceiverWnd::OnUpdateClear(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CTcpReceiverWnd::OnSearchTextChanged()
{
	CString searchText;
	m_searchEdit.GetWindowText(searchText);
	m_searchText = searchText;

	// 重新构建显示
	RebuildLogDisplay();
}

void CTcpReceiverWnd::RebuildLogDisplay()
{
	if (!m_logEdit.m_hWnd) return;

	std::lock_guard<std::mutex> lock(m_bufferMutex);

	m_matchedLines.clear();
	m_currentMatchIndex = -1;

	m_logEdit.SetRedraw(FALSE);
	m_logEdit.LockWindowUpdate();
	m_logEdit.SetWindowText(_T(""));

	for (const auto& item : m_dataBuffer)
	{
		bool isHighlight = false;

		if (!m_searchText.IsEmpty())
		{
			if (item.data.Find(m_searchText) >= 0)
			{
				isHighlight = true;
				m_matchedLines.push_back((int)m_dataBuffer.size());
			}
		}

		CString displayLine;
		displayLine.Format(_T("[%s] %s\r\n"), item.timestamp.GetString(), item.data.GetString());
		AppendLogLine(displayLine, isHighlight, false);
	}

	m_logEdit.UnlockWindowUpdate();
	m_logEdit.SetRedraw(TRUE);
	m_logEdit.Invalidate(FALSE);

	// 滚动到底部
	if (!m_matchedLines.empty())
		m_currentMatchIndex = 0;
}

void CTcpReceiverWnd::FindNext(const CString& searchText)
{
	if (!m_logEdit.m_hWnd || searchText.IsEmpty()) return;

	CString text;
	m_logEdit.GetWindowText(text);

	// 获取当前选择位置
	long start, end;
	m_logEdit.GetSel(start, end);

	// 从当前位置之后搜索
	int foundPos = text.Find(searchText, end);
	if (foundPos < 0)
	{
		// 循环到开头搜索
		foundPos = text.Find(searchText, 0);
	}

	if (foundPos >= 0)
	{
		m_logEdit.SetSel(foundPos, foundPos + searchText.GetLength());
		m_logEdit.SetFocus();

		// 确保选中文本可见
		int lineIndex = m_logEdit.LineFromChar(foundPos);
		m_logEdit.LineScroll(lineIndex - m_logEdit.GetFirstVisibleLine());
	}
	else
	{
		AfxMessageBox(_T("未找到"), MB_OK | MB_ICONINFORMATION);
	}
}

void CTcpReceiverWnd::FindPrev(const CString& searchText)
{
	if (!m_logEdit.m_hWnd || searchText.IsEmpty()) return;

	CString text;
	m_logEdit.GetWindowText(text);

	// 获取当前选择位置
	long start, end;
	m_logEdit.GetSel(start, end);

	// 从当前位置之前搜索
	int foundPos = -1;
	int searchFrom = start - 1;
	while (searchFrom >= 0)
	{
		int pos = text.Find(searchText, searchFrom);
		if (pos >= 0 && pos < start)
		{
			foundPos = pos;
			searchFrom = pos - 1;
		}
		else
		{
			break;
		}
	}

	// 如果没找到，从末尾开始搜索
	if (foundPos < 0)
	{
		searchFrom = text.GetLength() - searchText.GetLength() - 1;
		while (searchFrom >= 0)
		{
			int pos = text.ReverseFind(searchText[0]);
			if (pos >= 0)
			{
				CString substr = text.Mid(pos, searchText.GetLength());
				if (substr == searchText)
				{
					foundPos = pos;
					break;
				}
				searchFrom = pos - 1;
			}
			else
			{
				break;
			}
		}
	}

	if (foundPos >= 0)
	{
		m_logEdit.SetSel(foundPos, foundPos + searchText.GetLength());
		m_logEdit.SetFocus();

		// 确保选中文本可见
		int lineIndex = m_logEdit.LineFromChar(foundPos);
		m_logEdit.LineScroll(lineIndex - m_logEdit.GetFirstVisibleLine());
	}
	else
	{
		AfxMessageBox(_T("未找到"), MB_OK | MB_ICONINFORMATION);
	}
}

void CTcpReceiverWnd::OnEditCopy()
{
	if (m_logEdit.m_hWnd)
	{
		m_logEdit.Copy();
	}
}

void CTcpReceiverWnd::ClearData()
{
	std::lock_guard<std::mutex> lock(m_bufferMutex);
	m_dataBuffer.clear();
	m_matchedLines.clear();
}
