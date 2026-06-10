#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::cron::test_hooks {

	// Phase DI Step 6: injectable webhook transport for reproducible transport-dispatch tests.
	struct WebhookTransportDispatchResult {
		bool attempted = false;
		std::optional<std::int64_t> httpStatus;
		std::string error;
	};

	using WebhookTransportDispatchFn = std::function<WebhookTransportDispatchResult(
		const std::string& url,
		std::int64_t timeoutMs)>;

	void SetWebhookTransportDispatchStub(WebhookTransportDispatchFn stub);
	void ClearWebhookTransportDispatchStub();
	bool HasWebhookTransportDispatchStub();
	WebhookTransportDispatchResult InvokeWebhookTransportDispatchStub(
		const std::string& url,
		std::int64_t timeoutMs);

} // namespace blazeclaw::cron::test_hooks
