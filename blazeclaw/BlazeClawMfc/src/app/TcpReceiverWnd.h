#pragma once

#include "Resource.h"

#include <afxwin.h>
#include <afxext.h>
#include <afxcontrolbars.h>
#include <afxdtctl.h>
#include <afxdisp.h>
#include <afxmt.h>
#include <afxrich.h>
#include <atlbase.h>
#include <atlstr.h>

#include <string>
#include <vector>
#include <queue>
#include <atomic>
#include <memory>
#include <functional>
#include <chrono>
#include <list>
#include <mutex>

constexpr UINT WM_TCP_RECEIVER_DATA = WM_USER + 0x200;
constexpr UINT WM_TCP_RECEIVER_STATUS = WM_USER + 0x201;

struct TcpDataItem
{
	CString timestamp;
	CString sourceIp;
	CString data;
	bool isIncoming;
	int dataLength;
};

class CTcpReceiverWnd : public CDockablePane
{
	DECLARE_DYNAMIC(CTcpReceiverWnd)

public:
	CTcpReceiverWnd() noexcept;
	virtual ~CTcpReceiverWnd();

	void ClearData();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);
	afx_msg LRESULT OnTcpDataReceived(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnTcpStatusChanged(WPARAM wParam, LPARAM lParam);
	afx_msg void OnClear();
	afx_msg void OnUpdateClear(CCmdUI* pCmdUI);
	afx_msg void OnSearchTextChanged();
	afx_msg void OnEditCopy();

	DECLARE_MESSAGE_MAP()

private:
	enum : int { MAX_LOG_ITEMS = 500 };

	CRichEditCtrl m_logEdit;
	CButton m_clearBtn;
	CEdit m_searchEdit;
	CStatic m_searchLabel;

	std::list<TcpDataItem> m_dataBuffer;
	std::mutex m_bufferMutex;
	CString m_searchText;
	std::vector<int> m_matchedLines;
	int m_currentMatchIndex;
	bool m_highlightSearch;

	BOOL CreateControls();
	void AppendLogLine(const CString& line, bool isHighlight, bool scrollToBottom = true);
	void AddItemToRichEdit(const CString& data);
	void AddStatusLog(const CString& line);
	void RebuildLogDisplay();
	void FindNext(const CString& searchText);
	void FindPrev(const CString& searchText);
	void TrimRichEditToMaxLines();
};
