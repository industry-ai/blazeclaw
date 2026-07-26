// IHooksGovernanceEmitter.h
#pragma once

#include "HooksGovernanceEmitter.h"

namespace blazeclaw::core {

	class IHooksGovernanceEmitter {
	public:
		virtual ~IHooksGovernanceEmitter() = default;

		virtual void EmitGovernanceReportIfNeeded(
			const HooksGovernanceEmitter::GovernanceContext& context,
			std::vector<std::wstring>& inOutWarnings) = 0;

		virtual void EmitRemediationLifecycleIfNeeded(
			const HooksGovernanceEmitter::RemediationContext& context,
			std::vector<std::wstring>& inOutWarnings) = 0;
	};

	class HooksGovernanceEmitterAdapter final : public IHooksGovernanceEmitter {
	public:
		void EmitGovernanceReportIfNeeded(
			const HooksGovernanceEmitter::GovernanceContext& context,
			std::vector<std::wstring>& inOutWarnings) override
		{
			emitter_.EmitGovernanceReportIfNeeded(context, inOutWarnings);
		}

		void EmitRemediationLifecycleIfNeeded(
			const HooksGovernanceEmitter::RemediationContext& context,
			std::vector<std::wstring>& inOutWarnings) override
		{
			emitter_.EmitRemediationLifecycleIfNeeded(context, inOutWarnings);
		}

	private:
		HooksGovernanceEmitter emitter_;
	};

}