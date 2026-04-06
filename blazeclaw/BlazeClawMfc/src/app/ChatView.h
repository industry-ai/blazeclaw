#pragma once

#include "pch.h"
#include "ChatInputEdit.h"
#include "../gateway/GatewayProtocolModels.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CChatView : public CView
{
protected:
	CChatView() noexcept;
	DECLARE_DYNCREATE(CChatView)

	// Controls
protected:
	CListBox m_wndMsgList;
	CChatInputEdit m_wndInput;
	CButton  m_wndSend;
	CButton  m_wndAbort;
	CButton  m_wndAttach;

	struct CHAT_ITEM
	{
		CString text;
		BOOL bSelf = FALSE; // TRUE=self (right), FALSE=peer (left)
	};
	CArray<CHAT_ITEM, CHAT_ITEM&> m_items;

	struct NativeChatEventPayload
	{
		std::string runId;
		std::string sessionKey;
		std::string state;
		std::optional<std::string> messageJson;
		std::optional<std::string> errorMessage;
	};

	struct NativeChatSendCompletionPayload
	{
		std::uint64_t generation = 0;
		std::string runId;
		bool accepted = false;
		std::string errorDetail;
	};

	struct NativeChatState
	{
		bool connected = false;
		std::string sessionKey = "main";
		bool chatLoading = false;
		std::vector<std::string> chatMessages;
		std::optional<std::string> chatThinkingLevel;
		bool chatSending = false;
		std::string chatMessage;
		struct Attachment
		{
			std::string filePath;
			std::string mimeType;
			std::string contentBase64;
		};
		std::vector<Attachment> chatAttachments;
		std::optional<std::string> chatRunId;
		std::optional<std::string> chatStream;
		std::optional<std::uint64_t> chatStreamStartedAt;
		std::optional<std::string> lastError;
	};

	NativeChatState m_chatState;
	UINT_PTR m_chatPollTimerId = 0;
	std::size_t m_renderedMessageCount = 0;
	int m_streamItemIndex = -1;
	int m_errorItemIndex = -1;
	std::string m_renderedStreamText;
	std::string m_renderedErrorText;
	std::uint64_t m_chatSendGeneration = 1;

	// Overrides
public:
	virtual void OnDraw(CDC* /*pDC*/);
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnSendClicked();
	afx_msg void OnAbortClicked();
	afx_msg void OnAttachClicked();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnNativeChatSendCompleted(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg void OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	DECLARE_MESSAGE_MAP()

	void LayoutControls(int cx, int cy);
	int AppendMessage(const CString& strText, BOOL bSelf);
	bool IsGatewayConnected() const;
	bool RequestGateway(
		const std::string& method,
		const std::optional<std::string>& paramsJson,
		blazeclaw::gateway::protocol::ResponseFrame& response) const;
	void LoadChatHistoryNative();
	void SendChatMessageNative(const std::string& message);
	void AbortChatRunNative();
	void PumpChatEventsNative();
	void HandleChatEventNative(const NativeChatEventPayload& payload);
	void SyncItemsFromState();
	void RebuildItemsFromState();
	void ResetRenderedItemsTracking();
	void RemoveItemAt(int index);
	void UpdateItemAt(int index, const CString& strText, BOOL bSelf);
	void UpdateControlStates();
	void AddStatusMessage(const CString& message);
};

