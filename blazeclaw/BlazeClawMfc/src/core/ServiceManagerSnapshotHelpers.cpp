#include "pch.h"
#include "ServiceManagerSnapshotHelpers.h"

namespace blazeclaw::core::servicemanager_snapshot {

	GatewayStatusSnapshot CollectGatewayStatusSnapshot(const blazeclaw::config::AppConfig& config) {
		GatewayStatusSnapshot s;
		s.supported = true;
		s.ready = true;
		s.status = "ok";
		s.uptimeMs = 0;
		return s;
	}

	RuntimeHealthSnapshot CollectRuntimeHealthSnapshot() {
		RuntimeHealthSnapshot r;
		r.allSystemsGo = true;
		r.summary = "all_systems_nominal";
		return r;
	}

} // namespace blazeclaw::core::servicemanager_snapshot
