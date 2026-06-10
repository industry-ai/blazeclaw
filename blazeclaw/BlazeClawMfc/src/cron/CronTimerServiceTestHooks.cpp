#include "pch.h"

#include "CronTimerServiceTestHooks.h"

namespace blazeclaw::cron::test_hooks {

	namespace {
		WebhookTransportDispatchFn g_webhookTransportDispatchStub;
	}

	void SetWebhookTransportDispatchStub(WebhookTransportDispatchFn stub) {
		g_webhookTransportDispatchStub = std::move(stub);
	}

	void ClearWebhookTransportDispatchStub() {
		g_webhookTransportDispatchStub = nullptr;
	}

	bool HasWebhookTransportDispatchStub() {
		return static_cast<bool>(g_webhookTransportDispatchStub);
	}

	WebhookTransportDispatchResult InvokeWebhookTransportDispatchStub(
		const std::string& url,
		const std::int64_t timeoutMs) {
		if (!g_webhookTransportDispatchStub) {
			return {};
		}

		return g_webhookTransportDispatchStub(url, timeoutMs);
	}

} // namespace blazeclaw::cron::test_hooks
