#include "pch.h"
#include "CMgrMessage.h"

#include <algorithm>

namespace {
	class Win32MessageEngine final : public CMgrMessage::IEngine
	{
	public:
		bool IsWindow(const HWND hwnd) const override
		{
			return hwnd != nullptr && ::IsWindow(hwnd);
		}

		bool PostMessage(
			const HWND hwnd,
			const UINT messageId,
			const WPARAM wParam,
			const LPARAM lParam) const override
		{
			return ::PostMessage(hwnd, messageId, wParam, lParam) != FALSE;
		}
	};
}

CMgrMessage& CMgrMessage::Instance()
{
	static CMgrMessage instance;
	return instance;
}

void CMgrMessage::Initialize(const HWND mainFrameHwnd)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_mainFrameHwnd = mainFrameHwnd;
	if (!m_engine)
	{
		m_engine = std::make_shared<Win32MessageEngine>();
	}
}

void CMgrMessage::Shutdown()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_mainFrameHwnd = nullptr;
}

bool CMgrMessage::IsInitialized() const noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mainFrameHwnd != nullptr;
}

void CMgrMessage::SetEngineForTesting(std::shared_ptr<IEngine> engine)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_engine = std::move(engine);
}

void CMgrMessage::ResetEngineForTesting()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_engine = std::make_shared<Win32MessageEngine>();
}

void CMgrMessage::RegisterMessage(MessageDescriptor descriptor)
{
	if (descriptor.messageId == 0)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = std::find_if(
		m_messages.begin(),
		m_messages.end(),
		[messageId = descriptor.messageId](const MessageDescriptor& entry)
		{
			return entry.messageId == messageId;
		});
	if (it != m_messages.end())
	{
		*it = std::move(descriptor);
		return;
	}

	m_messages.push_back(std::move(descriptor));
}

std::optional<CMgrMessage::MessageDescriptor> CMgrMessage::FindMessage(const UINT messageId) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = std::find_if(
		m_messages.begin(),
		m_messages.end(),
		[messageId](const MessageDescriptor& entry)
		{
			return entry.messageId == messageId;
		});
	if (it == m_messages.end())
	{
		return std::nullopt;
	}

	return *it;
}

std::vector<CMgrMessage::MessageDescriptor> CMgrMessage::GetMessages() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_messages;
}

bool CMgrMessage::PostToMainFrame(
	const UINT messageId,
	const WPARAM wParam,
	const LPARAM lParam) const noexcept
{
	HWND target = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		target = m_mainFrameHwnd;
	}

	return PostToHwnd(target, messageId, wParam, lParam);
}

bool CMgrMessage::PostToHwnd(
	const HWND targetHwnd,
	const UINT messageId,
	const WPARAM wParam,
	const LPARAM lParam) const noexcept
{
	m_postAttempts.fetch_add(1, std::memory_order_relaxed);

	std::shared_ptr<IEngine> engine;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		engine = m_engine;
	}

	if (!engine)
	{
		m_postFailures.fetch_add(1, std::memory_order_relaxed);
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	if (!engine->IsWindow(targetHwnd))
	{
		m_postFailures.fetch_add(1, std::memory_order_relaxed);
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	if (!engine->PostMessage(targetHwnd, messageId, wParam, lParam))
	{
		m_postFailures.fetch_add(1, std::memory_order_relaxed);
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	m_postSuccess.fetch_add(1, std::memory_order_relaxed);
	m_handled.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool CMgrMessage::PostOwnedToolStatusLine(
	const UINT messageId,
	CString* const linePayload) const noexcept
{
	if (linePayload == nullptr)
	{
		m_postAttempts.fetch_add(1, std::memory_order_relaxed);
		m_postFailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	if (PostToMainFrame(messageId, 0, reinterpret_cast<LPARAM>(linePayload)))
	{
		return true;
	}

	delete linePayload;
	return false;
}

bool CMgrMessage::PostOwnedPayloadToHwnd(
	const HWND targetHwnd,
	const UINT messageId,
	void* const payload,
	const bool payloadInWparam,
	const PayloadDeleter deleter) const noexcept
{
	if (payload == nullptr || deleter == nullptr)
	{
		m_postAttempts.fetch_add(1, std::memory_order_relaxed);
		m_postFailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	const WPARAM wParam = payloadInWparam
		? reinterpret_cast<WPARAM>(payload)
		: static_cast<WPARAM>(0);
	const LPARAM lParam = payloadInWparam
		? static_cast<LPARAM>(0)
		: reinterpret_cast<LPARAM>(payload);

	if (PostToHwnd(targetHwnd, messageId, wParam, lParam))
	{
		return true;
	}

	deleter(payload);
	m_payloadCleanup.fetch_add(1, std::memory_order_relaxed);
	return false;
}

bool CMgrMessage::PostOwnedPayloadToMainFrame(
	const UINT messageId,
	void* const payload,
	const bool payloadInWparam,
	const PayloadDeleter deleter) const noexcept
{
	HWND target = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		target = m_mainFrameHwnd;
	}

	return PostOwnedPayloadToHwnd(
		target,
		messageId,
		payload,
		payloadInWparam,
		deleter);
}

void CMgrMessage::RegisterWebChannelHandler(
	const std::string& channel,
	WebChannelHandler handler)
{
	if (channel.empty() || !handler)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_webChannelHandlers[channel] = std::move(handler);
}

void CMgrMessage::UnregisterWebChannelHandler(const std::string& channel)
{
	if (channel.empty())
	{
		return;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_webChannelHandlers.erase(channel);
}

void CMgrMessage::ClearWebChannelHandlers()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_webChannelHandlers.clear();
}

bool CMgrMessage::DispatchWebChannelMessage(
	const std::string& channel,
	const std::string& rawMessageJson)
{
	WebChannelHandler handler;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_webChannelHandlers.find(channel);
		if (it == m_webChannelHandlers.end())
		{
			m_dropped.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		handler = it->second;
	}

	if (!handler)
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	const bool handled = handler(rawMessageJson);
	if (handled)
	{
		m_handled.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
	}
	return handled;
}

CMgrMessage::DiagnosticsSnapshot CMgrMessage::GetDiagnostics() const noexcept
{
	DiagnosticsSnapshot snapshot;
	snapshot.postAttempts = m_postAttempts.load(std::memory_order_relaxed);
	snapshot.postSuccess = m_postSuccess.load(std::memory_order_relaxed);
	snapshot.postFailures = m_postFailures.load(std::memory_order_relaxed);
	snapshot.posted = snapshot.postSuccess;
	snapshot.failedPost = snapshot.postFailures;
	snapshot.handled = m_handled.load(std::memory_order_relaxed);
	snapshot.dropped = m_dropped.load(std::memory_order_relaxed);
	snapshot.payloadCleanup = m_payloadCleanup.load(std::memory_order_relaxed);
	return snapshot;
}

void CMgrMessage::ResetDiagnostics() noexcept
{
	m_postAttempts.store(0, std::memory_order_relaxed);
	m_postSuccess.store(0, std::memory_order_relaxed);
	m_postFailures.store(0, std::memory_order_relaxed);
	m_handled.store(0, std::memory_order_relaxed);
	m_dropped.store(0, std::memory_order_relaxed);
	m_payloadCleanup.store(0, std::memory_order_relaxed);
}
