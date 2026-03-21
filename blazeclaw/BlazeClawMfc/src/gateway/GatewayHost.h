#pragma once

#include "../config/ConfigModels.h"
#include "GatewayAgentRegistry.h"
#include "GatewayChannelRegistry.h"
#include "GatewayMethodDispatcher.h"
#include "GatewaySessionRegistry.h"
#include "GatewayToolRegistry.h"
#include "GatewayWebSocketTransport.h"
#include "GatewayProtocolContract.h"

namespace blazeclaw::gateway {

	struct SkillsCatalogGatewayEntry {
		std::string name;
        std::string skillKey;
        std::string commandName;
        std::string installKind;
		std::string installCommand;
		bool installExecutable = false;
		std::string installReason;
		std::string description;
		std::string source;
		std::int32_t precedence = 0;
      bool eligible = false;
		bool disabled = false;
		bool blockedByAllowlist = false;
		bool disableModelInvocation = false;
		bool validFrontmatter = false;
		std::size_t validationErrorCount = 0;
	};

	struct SkillsCatalogGatewayState {
		std::vector<SkillsCatalogGatewayEntry> entries;
		std::size_t rootsScanned = 0;
		std::size_t rootsSkipped = 0;
		std::size_t oversizedSkillFiles = 0;
		std::size_t invalidFrontmatterFiles = 0;
		std::size_t warningCount = 0;
      std::size_t eligibleCount = 0;
		std::size_t disabledCount = 0;
		std::size_t blockedByAllowlistCount = 0;
		std::size_t missingRequirementsCount = 0;
		std::size_t promptIncludedCount = 0;
		std::size_t promptChars = 0;
		bool promptTruncated = false;
     std::uint64_t snapshotVersion = 0;
		bool watchEnabled = true;
		std::uint32_t watchDebounceMs = 250;
		std::string watchReason;
		std::string prompt;
      bool sandboxSyncOk = false;
		std::size_t sandboxSynced = 0;
		std::size_t sandboxSkipped = 0;
		std::size_t envAllowed = 0;
		std::size_t envBlocked = 0;
      std::size_t installExecutableCount = 0;
		std::size_t installBlockedCount = 0;
		std::size_t scanInfoCount = 0;
		std::size_t scanWarnCount = 0;
		std::size_t scanCriticalCount = 0;
		std::size_t scanScannedFiles = 0;
	};

	class GatewayHost {
	public:
     using SkillsRefreshCallback = std::function<SkillsCatalogGatewayState()>;

	 struct ChatRuntimeRequest {
		 std::string sessionKey;
		 std::string message;
		 bool hasAttachments = false;
	 };

	 struct ChatRuntimeResult {
		 bool ok = false;
		 std::string assistantText;
		 std::string modelId;
		 std::string errorCode;
		 std::string errorMessage;
	 };

	 using ChatRuntimeCallback = std::function<ChatRuntimeResult(const ChatRuntimeRequest&)>;

		bool Start(const blazeclaw::config::GatewayConfig& config);
		void Stop();
		void SetSkillsCatalogState(SkillsCatalogGatewayState state);
		void SetSkillsRefreshCallback(SkillsRefreshCallback callback);
		void SetChatRuntimeCallback(ChatRuntimeCallback callback);

		[[nodiscard]] bool IsRunning() const noexcept;
		[[nodiscard]] std::string LastWarning() const;
		[[nodiscard]] std::string HandleInboundText(const std::string& inboundJson) const;
		bool AcceptConnection(const std::string& connectionId, std::string& error);
		bool PumpInboundFrame(const std::string& connectionId, const std::string& inboundFrame, std::string& error);
		std::vector<std::string> DrainOutboundFrames(const std::string& connectionId, std::string& error);
		bool PumpNetworkOnce(std::string& error);
		[[nodiscard]] std::string BuildTickEventFrame(std::uint64_t timestampMs, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildHealthEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildChannelsUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildChannelsAccountsUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildToolsCatalogUpdateEventFrame(std::uint64_t seq) const;

		[[nodiscard]] protocol::ResponseFrame RouteRequest(const protocol::RequestFrame& request) const;

	private:
     struct AgentRunState {
			std::string runId;
			std::string agentId;
			std::string sessionId;
			std::string message;
			std::string status;
			std::string summary;
			std::uint64_t startedAtMs = 0;
			std::optional<std::uint64_t> completedAtMs;
		};

		struct ChatRunState {
			std::string runId;
			std::string sessionKey;
			std::string idempotencyKey;
			std::string userMessage;
			std::string assistantText;
          std::size_t streamCursor = 0;
			std::uint64_t lastEmitMs = 0;
			bool failed = false;
			std::string errorMessage;
			std::uint64_t startedAtMs = 0;
			bool active = true;
		};

		struct ChatEventState {
			std::string runId;
			std::string sessionKey;
			std::string state;
			std::optional<std::string> messageJson;
			std::optional<std::string> errorMessage;
			std::uint64_t timestampMs = 0;
		};

		void RegisterDefaultHandlers();
      void RegisterChannelsHandlers();
     void RegisterEventHandlers();
     void RegisterToolsHandlers();
     void RegisterScopeClusterHandlers();
     void RegisterGeneratedScopeClusterHandlers();
		void RegisterSecurityOpsHandlers();
		void RegisterRuntimeHandlers();
		void RegisterTransportHandlers();

		bool m_running = false;
		std::string m_bindAddress;
		std::uint16_t m_port = 0;
		std::string m_runtimeGatewayBind = "127.0.0.1";
		std::uint16_t m_runtimeGatewayPort = 18789;
		std::string m_runtimeAgentModel = "default";
		bool m_runtimeAgentStreaming = true;
      std::string m_runtimeAssignedSessionId = "main";
		std::string m_runtimeAssignedAgentId = "default";
		std::size_t m_runtimeQueueDepth = 0;
		std::size_t m_runtimeRunningCount = 0;
		std::size_t m_runtimeQueueCapacity = 8;
		std::size_t m_runtimeAssignmentCount = 0;
		std::size_t m_runtimeRebalanceCount = 0;
		std::size_t m_runtimeDrainCount = 0;
      std::size_t m_streamingBufferedFrames = 0;
		std::size_t m_streamingBufferedBytes = 0;
		std::size_t m_streamingHighWatermark = 16;
		std::size_t m_streamingWindowMs = 5000;
		std::size_t m_streamingThrottleLimitPerSec = 120;
		bool m_streamingThrottled = false;
		bool m_failoverOverrideActive = false;
		std::string m_failoverOverrideModel = "default";
		std::string m_failoverOverrideReason = "none";
		std::size_t m_failoverOverrideChanges = 0;
		std::size_t m_failoverAttempts = 0;
		std::size_t m_failoverFallbackHits = 0;
		std::string m_lastWarning;
		GatewayMethodDispatcher m_dispatcher;
		GatewayWebSocketTransport m_transport;
		GatewayAgentRegistry m_agentRegistry;
		GatewayChannelRegistry m_channelRegistry;
		GatewaySessionRegistry m_sessionRegistry;
		GatewayToolRegistry m_toolRegistry;
     std::unordered_map<std::string, AgentRunState> m_agentRuns;
		std::unordered_map<std::string, std::string> m_agentRunByIdempotency;
	  std::unordered_map<std::string, std::string> m_mutationPayloadByIdempotency;
      std::unordered_map<std::string, std::vector<std::string>> m_chatHistoryBySession;
	  std::unordered_map<std::string, std::deque<ChatEventState>> m_chatEventsBySession;
	  std::unordered_map<std::string, ChatRunState> m_chatRunsById;
	  std::unordered_map<std::string, std::string> m_chatRunByIdempotency;
        SkillsCatalogGatewayState m_skillsCatalogState;
      SkillsRefreshCallback m_skillsRefreshCallback;
      ChatRuntimeCallback m_chatRuntimeCallback;
	};

} // namespace blazeclaw::gateway
