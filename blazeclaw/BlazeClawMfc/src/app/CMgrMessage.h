#pragma once

#include <Windows.h>
#include <afxstr.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class CMgrMessage final
{
public:
	struct MessageDescriptor
	{
		UINT messageId = 0;
		std::string name;
		std::string producer;
		std::string consumer;
		std::string payloadContract;
		std::string threadContract;
	};

	struct DiagnosticsSnapshot
	{
		std::uint64_t postAttempts = 0;
		std::uint64_t postSuccess = 0;
		std::uint64_t postFailures = 0;
		std::uint64_t posted = 0;
		std::uint64_t failedPost = 0;
		std::uint64_t handled = 0;
		std::uint64_t dropped = 0;
		std::uint64_t payloadCleanup = 0;
	};

	class IEngine
	{
	public:
		virtual ~IEngine() = default;
		virtual bool IsWindow(HWND hwnd) const = 0;
		virtual bool PostMessage(
			HWND hwnd,
			UINT messageId,
			WPARAM wParam,
			LPARAM lParam) const = 0;
	};

	static CMgrMessage& Instance();

	void Initialize(HWND mainFrameHwnd);
	void Shutdown();
	bool IsInitialized() const noexcept;

	void SetEngineForTesting(std::shared_ptr<IEngine> engine);
	void ResetEngineForTesting();

	void RegisterMessage(MessageDescriptor descriptor);
	std::optional<MessageDescriptor> FindMessage(UINT messageId) const;
	std::vector<MessageDescriptor> GetMessages() const;

	bool PostToMainFrame(
		UINT messageId,
		WPARAM wParam = 0,
		LPARAM lParam = 0) const noexcept;

	bool PostToHwnd(
		HWND targetHwnd,
		UINT messageId,
		WPARAM wParam = 0,
		LPARAM lParam = 0) const noexcept;

	// Convenience helper for existing kMsgAppendToolStatusLine payload flow.
	// Ownership remains with caller unless PostMessage succeeds.
	bool PostOwnedToolStatusLine(
		UINT messageId,
		CString* linePayload) const noexcept;

	using PayloadDeleter = void(*)(void*);

	// Generic owned payload helper for message posting paths that transfer
	// ownership to UI handlers via WPARAM or LPARAM.
	bool PostOwnedPayloadToHwnd(
		HWND targetHwnd,
		UINT messageId,
		void* payload,
		bool payloadInWparam,
		PayloadDeleter deleter) const noexcept;

	bool PostOwnedPayloadToMainFrame(
		UINT messageId,
		void* payload,
		bool payloadInWparam,
		PayloadDeleter deleter) const noexcept;

	using WebChannelHandler = std::function<bool(const std::string& rawMessageJson)>;
	void RegisterWebChannelHandler(
		const std::string& channel,
		WebChannelHandler handler);
	void UnregisterWebChannelHandler(const std::string& channel);
	void ClearWebChannelHandlers();
	bool DispatchWebChannelMessage(
		const std::string& channel,
		const std::string& rawMessageJson);

	DiagnosticsSnapshot GetDiagnostics() const noexcept;
	void ResetDiagnostics() noexcept;

private:
	CMgrMessage() = default;

	HWND m_mainFrameHwnd = nullptr;
	mutable std::mutex m_mutex;
	std::vector<MessageDescriptor> m_messages;
	std::unordered_map<std::string, WebChannelHandler> m_webChannelHandlers;
	std::shared_ptr<IEngine> m_engine;

	mutable std::atomic<std::uint64_t> m_postAttempts{ 0 };
	mutable std::atomic<std::uint64_t> m_postSuccess{ 0 };
	mutable std::atomic<std::uint64_t> m_postFailures{ 0 };
	mutable std::atomic<std::uint64_t> m_handled{ 0 };
	mutable std::atomic<std::uint64_t> m_dropped{ 0 };
	mutable std::atomic<std::uint64_t> m_payloadCleanup{ 0 };
};
