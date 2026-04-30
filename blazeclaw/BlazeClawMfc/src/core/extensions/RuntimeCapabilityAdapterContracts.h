#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::gateway {
	class GatewayHost;
}

namespace blazeclaw::core {
	struct SkillsCommandSpec;
	struct SkillsCommandSnapshot;
}

namespace blazeclaw::core::extensions {

	struct RuntimeToolPolicySnapshot {
		std::optional<std::filesystem::path> imapSmtpSkillRoot;
		std::optional<std::filesystem::path> baiduSearchSkillRoot;
		std::optional<std::filesystem::path> braveSearchSkillRoot;
		std::optional<std::filesystem::path> openClawWebBrowsingSkillRoot;
		bool braveRequireApiKey = false;
		bool braveApiKeyPresent = false;
		bool enableOpenClawWebBrowsingFallback = false;
	};

	struct RuntimeCapabilityDescriptor {
		std::string capabilityId;
		std::string owner;
		std::string version = "v1";
		std::string publicContractVersion = "";
		std::string source = "internal";
		std::string contractScope = "internal";
		std::string stability = "internal-stable";
		std::string minHostVersion = "2026.0";
		std::string documentationRef;

		[[nodiscard]] bool IsPublicContract() const {
			return contractScope == "public";
		}

		[[nodiscard]] bool IsValidPublicContractVersion() const {
			if (!IsPublicContract()) {
				return true;
			}

			if (publicContractVersion.empty()) {
				return false;
			}

			const auto firstDot = publicContractVersion.find('.');
			if (firstDot == std::string::npos || firstDot == 0 ||
				firstDot + 1 >= publicContractVersion.size()) {
				return false;
			}

			for (std::size_t index = 0; index < publicContractVersion.size(); ++index) {
				const char ch = publicContractVersion[index];
				if (ch == '.') {
					continue;
				}

				if (ch < '0' || ch > '9') {
					return false;
				}
			}

			return true;
		}
	};

	struct RuntimeToolAdapterContext {
		blazeclaw::gateway::GatewayHost& host;
		const RuntimeToolPolicySnapshot& toolPolicy;
	};

	struct RuntimeSkillAdapterContext {
		const blazeclaw::core::SkillsCommandSnapshot& commandSnapshot;
		std::vector<blazeclaw::core::SkillsCommandSpec>& mutableCommands;
	};

	struct RuntimeSkillCommandSourceAdapterContext {
		const blazeclaw::core::SkillsCommandSnapshot& commandSnapshot;
	};

	class IRuntimeToolCapabilityAdapter {
	public:
		virtual ~IRuntimeToolCapabilityAdapter() = default;

		[[nodiscard]] virtual RuntimeCapabilityDescriptor Describe() const = 0;
		virtual void RegisterRuntimeTools(const RuntimeToolAdapterContext& context) const = 0;
	};

	class IRuntimeSkillCapabilityAdapter {
	public:
		virtual ~IRuntimeSkillCapabilityAdapter() = default;

		[[nodiscard]] virtual RuntimeCapabilityDescriptor Describe() const = 0;
		virtual void EnrichSkillsCommandSnapshot(
			const RuntimeSkillAdapterContext& context) const = 0;
	};

	class IRuntimeSkillCommandSourceAdapter {
	public:
		virtual ~IRuntimeSkillCommandSourceAdapter() = default;

		[[nodiscard]] virtual RuntimeCapabilityDescriptor Describe() const = 0;
		[[nodiscard]] virtual std::vector<blazeclaw::core::SkillsCommandSpec>
			BuildAdditionalSkillsCommands(
				const RuntimeSkillCommandSourceAdapterContext& context) const = 0;
	};

} // namespace blazeclaw::core::extensions
