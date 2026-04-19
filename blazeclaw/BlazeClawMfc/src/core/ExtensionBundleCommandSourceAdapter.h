#pragma once

#include "extensions/RuntimeCapabilityAdapterContracts.h"
#include "SkillsCommandService.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace blazeclaw::core {

	/// Loads slash-command bundles from the workspace `blazeclaw/extensions` tree for
	/// `IRuntimeSkillCommandSourceAdapter`.
	class ExtensionBundleCommandSourceAdapter final
		: public extensions::IRuntimeSkillCommandSourceAdapter {
	public:
		struct Diagnostics {
			std::size_t rootsScanned = 0;
			std::size_t filesLoaded = 0;
			std::size_t filesSkippedDisabled = 0;
			std::size_t filesSkippedEmptyPrompt = 0;
			std::size_t filesSkippedInvalidName = 0;
			std::size_t filesRejectedUnsafe = 0;
		};

		explicit ExtensionBundleCommandSourceAdapter(
			const std::filesystem::path& extensionsRoot);

		[[nodiscard]] const Diagnostics& LastDiagnostics() const;

		[[nodiscard]] extensions::RuntimeCapabilityDescriptor Describe() const override;

		[[nodiscard]] std::vector<SkillsCommandSpec> BuildAdditionalSkillsCommands(
			const extensions::RuntimeSkillCommandSourceAdapterContext& context) const override;

	private:
		std::filesystem::path m_extensionsRoot;
		mutable Diagnostics m_lastDiagnostics;
	};

} // namespace blazeclaw::core
