#include "pch.h"
#include "framework.h"
#include "ChatView.h"
#include "BlazeClawMFCApp.h"
#include "MainFrame.h"

#include "../gateway/GatewayJsonUtils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <thread>
#include <wincrypt.h>
#include <future>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CChatView, CView)

namespace {
	constexpr UINT_PTR kNativeChatPollTimerId = 0x5A11;
	constexpr UINT kNativeChatPollIntervalMs = 500;
	constexpr UINT kNativeChatSendCompletedMessage = WM_APP + 0x521;
	constexpr UINT kNativeVoiceTranscribeCompletedMessage = WM_APP + 0x522;
	constexpr UINT kVoiceTranscribePendingDelayMs = 200;
	constexpr UINT kVoiceTranscribePollIntervalMs = 250;
	constexpr UINT kVoiceTranscribePollTimeoutMs = 30000;
	constexpr UINT kVoiceTranscribePollStatusUpdateMs = 1000;
	constexpr UINT kVoiceTranscribePollStartDelayMs = 300;
	constexpr UINT kVoiceTranscribePollRetryDelayMs = 500;
	constexpr UINT kVoiceTranscribeStartRetryMs = 250;
	constexpr UINT kMsgListControlId = 1000;
	constexpr UINT kSendButtonControlId = 1001;
	constexpr UINT kInputControlId = 1002;
	constexpr UINT kAbortButtonControlId = 1003;
	constexpr UINT kAttachButtonControlId = 1004;
	constexpr UINT kVoiceButtonControlId = 1005;
	constexpr char kSilentReplyToken[] = "NO_REPLY";
	constexpr std::size_t kMaxAttachmentBytes = 5 * 1024 * 1024;

	std::uint64_t CurrentEpochMs()
	{
		const auto now = std::chrono::system_clock::now();
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now.time_since_epoch())
			.count());
	}

	std::string EscapeJson(const std::string& value)
	{
		std::string escaped;
		escaped.reserve(value.size() + 8);
		for (const char ch : value)
		{
			switch (ch)
			{
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				escaped.push_back(ch);
				break;
			}
		}

		return escaped;
	}

	std::optional<std::string> GuessMimeType(const std::string& filePath)
	{
		auto lastDot = filePath.find_last_of('.');
		if (lastDot == std::string::npos)
		{
			return std::nullopt;
		}

		std::string ext = filePath.substr(lastDot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});

		if (ext == "png") return "image/png";
		if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
		if (ext == "gif") return "image/gif";
		if (ext == "webp") return "image/webp";
		if (ext == "bmp") return "image/bmp";
		return std::nullopt;
	}

	std::optional<std::vector<std::uint8_t>> ReadFileBytes(const std::string& filePath)
	{
		std::ifstream input(filePath, std::ios::binary | std::ios::ate);
		if (!input)
		{
			return std::nullopt;
		}

		const auto endPos = input.tellg();
		if (endPos <= 0)
		{
			return std::vector<std::uint8_t>{};
		}

		if (static_cast<std::size_t>(endPos) > kMaxAttachmentBytes)
		{
			return std::nullopt;
		}

		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(endPos));
		input.seekg(0, std::ios::beg);
		if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
		{
			return std::nullopt;
		}

		return bytes;
	}

	std::optional<std::string> Base64EncodeBytes(const std::vector<std::uint8_t>& bytes)
	{
		if (bytes.empty())
		{
			return std::string();
		}

		DWORD outChars = 0;
		if (!CryptBinaryToStringA(
			bytes.data(),
			static_cast<DWORD>(bytes.size()),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			nullptr,
			&outChars))
		{
			return std::nullopt;
		}

		std::string encoded(outChars, '\0');
		if (!CryptBinaryToStringA(
			bytes.data(),
			static_cast<DWORD>(bytes.size()),
			CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
			encoded.data(),
			&outChars))
		{
			return std::nullopt;
		}

		if (!encoded.empty() && encoded.back() == '\0')
		{
			encoded.pop_back();
		}

		return encoded;
	}

	bool IsSilentReplyText(const std::string& text)
	{
		return blazeclaw::gateway::json::Trim(text) == kSilentReplyToken;
	}

	std::vector<std::string> SplitTopLevelObjects(const std::string& arrayJson)
	{
		std::vector<std::string> objects;
		const std::string trimmed = blazeclaw::gateway::json::Trim(arrayJson);
		if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
		{
			return objects;
		}

		bool inString = false;
		int depth = 0;
		std::size_t start = std::string::npos;
		for (std::size_t i = 0; i < trimmed.size(); ++i)
		{
			const char ch = trimmed[i];
			if (inString)
			{
				if (ch == '\\')
				{
					++i;
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
				if (depth == 0)
				{
					start = i;
				}
				++depth;
				continue;
			}

			if (ch == '}')
			{
				--depth;
				if (depth == 0 && start != std::string::npos)
				{
					objects.push_back(trimmed.substr(start, (i - start) + 1));
					start = std::string::npos;
				}
			}
		}

		return objects;
	}

	std::string ExtractMessageText(const std::string& messageJson)
	{
		std::string text;
		if (blazeclaw::gateway::json::FindStringField(messageJson, "text", text))
		{
			return text;
		}

		std::string contentRaw;
		if (!blazeclaw::gateway::json::FindRawField(messageJson, "content", contentRaw))
		{
			return {};
		}

		blazeclaw::gateway::json::FindStringField(contentRaw, "text", text);
		return text;
	}

	std::string BuildRunTaskDeltasParams(const std::string& runId)
	{
		return std::string("{\"runId\":\"") + runId + "\"}";
	}

	std::string ExtractToolPathLabel(
		const std::string& toolName,
		const std::string& status,
		const std::string& errorCode)
	{
		std::string normalizedTool = blazeclaw::gateway::json::Trim(toolName);
		if (normalizedTool.empty())
		{
			return {};
		}

		std::string normalizedStatus = blazeclaw::gateway::json::Trim(status);
		if (normalizedStatus.empty())
		{
			normalizedStatus = "unknown";
		}

		std::string line = normalizedTool + " [" + normalizedStatus + "]";
		const std::string normalizedError = blazeclaw::gateway::json::Trim(errorCode);
		if (!normalizedError.empty())
		{
			line += " errorCode=" + normalizedError;
		}

		return line;
	}

	CString BuildDeepSeekDiagnosticLine(
		const char* stage,
		const std::string& detail)
	{
		const std::string safeStage =
			(stage == nullptr || std::string(stage).empty())
			? "unknown"
			: std::string(stage);
		const std::string line =
			std::string("[DeepSeek][") +
			safeStage +
			"] " +
			detail;
		return CString(CA2W(line.c_str(), CP_UTF8));
	}

	std::string TruncateDiagnosticText(
		const std::string& value,
		const std::size_t maxChars = 320)
	{
		if (value.size() <= maxChars)
		{
			return value;
		}

		return value.substr(0, maxChars) + "...";
	}

}

BEGIN_MESSAGE_MAP(CChatView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_MEASUREITEM()
	ON_WM_DRAWITEM()
	ON_MESSAGE(kNativeChatSendCompletedMessage, &CChatView::OnNativeChatSendCompleted)
	ON_MESSAGE(kNativeVoiceTranscribeCompletedMessage, &CChatView::OnNativeVoiceTranscribeCompleted)
	ON_BN_CLICKED(kSendButtonControlId, &CChatView::OnSendClicked)
	ON_BN_CLICKED(kAbortButtonControlId, &CChatView::OnAbortClicked)
	ON_BN_CLICKED(kAttachButtonControlId, &CChatView::OnAttachClicked)
	ON_BN_CLICKED(kVoiceButtonControlId, &CChatView::OnVoiceClicked)
END_MESSAGE_MAP()

CChatView::CChatView() noexcept
{}

BOOL CChatView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

void CChatView::OnDraw(CDC* /*pDC*/)
{
	// Drawing handled by child controls
}

int CChatView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
		return -1;

	CRect rcDummy(0, 0, 0, 0);

	// Message list
	if (!m_wndMsgList.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
		LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
		rcDummy, this, kMsgListControlId))
	{
		TRACE0("Failed to create message list\n");
		return -1;
	}

	// Input edit
	if (!m_wndInput.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
		ES_MULTILINE | ES_AUTOVSCROLL,
		rcDummy, this, kInputControlId))
	{
		TRACE0("Failed to create input edit\n");
		return -1;
	}
	m_wndInput.m_pOwner = this;

	// Send button
	if (!m_wndSend.Create(_T("发送"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		rcDummy, this, kSendButtonControlId))
	{
		TRACE0("Failed to create send button\n");
		return -1;
	}

	if (!m_wndAbort.Create(_T("停止"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		rcDummy, this, kAbortButtonControlId))
	{
		TRACE0("Failed to create abort button\n");
		return -1;
	}

	if (!m_wndAttach.Create(_T("附件"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		rcDummy, this, kAttachButtonControlId))
	{
		TRACE0("Failed to create attach button\n");
		return -1;
	}

	// Voice button
	if (!m_wndVoice.Create(_T("语音"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		rcDummy, this, kVoiceButtonControlId))
	{
		TRACE0("Failed to create voice button\n");
		return -1;
	}

	// Initialize voice recorder
	m_voiceRecorder.SetCallback(this);
	m_voiceRecorder.Initialize(m_hWnd);

	m_chatState.connected = IsGatewayConnected();
	LoadChatHistoryNative();
	UpdateControlStates();
	m_chatPollTimerId = SetTimer(
		kNativeChatPollTimerId,
		kNativeChatPollIntervalMs,
		nullptr);

	return 0;
}

void CChatView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);
	LayoutControls(cx, cy);
}

void CChatView::LayoutControls(int cx, int cy)
{
	if (!::IsWindow(m_wndMsgList.GetSafeHwnd()))
		return;

	const int nMargin = 8;
	const int nInputHeight = 64;
	const int nButtonWidth = 74;
	const int nVoiceButtonWidth = 74;
	const int nAttachWidth = 74;

	CRect rcClient(0, 0, cx, cy);

	// Message list (top)
	CRect rcList = rcClient;
	rcList.DeflateRect(nMargin, nMargin, nMargin, 0);
	rcList.bottom -= (nMargin + nInputHeight + nMargin);
	if (rcList.bottom < rcList.top)
		rcList.bottom = rcList.top;

	m_wndMsgList.MoveWindow(rcList);

	// Bottom bar
	CRect rcBottom = rcClient;
	rcBottom.DeflateRect(nMargin, 0, nMargin, nMargin);
	rcBottom.top = rcBottom.bottom - nInputHeight;
	if (rcBottom.top < rcList.bottom + nMargin)
		rcBottom.top = rcList.bottom + nMargin;

	CRect rcButton = rcBottom;
	rcButton.left = rcButton.right - nButtonWidth;
	CRect rcAbort = rcButton;
	rcAbort.left -= (nButtonWidth + nMargin);
	rcAbort.right -= (nButtonWidth + nMargin);
	CRect rcVoice = rcAbort;
	rcVoice.left -= (nVoiceButtonWidth + nMargin);
	rcVoice.right -= (nVoiceButtonWidth + nMargin);

	CRect rcAttach = rcBottom;
	rcAttach.right = rcAttach.left + nAttachWidth;

	CRect rcEdit = rcBottom;
	rcEdit.left = rcAttach.right + nMargin;
	rcEdit.right = rcVoice.left - nMargin;

	m_wndInput.MoveWindow(rcEdit);
	m_wndSend.MoveWindow(rcButton);
	m_wndAbort.MoveWindow(rcAbort);
	m_wndVoice.MoveWindow(rcVoice);
	m_wndAttach.MoveWindow(rcAttach);
}

void CChatView::OnSendClicked()
{
	CString strText;
	m_wndInput.GetWindowText(strText);
	strText.Trim();
	if (!strText.IsEmpty() || !m_chatState.chatAttachments.empty())
	{
		m_wndInput.SetWindowText(_T(""));
		CW2A utf8(strText.GetString(), CP_UTF8);
		SendChatMessageNative(std::string(utf8));
		SyncItemsFromState();
		UpdateControlStates();
	}
}

void CChatView::OnAbortClicked()
{
	AbortChatRunNative();
	UpdateControlStates();
}

void CChatView::OnAttachClicked()
{
	CFileDialog dialog(
		TRUE,
		nullptr,
		nullptr,
		OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
		_T("Images (*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp)|*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp||"),
		this);
	if (dialog.DoModal() != IDOK)
	{
		return;
	}

	const CString path = dialog.GetPathName();
	CW2A pathUtf8(path.GetString(), CP_UTF8);
	const std::string filePath(pathUtf8);
	const auto mimeType = GuessMimeType(filePath);
	if (!mimeType.has_value())
	{
		AfxMessageBox(L"Unsupported attachment type. Please select an image file.");
		return;
	}

	const auto bytes = ReadFileBytes(filePath);
	if (!bytes.has_value())
	{
		AfxMessageBox(L"Failed to read attachment or file exceeds size limit (5MB).");
		return;
	}

	const auto base64 = Base64EncodeBytes(bytes.value());
	if (!base64.has_value())
	{
		AfxMessageBox(L"Failed to encode attachment.");
		return;
	}

	m_chatState.chatAttachments.push_back(NativeChatState::Attachment{
		.filePath = filePath,
		.mimeType = mimeType.value(),
		.contentBase64 = base64.value(),
		});

	CString status;
	status.Format(L"[Attachment] %s", path.GetString());
	AddStatusMessage(status);
	UpdateControlStates();
}

void CChatView::OnVoiceClicked()
{
	if (m_voiceRecorder.GetState() == VoiceRecorderState::Recording)
	{
		m_voiceRecorder.StopRecording();
		UpdateControlStates();
		if (!m_strLastVoiceFilePath.IsEmpty())
		{
			const auto generation = ++m_voiceTranscribeGeneration;
			StartVoiceTranscriptionNative(
				std::string(CW2A(m_strLastVoiceFilePath.GetString(), CP_UTF8)),
				m_chatState.sessionKey,
				"voice-" + std::to_string(CurrentEpochMs()),
				generation);
		}
		return;
	}

	WCHAR exePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	LPWSTR p = wcsrchr(exePath, L'\\');
	if (p != nullptr)
	{
		*p = L'\0';
	}

	CString recordingsDir;
	recordingsDir.Format(L"%s\\BlazeClawRecordings", exePath);
	CreateDirectoryW(recordingsDir, nullptr);

	SYSTEMTIME st;
	GetLocalTime(&st);
	CStringW fileName;
	fileName.Format(L"recording_%04d%02d%02d_%02d%02d%02d.wav",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond);

	CStringW filePathW = recordingsDir + L"\\" + fileName;
	m_strLastVoiceFilePath = filePathW;
	m_voiceSessionState.reset();

	if (!m_voiceRecorder.StartRecording(filePathW))
	{
		AfxMessageBox(L"Failed to start recording. Please check your microphone.");
	}
	else
	{
		AddStatusMessage(L"[Voice] Recording started");
	}

	UpdateControlStates();
}

void CChatView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_chatPollTimerId && nIDEvent != 0)
	{
		PumpChatEventsNative();
		UpdateControlStates();
	}

	CView::OnTimer(nIDEvent);
}

void CChatView::OnDestroy()
{
	++m_chatSendGeneration;

	if (m_chatPollTimerId != 0)
	{
		KillTimer(m_chatPollTimerId);
		m_chatPollTimerId = 0;
	}

	m_voiceRecorder.Shutdown();

	CView::OnDestroy();
}

void CChatView::OnVoiceDataAvailable(const BYTE* /*pData*/, DWORD /*dwLength*/)
{
	// Data is written directly to file during recording
}

void CChatView::OnVoiceStateChanged(VoiceRecorderState state)
{
	if (state == VoiceRecorderState::Idle && !m_strLastVoiceFilePath.IsEmpty())
	{
		CString msg;
		msg.Format(L"[Voice] Saved: %s", m_strLastVoiceFilePath.GetString());
		AddStatusMessage(msg);
	}
	UpdateControlStates();
}

void CChatView::OnVoiceSessionChanged(
	const blazeclaw::core::speechrecognition::SpeechSessionState& sessionState)
{
	m_voiceSessionState = sessionState;
	UpdateVoiceSessionState(sessionState);
}

void CChatView::OnVoiceError(long nError, const wchar_t* pszDescription)
{
	CString msg;
	msg.Format(L"[Voice] Recording error (%d): %s", nError, pszDescription);
	AddStatusMessage(msg);
	m_wndVoice.SetWindowText(_T("语音"));
}

void CChatView::UpdateVoiceSessionState(
	const blazeclaw::core::speechrecognition::SpeechSessionState& sessionState)
{
	CString message;
	const std::string stageText = [stage = sessionState.stage]() {
		switch (stage)
		{
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Idle: return std::string("idle");
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Recording: return std::string("recording");
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Paused: return std::string("paused");
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Stopped: return std::string("stopped");
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Transcribing: return std::string("transcribing");
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Completed: return std::string("completed");
		case blazeclaw::core::speechrecognition::SpeechSessionStage::Failed: return std::string("failed");
		default: return std::string("unknown");
		}
	}();

	std::string detail;
	if (sessionState.segment.has_value())
	{
		const auto& segment = sessionState.segment.value();
		detail = segment.final ? "final segment" : "interim segment";
		if (!segment.text.empty())
		{
			detail += " text=\"";
			detail += segment.text;
			detail += "\"";
		}
		detail += " sequence=" + std::to_string(segment.sequence);
	}
	else if (!sessionState.transcriptText.empty())
	{
		detail = "transcript=\"" + sessionState.transcriptText + "\"";
	}

	if (!detail.empty())
	{
		message.Format(L"[Voice] Session %S (%S)", stageText.c_str(), detail.c_str());
	}
	else
	{
		message.Format(L"[Voice] Session %S", stageText.c_str());
	}
	AddStatusMessage(message);
}

void CChatView::StartVoiceTranscriptionNative(
	const std::string& audioPath,
	const std::string& sessionId,
	const std::string& runId,
	std::uint64_t generation)
{
	if (audioPath.empty() || !IsGatewayConnected())
	{
		return;
	}

	m_voiceSessionState = blazeclaw::core::speechrecognition::SpeechSessionState{
		.sessionId = sessionId,
		.runId = runId,
		.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Transcribing,
		.audioPath = audioPath,
	};
	UpdateVoiceSessionState(*m_voiceSessionState);

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		return;
	}

	std::thread(
		[this, app, audioPath, sessionId, runId, generation, hwnd = GetSafeHwnd()]()
		{
			std::string params =
				std::string("{\"audioPath\":\"") +
				EscapeJson(audioPath) +
				"\",\"sessionId\":\"" +
				EscapeJson(sessionId) +
				"\",\"runId\":\"" +
				EscapeJson(runId) +
				"\"}";

			const blazeclaw::gateway::protocol::RequestFrame request{
				.id = runId,
				.method = "speech.transcribe",
				.paramsJson = params,
			};
			auto response = app->RouteGatewayRequest(request);

			auto completion = std::make_unique<CChatView::NativeVoiceTranscribeCompletionPayload>();
			completion->generation = generation;
			completion->sessionState.sessionId = sessionId;
			completion->sessionState.runId = runId;
			completion->sessionState.audioPath = audioPath;
			completion->sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Failed;

			if (!response.ok || !response.payloadJson.has_value())
			{
				completion->ok = false;
				completion->errorCode = response.error.has_value() ? response.error->code : "speech_transcribe_failed";
				completion->errorMessage = response.error.has_value() ? response.error->message : "speech.transcribe request failed";
				completion->sessionState.error = blazeclaw::core::speechrecognition::SpeechRecognitionError{
					.code = blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode::RuntimeUnavailable,
					.message = completion->errorMessage,
				};
			}
			else
			{
				PopulateVoiceTranscribeCompletionFromPayload(
					response.payloadJson.value(),
					*completion);
			}

			if (!::PostMessage(
				hwnd,
				kNativeVoiceTranscribeCompletedMessage,
				static_cast<WPARAM>(generation),
				reinterpret_cast<LPARAM>(completion.get())))
			{
				return;
			}

			completion.release();
		})
		.detach();
}

bool CChatView::PopulateVoiceTranscribeCompletionFromPayload(
	const std::string& payload,
	NativeVoiceTranscribeCompletionPayload& completion) const
{
	auto stageFromString = [](const std::string& stage)
	{
		if (stage == "recording") return blazeclaw::core::speechrecognition::SpeechSessionStage::Recording;
		if (stage == "paused") return blazeclaw::core::speechrecognition::SpeechSessionStage::Paused;
		if (stage == "stopped") return blazeclaw::core::speechrecognition::SpeechSessionStage::Stopped;
		if (stage == "transcribing") return blazeclaw::core::speechrecognition::SpeechSessionStage::Transcribing;
		if (stage == "completed") return blazeclaw::core::speechrecognition::SpeechSessionStage::Completed;
		if (stage == "failed") return blazeclaw::core::speechrecognition::SpeechSessionStage::Failed;
		return blazeclaw::core::speechrecognition::SpeechSessionStage::Idle;
	};

	std::string stageValue;
	std::string speechSessionRaw;
	blazeclaw::gateway::json::FindBoolField(payload, "ok", completion.ok);
	blazeclaw::gateway::json::FindBoolField(payload, "cancelled", completion.cancelled);
	blazeclaw::gateway::json::FindStringField(payload, "text", completion.text);
	blazeclaw::gateway::json::FindStringField(payload, "language", completion.language);
	blazeclaw::gateway::json::FindStringField(payload, "errorCode", completion.errorCode);
	blazeclaw::gateway::json::FindStringField(payload, "errorMessage", completion.errorMessage);
	std::uint64_t latencyRaw = 0;
	if (blazeclaw::gateway::json::FindUInt64Field(payload, "latencyMs", latencyRaw))
	{
		completion.latencyMs = static_cast<std::uint32_t>(latencyRaw);
	}

	if (blazeclaw::gateway::json::FindRawField(payload, "speechSession", speechSessionRaw))
	{
		blazeclaw::gateway::json::FindStringField(speechSessionRaw, "sessionId", completion.sessionState.sessionId);
		blazeclaw::gateway::json::FindStringField(speechSessionRaw, "runId", completion.sessionState.runId);
		blazeclaw::gateway::json::FindStringField(speechSessionRaw, "audioPath", completion.sessionState.audioPath);
		blazeclaw::gateway::json::FindStringField(speechSessionRaw, "text", completion.sessionState.transcriptText);
		blazeclaw::gateway::json::FindStringField(speechSessionRaw, "language", completion.sessionState.language);
		blazeclaw::gateway::json::FindStringField(speechSessionRaw, "stage", stageValue);
		std::uint64_t sessionLatencyRaw = 0;
		if (blazeclaw::gateway::json::FindUInt64Field(speechSessionRaw, "latencyMs", sessionLatencyRaw))
		{
			completion.sessionState.latencyMs = static_cast<std::uint32_t>(sessionLatencyRaw);
		}
		blazeclaw::gateway::json::FindBoolField(speechSessionRaw, "cancelled", completion.sessionState.cancelled);

		std::string segmentRaw;
		if (blazeclaw::gateway::json::FindRawField(speechSessionRaw, "segment", segmentRaw))
		{
			blazeclaw::core::speechrecognition::SpeechTranscriptSegment segment;
			blazeclaw::gateway::json::FindStringField(segmentRaw, "text", segment.text);
			blazeclaw::gateway::json::FindBoolField(segmentRaw, "final", segment.final);
			std::uint64_t segmentSequence = 0;
			if (blazeclaw::gateway::json::FindUInt64Field(segmentRaw, "sequence", segmentSequence))
			{
				segment.sequence = static_cast<std::uint32_t>(segmentSequence);
			}
			completion.sessionState.segment = segment;
		}
	}

	completion.sessionState.stage = stageFromString(stageValue);
	if (completion.sessionState.transcriptText.empty())
	{
		completion.sessionState.transcriptText = completion.text;
	}
	if (completion.sessionState.language.empty())
	{
		completion.sessionState.language = completion.language;
	}
	if (completion.sessionState.latencyMs == 0)
	{
		completion.sessionState.latencyMs = completion.latencyMs;
	}
	if (!completion.ok && !completion.errorMessage.empty())
	{
		completion.sessionState.error = blazeclaw::core::speechrecognition::SpeechRecognitionError{
			.code = completion.cancelled
				? blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode::Cancelled
				: blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode::InferenceFailed,
			.message = completion.errorMessage,
		};
	}

	return true;
}

LRESULT CChatView::OnNativeVoiceTranscribeCompleted(WPARAM wParam, LPARAM lParam)
{
	const std::uint64_t expectedGeneration = static_cast<std::uint64_t>(wParam);
	std::unique_ptr<NativeVoiceTranscribeCompletionPayload> result(
		reinterpret_cast<NativeVoiceTranscribeCompletionPayload*>(lParam));
	if (!result || expectedGeneration != m_voiceTranscribeGeneration)
	{
		return 0;
	}

	m_voiceSessionState = result->sessionState;
	UpdateVoiceSessionState(result->sessionState);
	if (result->ok && !result->text.empty())
	{
		m_wndInput.SetWindowText(CA2W(result->text.c_str(), CP_UTF8));
		SendChatMessageNative(result->text);
		m_voiceSessionState->stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Completed;
		m_voiceSessionState->transcriptText = result->text;
		m_voiceSessionState->language = result->language;
		m_voiceSessionState->latencyMs = result->latencyMs;
		UpdateVoiceSessionState(*m_voiceSessionState);
	}
	else
	{
		const std::string errorText = result->errorMessage.empty()
			? "speech transcription failed"
			: result->errorMessage;
		AddStatusMessage(CString(CA2W(errorText.c_str(), CP_UTF8)));
	}

	m_wndVoice.SetWindowText(_T("语音"));
	UpdateControlStates();
	return 0;
}

bool CChatView::IsGatewayConnected() const
{
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	return app != nullptr && app->Services().IsRunning();
}

bool CChatView::RequestGateway(
	const std::string& method,
	const std::optional<std::string>& paramsJson,
	blazeclaw::gateway::protocol::ResponseFrame& response) const
{
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		return false;
	}

	const blazeclaw::gateway::protocol::RequestFrame request{
		.id = "native-chat",
		.method = method,
		.paramsJson = paramsJson,
	};

	response = app->RouteGatewayRequest(request);
	return true;
}

void CChatView::LoadChatHistoryNative()
{
	m_chatState.chatLoading = true;
	m_chatState.lastError.reset();
	if (!IsGatewayConnected())
	{
		m_chatState.chatLoading = false;
		return;
	}

	blazeclaw::gateway::protocol::ResponseFrame response;
	const std::string params =
		std::string("{\"sessionKey\":\"") +
		m_chatState.sessionKey +
		"\",\"limit\":200}";
	if (!RequestGateway("chat.history", params, response) || !response.ok ||
		!response.payloadJson.has_value())
	{
		m_chatState.lastError = "chat.history failed";
		m_chatState.chatLoading = false;
		SyncItemsFromState();
		UpdateControlStates();
		return;
	}

	std::string messagesRaw;
	if (blazeclaw::gateway::json::FindRawField(
		response.payloadJson.value(),
		"messages",
		messagesRaw))
	{
		m_chatState.chatMessages = SplitTopLevelObjects(messagesRaw);
	}

	std::string thinkingLevel;
	if (blazeclaw::gateway::json::FindStringField(
		response.payloadJson.value(),
		"thinkingLevel",
		thinkingLevel))
	{
		m_chatState.chatThinkingLevel = thinkingLevel;
	}

	m_chatState.chatStream.reset();
	m_chatState.chatStreamStartedAt.reset();
	m_chatState.chatLoading = false;
	RebuildItemsFromState();
	UpdateControlStates();
}

void CChatView::SendChatMessageNative(const std::string& message)
{
	const std::string trimmed = blazeclaw::gateway::json::Trim(message);
	const bool hasAttachments = !m_chatState.chatAttachments.empty();
	if ((trimmed.empty() && !hasAttachments) || !IsGatewayConnected())
	{
		if (!IsGatewayConnected())
		{
			if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
			{
				frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
					"send",
					"chat.send skipped because gateway is disconnected."));
			}
		}
		return;
	}

	const std::string runId =
		"native-run-" + std::to_string(CurrentEpochMs());
	m_reportedSkillPathRunIds.erase(runId);
	if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
	{
		frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
			"send",
			std::string("submit runId=") +
			runId +
			" session=" +
			m_chatState.sessionKey +
			" promptChars=" +
			std::to_string(trimmed.size()) +
			" attachments=" +
			std::to_string(m_chatState.chatAttachments.size())));
	}
	m_chatState.chatSending = true;
	m_chatState.chatRunId = runId;
	m_chatState.chatStream = std::string();
	m_chatState.chatStreamStartedAt = CurrentEpochMs();
	m_chatState.lastError.reset();

	std::string userContent = "[";
	bool firstBlock = true;
	if (!trimmed.empty())
	{
		userContent +=
			"{\"type\":\"text\",\"text\":\"" +
			EscapeJson(trimmed) +
			"\"}";
		firstBlock = false;
	}

	for (const auto& att : m_chatState.chatAttachments)
	{
		if (!firstBlock)
		{
			userContent += ",";
		}

		userContent +=
			"{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"" +
			EscapeJson(att.mimeType) +
			"\",\"data\":\"[selected]\"}}";
		firstBlock = false;
	}
	userContent += "]";

	m_chatState.chatMessages.push_back(
		"{\"role\":\"user\",\"content\":" +
		userContent +
		",\"timestamp\":" +
		std::to_string(CurrentEpochMs()) +
		"}");

	std::string attachmentsJson = "[";
	for (std::size_t i = 0; i < m_chatState.chatAttachments.size(); ++i)
	{
		if (i > 0)
		{
			attachmentsJson += ",";
		}

		const auto& att = m_chatState.chatAttachments[i];
		attachmentsJson +=
			"{\"type\":\"image\",\"mimeType\":\"" +
			EscapeJson(att.mimeType) +
			"\",\"content\":\"" +
			EscapeJson(att.contentBase64) +
			"\"}";
	}
	attachmentsJson += "]";

	const std::string params =
		std::string("{\"sessionKey\":\"") +
		m_chatState.sessionKey +
		"\",\"message\":\"" +
		EscapeJson(trimmed) +
		"\",\"deliver\":false,\"idempotencyKey\":\"" +
		runId +
		"\",\"attachments\":" +
		attachmentsJson +
		"}";
	const auto generation = m_chatSendGeneration;
	const HWND targetHwnd = GetSafeHwnd();
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr || !::IsWindow(targetHwnd))
	{
		m_chatState.chatSending = false;
		m_chatState.chatRunId.reset();
		m_chatState.chatStream.reset();
		m_chatState.chatStreamStartedAt.reset();
		m_chatState.lastError = "chat.send unavailable";
		SyncItemsFromState();
		UpdateControlStates();
		return;
	}

	std::thread(
		[app, targetHwnd, generation, runId, params]()
		{
			auto completion = std::make_unique<CChatView::NativeChatSendCompletionPayload>();
			completion->generation = generation;
			completion->runId = runId;

			blazeclaw::gateway::protocol::ResponseFrame response;
			const blazeclaw::gateway::protocol::RequestFrame request{
				.id = runId,
				.method = "chat.send",
				.paramsJson = params,
			};
			response = app->RouteGatewayRequest(request);

			completion->accepted = response.ok;
			if (!completion->accepted)
			{
				completion->errorDetail = "chat.send failed";
				if (response.error.has_value())
				{
					completion->errorDetail =
						response.error->code + ":" + response.error->message;
				}
			}

			if (!::PostMessage(
				targetHwnd,
				kNativeChatSendCompletedMessage,
				0,
				reinterpret_cast<LPARAM>(completion.get())))
			{
				return;
			}

			completion.release();
		})
		.detach();

	SyncItemsFromState();
	UpdateControlStates();
}

LRESULT CChatView::OnNativeChatSendCompleted(WPARAM /*wParam*/, LPARAM lParam)
{
	std::unique_ptr<NativeChatSendCompletionPayload> completion(
		reinterpret_cast<NativeChatSendCompletionPayload*>(lParam));
	if (!completion)
	{
		return 0;
	}

	if (completion->generation != m_chatSendGeneration)
	{
		return 0;
	}

	if (!completion->accepted)
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"send",
				std::string("chat.send error runId=") +
				completion->runId +
				" detail=" +
				completion->errorDetail));
			MaybeReportRunSkillPaths(completion->runId);
		}

		if (m_chatState.chatRunId.has_value() &&
			m_chatState.chatRunId.value() == completion->runId)
		{
			m_chatState.chatRunId.reset();
			m_chatState.chatStream.reset();
			m_chatState.chatStreamStartedAt.reset();
			m_chatState.lastError = "chat.send failed";
		}
	}
	else
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"send",
				std::string("chat.send accepted runId=") + completion->runId));
		}

		m_chatState.chatAttachments.clear();
	}

	m_chatState.chatSending = false;
	SyncItemsFromState();
	UpdateControlStates();
	return 0;
}

void CChatView::AbortChatRunNative()
{
	if (!IsGatewayConnected())
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"cancel",
				"abort skipped because gateway is disconnected."));
		}
		return;
	}

	if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
	{
		frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
			"cancel",
			std::string("abort requested session=") +
			m_chatState.sessionKey +
			" runId=" +
			m_chatState.chatRunId.value_or("none")));
	}

	std::string params =
		std::string("{\"sessionKey\":\"") +
		m_chatState.sessionKey +
		"\"";
	if (m_chatState.chatRunId.has_value())
	{
		params += ",\"runId\":\"" + m_chatState.chatRunId.value() + "\"";
	}
	params += "}";

	blazeclaw::gateway::protocol::ResponseFrame response;
	if (!RequestGateway("chat.abort", params, response) || !response.ok)
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"cancel",
				"chat.abort failed."));
		}
		m_chatState.lastError = "chat.abort failed";
	}
	else
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"cancel",
				"chat.abort accepted."));
		}
	}

	SyncItemsFromState();
}

void CChatView::HandleChatEventNative(const NativeChatEventPayload& payload)
{
	if (payload.sessionKey != m_chatState.sessionKey)
	{
		return;
	}

	if (payload.state == "delta")
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"lifecycle",
				std::string("state=delta runId=") + payload.runId));
		}

		const std::string next = payload.messageJson.has_value()
			? ExtractMessageText(payload.messageJson.value())
			: std::string();
		if (!next.empty() && !IsSilentReplyText(next))
		{
			const std::string current = m_chatState.chatStream.value_or(std::string());
			if (current.empty() || next.size() >= current.size())
			{
				m_chatState.chatStream = next;
			}
		}
		return;
	}

	if (payload.state == "final" || payload.state == "aborted")
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"lifecycle",
				std::string("state=") + payload.state +
				" runId=" +
				payload.runId));
		}

		if (payload.messageJson.has_value())
		{
			const std::string text = ExtractMessageText(payload.messageJson.value());
			if (!IsSilentReplyText(text))
			{
				m_chatState.chatMessages.push_back(payload.messageJson.value());
			}
		}
		else if (m_chatState.chatStream.has_value() &&
			!blazeclaw::gateway::json::Trim(m_chatState.chatStream.value()).empty() &&
			!IsSilentReplyText(m_chatState.chatStream.value()))
		{
			m_chatState.chatMessages.push_back(
				"{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"" +
				EscapeJson(m_chatState.chatStream.value()) +
				"\"}],\"timestamp\":" +
				std::to_string(CurrentEpochMs()) +
				"}");
		}

		m_chatState.chatRunId.reset();
		m_chatState.chatStream.reset();
		m_chatState.chatStreamStartedAt.reset();
		MaybeReportRunSkillPaths(payload.runId);
		return;
	}

	if (payload.state == "error")
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"lifecycle",
				std::string("state=error runId=") + payload.runId +
				" detail=" +
				payload.errorMessage.value_or("chat error")));
		}

		m_chatState.chatRunId.reset();
		m_chatState.chatStream.reset();
		m_chatState.chatStreamStartedAt.reset();
		m_chatState.lastError = payload.errorMessage.value_or("chat error");
		MaybeReportRunSkillPaths(payload.runId);
		return;
	}

	if (payload.state == "queued" || payload.state == "started")
	{
		if (auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd()); frame != nullptr)
		{
			frame->AddChatStatusLine(BuildDeepSeekDiagnosticLine(
				"lifecycle",
				std::string("state=") + payload.state +
				" runId=" +
				payload.runId));
		}
		return;
	}

}

void CChatView::MaybeReportRunSkillPaths(const std::string& runId)
{
	if (runId.empty())
	{
		return;
	}

	if (m_reportedSkillPathRunIds.find(runId) != m_reportedSkillPathRunIds.end())
	{
		return;
	}

	ReportTriedSkillPathsToFindOutput(runId);
	m_reportedSkillPathRunIds.insert(runId);
}

void CChatView::ReportTriedSkillPathsToFindOutput(const std::string& runId)
{
	auto* frame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (frame == nullptr)
	{
		return;
	}

	blazeclaw::gateway::protocol::ResponseFrame response;
	if (!RequestGateway(
		"gateway.runtime.taskDeltas.get",
		BuildRunTaskDeltasParams(runId),
		response))
	{
		frame->AddToolStatusLine(
			BuildDeepSeekDiagnosticLine(
				"skill-path",
				std::string("runId=") + runId +
				" taskDeltas query transport failed."));
		return;
	}

	if (!response.ok || !response.payloadJson.has_value())
	{
		std::string detail = std::string("runId=") + runId +
			" taskDeltas query failed";
		if (response.error.has_value())
		{
			detail += " errorCode=" +
				blazeclaw::gateway::json::Trim(response.error->code);
			detail += " errorMessage=" +
				TruncateDiagnosticText(
					blazeclaw::gateway::json::Trim(response.error->message));
		}

		if (response.payloadJson.has_value())
		{
			detail += " payload=" +
				TruncateDiagnosticText(
					blazeclaw::gateway::json::Trim(response.payloadJson.value()));
		}

		frame->AddToolStatusLine(
			BuildDeepSeekDiagnosticLine(
				"skill-path",
				detail));
		return;
	}

	const std::string payloadTrimmed =
		blazeclaw::gateway::json::Trim(response.payloadJson.value());
	std::string taskDeltasRaw;
	if (!blazeclaw::gateway::json::FindRawField(
		response.payloadJson.value(),
		"taskDeltas",
		taskDeltasRaw))
	{
		std::string payloadErrorCode;
		blazeclaw::gateway::json::FindStringField(
			response.payloadJson.value(),
			"errorCode",
			payloadErrorCode);

		std::string detail =
			std::string("runId=") + runId + " task deltas missing.";
		if (!blazeclaw::gateway::json::Trim(payloadErrorCode).empty())
		{
			detail += " errorCode=" +
				blazeclaw::gateway::json::Trim(payloadErrorCode);
		}

		detail += " payload=" + TruncateDiagnosticText(payloadTrimmed);

		frame->AddToolStatusLine(
			BuildDeepSeekDiagnosticLine(
				"skill-path",
				detail));
		return;
	}

	const auto taskDeltaObjects = SplitTopLevelObjects(taskDeltasRaw);
	if (taskDeltaObjects.empty())
	{
		frame->AddToolStatusLine(
			BuildDeepSeekDiagnosticLine(
				"skill-path",
				std::string("runId=") + runId +
				" no tried skill paths."));
		return;
	}

	frame->AddToolStatusLine(
		BuildDeepSeekDiagnosticLine(
			"skill-path",
			std::string("runId=") + runId + " tried skill paths:"));

	std::unordered_set<std::string> emitted;
	for (const auto& deltaJson : taskDeltaObjects)
	{
		std::string phase;
		blazeclaw::gateway::json::FindStringField(deltaJson, "phase", phase);

		if (phase == "fallback")
		{
			std::string status;
			std::string errorCode;
			std::string stepLabel;
			blazeclaw::gateway::json::FindStringField(deltaJson, "status", status);
			blazeclaw::gateway::json::FindStringField(deltaJson, "errorCode", errorCode);
			blazeclaw::gateway::json::FindStringField(deltaJson, "stepLabel", stepLabel);

			std::string fallbackLine =
				std::string("  - [fallback] ") +
				blazeclaw::gateway::json::Trim(stepLabel);
			if (!blazeclaw::gateway::json::Trim(status).empty())
			{
				fallbackLine += " status=" + blazeclaw::gateway::json::Trim(status);
			}
			if (!blazeclaw::gateway::json::Trim(errorCode).empty())
			{
				fallbackLine += " code=" + blazeclaw::gateway::json::Trim(errorCode);
			}

			if (emitted.find(fallbackLine) == emitted.end())
			{
				emitted.insert(fallbackLine);
				frame->AddToolStatusLine(
					CString(CA2W(fallbackLine.c_str(), CP_UTF8)));
			}
			continue;
		}

		if (phase != "tool_result")
		{
			continue;
		}

		std::string toolName;
		std::string status;
		std::string errorCode;
		blazeclaw::gateway::json::FindStringField(deltaJson, "toolName", toolName);
		blazeclaw::gateway::json::FindStringField(deltaJson, "status", status);
		blazeclaw::gateway::json::FindStringField(deltaJson, "errorCode", errorCode);

		const std::string label = ExtractToolPathLabel(toolName, status, errorCode);
		if (label.empty() || emitted.find(label) != emitted.end())
		{
			continue;
		}

		emitted.insert(label);
		frame->AddToolStatusLine(CString(CA2W((std::string("  - ") + label).c_str(), CP_UTF8)));
	}
}

void CChatView::PumpChatEventsNative()
{
	m_chatState.connected = IsGatewayConnected();
	if (!m_chatState.connected)
	{
		return;
	}

	blazeclaw::gateway::protocol::ResponseFrame response;
	const std::string params =
		std::string("{\"sessionKey\":\"") +
		m_chatState.sessionKey +
		"\",\"limit\":20}";
	if (!RequestGateway("chat.events.poll", params, response) || !response.ok ||
		!response.payloadJson.has_value())
	{
		return;
	}

	std::string eventsRaw;
	if (!blazeclaw::gateway::json::FindRawField(
		response.payloadJson.value(),
		"events",
		eventsRaw))
	{
		return;
	}

	bool changed = false;

	for (const auto& eventJson : SplitTopLevelObjects(eventsRaw))
	{
		NativeChatEventPayload payload;
		blazeclaw::gateway::json::FindStringField(eventJson, "runId", payload.runId);
		blazeclaw::gateway::json::FindStringField(
			eventJson,
			"sessionKey",
			payload.sessionKey);
		blazeclaw::gateway::json::FindStringField(eventJson, "state", payload.state);

		std::string messageRaw;
		if (blazeclaw::gateway::json::FindRawField(eventJson, "message", messageRaw))
		{
			payload.messageJson = blazeclaw::gateway::json::Trim(messageRaw);
		}

		std::string error;
		if (blazeclaw::gateway::json::FindStringField(eventJson, "errorMessage", error))
		{
			payload.errorMessage = error;
		}

		HandleChatEventNative(payload);
		changed = true;
	}

	if (changed)
	{
		SyncItemsFromState();
	}
}

void CChatView::SyncItemsFromState()
{
	if (!::IsWindow(m_wndMsgList.GetSafeHwnd()))
	{
		return;
	}

	if (m_renderedMessageCount > m_chatState.chatMessages.size())
	{
		RebuildItemsFromState();
		return;
	}

	auto appendJsonMessage = [this](const std::string& messageJson)
		{
			std::string role;
			blazeclaw::gateway::json::FindStringField(messageJson, "role", role);
			const std::string text = ExtractMessageText(messageJson);
			if (text.empty())
			{
				return;
			}

			CStringW textWide(CA2W(text.c_str(), CP_UTF8));
			AppendMessage(textWide, role == "user" ? TRUE : FALSE);
		};

	for (std::size_t i = m_renderedMessageCount;
		i < m_chatState.chatMessages.size();
		++i)
	{
		appendJsonMessage(m_chatState.chatMessages[i]);
	}
	m_renderedMessageCount = m_chatState.chatMessages.size();

	const std::string streamText =
		m_chatState.chatStream.has_value()
		? blazeclaw::gateway::json::Trim(m_chatState.chatStream.value())
		: std::string();
	if (!streamText.empty())
	{
		CStringW streamWide(CA2W(streamText.c_str(), CP_UTF8));
		if (m_streamItemIndex < 0)
		{
			m_streamItemIndex = AppendMessage(streamWide, FALSE);
		}
		else if (m_renderedStreamText != streamText)
		{
			UpdateItemAt(m_streamItemIndex, streamWide, FALSE);
		}
		m_renderedStreamText = streamText;
	}
	else if (m_streamItemIndex >= 0)
	{
		RemoveItemAt(m_streamItemIndex);
		m_renderedStreamText.clear();
	}

	const std::string errorText =
		(m_chatState.lastError.has_value() && !m_chatState.lastError->empty())
		? m_chatState.lastError.value()
		: std::string();
	if (!errorText.empty())
	{
		CStringW errorWide(CA2W(errorText.c_str(), CP_UTF8));
		CString line;
		line.Format(L"[Error] %s", errorWide.GetString());
		if (m_errorItemIndex < 0)
		{
			m_errorItemIndex = AppendMessage(line, FALSE);
		}
		else if (m_renderedErrorText != errorText)
		{
			UpdateItemAt(m_errorItemIndex, line, FALSE);
		}
		m_renderedErrorText = errorText;
	}
	else if (m_errorItemIndex >= 0)
	{
		RemoveItemAt(m_errorItemIndex);
		m_renderedErrorText.clear();
	}
}

void CChatView::RebuildItemsFromState()
{
	m_items.RemoveAll();
	m_wndMsgList.ResetContent();
	ResetRenderedItemsTracking();

	auto appendJsonMessage = [this](const std::string& messageJson)
		{
			std::string role;
			blazeclaw::gateway::json::FindStringField(messageJson, "role", role);
			const std::string text = ExtractMessageText(messageJson);
			if (text.empty())
			{
				return;
			}

			CStringW textWide(CA2W(text.c_str(), CP_UTF8));
			AppendMessage(textWide, role == "user" ? TRUE : FALSE);
		};

	for (const auto& messageJson : m_chatState.chatMessages)
	{
		appendJsonMessage(messageJson);
	}
	m_renderedMessageCount = m_chatState.chatMessages.size();

	if (m_chatState.chatStream.has_value() &&
		!blazeclaw::gateway::json::Trim(m_chatState.chatStream.value()).empty())
	{
		m_renderedStreamText = blazeclaw::gateway::json::Trim(
			m_chatState.chatStream.value());
		CStringW streamWide(CA2W(m_renderedStreamText.c_str(), CP_UTF8));
		m_streamItemIndex = AppendMessage(streamWide, FALSE);
	}

	if (m_chatState.lastError.has_value() && !m_chatState.lastError->empty())
	{
		m_renderedErrorText = m_chatState.lastError.value();
		CStringW errorWide(CA2W(m_renderedErrorText.c_str(), CP_UTF8));
		CString line;
		line.Format(L"[Error] %s", errorWide.GetString());
		m_errorItemIndex = AppendMessage(line, FALSE);
	}
}

void CChatView::ResetRenderedItemsTracking()
{
	m_renderedMessageCount = 0;
	m_streamItemIndex = -1;
	m_errorItemIndex = -1;
	m_renderedStreamText.clear();
	m_renderedErrorText.clear();
}

void CChatView::RemoveItemAt(int index)
{
	if (index < 0 || index >= m_items.GetSize())
	{
		return;
	}

	m_items.RemoveAt(index);
	m_wndMsgList.DeleteString(index);

	if (m_streamItemIndex == index)
	{
		m_streamItemIndex = -1;
	}
	else if (m_streamItemIndex > index)
	{
		--m_streamItemIndex;
	}

	if (m_errorItemIndex == index)
	{
		m_errorItemIndex = -1;
	}
	else if (m_errorItemIndex > index)
	{
		--m_errorItemIndex;
	}

	if (m_items.GetSize() > 0)
	{
		const int lastIndex = static_cast<int>(m_items.GetSize() - 1);
		m_wndMsgList.SetCurSel(lastIndex);
		m_wndMsgList.SetTopIndex(max(0, lastIndex - 1));
	}

	m_wndMsgList.Invalidate();
	m_wndMsgList.UpdateWindow();
}

void CChatView::UpdateItemAt(int index, const CString& strText, BOOL bSelf)
{
	if (index < 0 || index >= m_items.GetSize())
	{
		return;
	}

	m_items[index].text = strText;
	m_items[index].bSelf = bSelf;
	m_wndMsgList.DeleteString(index);
	const int inserted = m_wndMsgList.InsertString(index, _T(""));
	if (inserted != LB_ERR && inserted != LB_ERRSPACE)
	{
		m_wndMsgList.SetCurSel(inserted);
		m_wndMsgList.SetTopIndex(max(0, inserted - 1));
	}

	m_wndMsgList.Invalidate();
	m_wndMsgList.UpdateWindow();
}

void CChatView::UpdateControlStates()
{
	if (!::IsWindow(m_wndSend.GetSafeHwnd()))
	{
		return;
	}

	m_chatState.connected = IsGatewayConnected();
	const bool busy = m_chatState.chatLoading || m_chatState.chatSending;
	m_wndSend.EnableWindow(m_chatState.connected && !m_chatState.chatLoading);
	m_wndInput.EnableWindow(m_chatState.connected && !busy);
	m_wndAbort.EnableWindow(m_chatState.connected && m_chatState.chatRunId.has_value());
	m_wndAttach.EnableWindow(m_chatState.connected && !busy);

	// Voice button: show "Stop" during recording, "语音" when idle
	if (m_voiceRecorder.GetState() == VoiceRecorderState::Recording)
	{
		m_wndVoice.SetWindowText(_T("Stop"));
		m_wndVoice.EnableWindow(TRUE);
		m_wndSend.EnableWindow(FALSE);
		m_wndInput.EnableWindow(FALSE);
		m_wndAttach.EnableWindow(FALSE);
	}
	else
	{
		m_wndVoice.SetWindowText(_T("语音"));
		m_wndVoice.EnableWindow(m_chatState.connected && !busy);
	}

	CString sendText = m_chatState.chatSending ? L"发送中" : L"发送";
	m_wndSend.SetWindowText(sendText);
	CString attachText;
	attachText.Format(L"附件(%d)", static_cast<int>(m_chatState.chatAttachments.size()));
	m_wndAttach.SetWindowText(attachText);
}

void CChatView::AddStatusMessage(const CString& message)
{
	if (message.IsEmpty())
	{
		return;
	}

	AppendMessage(message, FALSE);
}

int CChatView::AppendMessage(const CString& strText, BOOL bSelf)
{
	CHAT_ITEM item;
	item.text = strText;
	item.bSelf = bSelf;
	int nIndex = (int)m_items.Add(item);
	m_wndMsgList.AddString(_T(""));
	if (nIndex != LB_ERR && nIndex != LB_ERRSPACE)
	{
		m_wndMsgList.Invalidate();
		m_wndMsgList.UpdateWindow();
		m_wndMsgList.SetCurSel(nIndex);
		m_wndMsgList.SetTopIndex(max(0, nIndex - 1));
	}

	return nIndex;
}

static void DrawBubble(CDC& dc, const CRect& rcBubble, bool bSelf)
{
	const COLORREF clrFill = bSelf ? RGB(149, 236, 105) : RGB(242, 242, 242);
	const COLORREF clrBorder = bSelf ? RGB(120, 210, 80) : RGB(220, 220, 220);

	CPen pen(PS_SOLID, 1, clrBorder);
	CBrush br(clrFill);
	CPen* pOldPen = dc.SelectObject(&pen);
	CBrush* pOldBrush = dc.SelectObject(&br);

	CRect rr = rcBubble;
	dc.RoundRect(rr, CPoint(10, 10));

	POINT pts[3]{};
	if (bSelf)
	{
		pts[0] = { rr.right, rr.top + 14 };
		pts[1] = { rr.right + 8, rr.top + 18 };
		pts[2] = { rr.right, rr.top + 22 };
	}
	else
	{
		pts[0] = { rr.left, rr.top + 14 };
		pts[1] = { rr.left - 8, rr.top + 18 };
		pts[2] = { rr.left, rr.top + 22 };
	}
	dc.Polygon(pts, 3);

	dc.SelectObject(pOldBrush);
	dc.SelectObject(pOldPen);
}

void CChatView::OnMeasureItem(int nIDCtl, LPMEASUREITEMSTRUCT lpMIS)
{
	if (nIDCtl != 1000 || lpMIS == nullptr)
	{
		CView::OnMeasureItem(nIDCtl, lpMIS);
		return;
	}

	if (lpMIS->itemID == (UINT)-1 || lpMIS->itemID >= (UINT)m_items.GetSize())
	{
		lpMIS->itemHeight = 24;
		return;
	}

	const CHAT_ITEM& item = m_items[(int)lpMIS->itemID];

	CRect rcClient;
	m_wndMsgList.GetClientRect(&rcClient);

	const int outerMarginV = 6;
	const int bubblePaddingH = 10;
	const int bubblePaddingV = 6;

	CDC* pDC = m_wndMsgList.GetDC();
	if (pDC == nullptr)
	{
		lpMIS->itemHeight = 32;
		return;
	}

	const int maxBubbleWidth = max(120, (int)(rcClient.Width() * 0.70f));
	const int maxTextWidth = maxBubbleWidth - 2 * bubblePaddingH;

	CFont* pOldFont = pDC->SelectObject(m_wndMsgList.GetFont());

	CRect rcCalc(0, 0, maxTextWidth, 0);
	pDC->DrawText(item.text, rcCalc,
		DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);

	pDC->SelectObject(pOldFont);

	const int bubbleH = rcCalc.Height() + 2 * bubblePaddingV;
	int itemH = bubbleH + 2 * outerMarginV;

	m_wndMsgList.ReleaseDC(pDC);
	lpMIS->itemHeight = max(28, itemH);
}

void CChatView::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS)
{
	if (nIDCtl != 1000 || lpDIS == nullptr)
	{
		CView::OnDrawItem(nIDCtl, lpDIS);
		return;
	}

	if (lpDIS->itemID == (UINT)-1 || lpDIS->itemID >= (UINT)m_items.GetSize())
		return;

	CDC dc;
	dc.Attach(lpDIS->hDC);

	CRect rcItem(lpDIS->rcItem);
	dc.FillSolidRect(rcItem, RGB(255, 255, 255));

	const CHAT_ITEM& item = m_items[(int)lpDIS->itemID];
	const bool bSelf = item.bSelf != FALSE;

	const int outerMarginH = 10;
	const int outerMarginV = 6;
	const int bubblePaddingH = 10;
	const int bubblePaddingV = 6;
	const int tail = 10;

	const int itemWidth = rcItem.Width();
	const int maxBubbleWidth = max(120, (int)(itemWidth * 0.70f));
	const int maxTextWidth = maxBubbleWidth - 2 * bubblePaddingH;

	CRect rcCalc(0, 0, maxTextWidth, 0);
	dc.SetBkMode(TRANSPARENT);
	CFont* pOldFont = dc.SelectObject(m_wndMsgList.GetFont());
	dc.DrawText(item.text, rcCalc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);

	const int bubbleW = min(maxBubbleWidth, rcCalc.Width() + 2 * bubblePaddingH);
	const int bubbleH = rcCalc.Height() + 2 * bubblePaddingV;

	CRect rcBubble;
	rcBubble.top = rcItem.top + outerMarginV;
	rcBubble.bottom = rcBubble.top + bubbleH;

	if (bSelf)
	{
		rcBubble.right = rcItem.right - outerMarginH - tail;
		rcBubble.left = rcBubble.right - bubbleW;
	}
	else
	{
		rcBubble.left = rcItem.left + outerMarginH + tail;
		rcBubble.right = rcBubble.left + bubbleW;
	}

	DrawBubble(dc, rcBubble, bSelf);

	CRect rcText = rcBubble;
	rcText.DeflateRect(bubblePaddingH, bubblePaddingV);
	dc.SetTextColor(RGB(0, 0, 0));
	dc.DrawText(item.text, rcText, DT_WORDBREAK | DT_NOPREFIX);

	dc.SelectObject(pOldFont);
	dc.Detach();
}
