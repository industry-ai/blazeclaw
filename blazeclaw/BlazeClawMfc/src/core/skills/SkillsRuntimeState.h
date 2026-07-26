// SkillsRuntimeState.h
#pragma once

#include "../SkillsCatalogService.h"
#include "../SkillsEligibilityService.h"
#include "../SkillsPromptService.h"
#include "../SkillsCommandService.h"
#include "../SkillsSyncService.h"
#include "../SkillsEnvOverrideService.h"
#include "../SkillsInstallService.h"
#include "../SkillsWatchService.h"
#include "../SkillsFacade.h"
#include "../SkillSecurityScanService.h"
#include "../HookCatalogService.h"
#include "../HookExecutionService.h"
#include "../HookEventService.h"

namespace blazeclaw::core {

	struct SkillsRuntimeState {
		SkillsCatalogSnapshot catalog;
		SkillsEligibilitySnapshot eligibility;
		SkillsPromptSnapshot prompt;
		SkillsCommandSnapshot commands;
		SkillsSyncSnapshot sync;
		SkillsEnvOverrideSnapshot envOverrides;
		SkillsInstallSnapshot install;
		SkillsRunSnapshot runSnapshot;
		SkillSecurityScanSnapshot securityScan;
		SkillsWatchSnapshot watch;
	};

	struct HooksRuntimeState {
		HookCatalogSnapshot catalog;
		HookExecutionSnapshot execution;
		HookEventSnapshot events;
	};

	struct SkillsHooksRuntimeState {
		SkillsRuntimeState skills;
		HooksRuntimeState hooks;
	};

}