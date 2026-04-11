#pragma once

#include <string>

namespace blazeclaw::gateway {

	class ChatAbortCoordinator {
	public:
		struct PersistPartialParams {
			std::string sessionKey;
			std::string runId;
			std::string text;
			std::string origin;
		};

		struct PersistPartialResult {
			bool persisted = false;
			std::string messageId;
			std::string messageJson;
			std::string error;
		};

		[[nodiscard]] PersistPartialResult PersistAbortedPartial(
			const PersistPartialParams& params) const;
	};

} // namespace blazeclaw::gateway
