#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"
#include "GatewayProtocolSchemaValidator.Internal.h"

#include <functional>
#include <unordered_map>

namespace blazeclaw::gateway::protocol {
	namespace {
		constexpr const char* kSessionFieldTokens[] = { "id", "scope", "active" };
		constexpr const char* kAgentFieldTokens[] = { "id", "name", "active" };
		constexpr const char* kModelFieldTokens[] = { "id", "provider", "displayName", "streaming" };
		constexpr const char* kRouteFieldTokens[] = { "channel", "accountId", "agentId", "sessionId" };
		constexpr const char* kAccountFieldTokens[] = { "channel", "accountId", "label", "active", "connected" };
		constexpr const char* kChannelFieldTokens[] = { "id", "label", "connected", "accounts" };
		constexpr const char* kToolFieldTokens[] = { "id", "label", "category", "enabled" };
      constexpr const char* kToolExecutionFieldTokens[] = { "tool", "executed", "status", "output", "argsProvided" };
		constexpr const char* kChannelAdapterFieldTokens[] = { "id", "label", "defaultAccountId" };
		constexpr const char* kFileFieldTokens[] = { "path", "size", "updatedMs", "content" };
		constexpr const char* kFileListFieldTokens[] = { "path", "size", "updatedMs" };
		constexpr const char* kCatalogEventNames[] = {
			"gateway.tick",
			"gateway.health",
			"gateway.shutdown",
			"gateway.channels.update",
			"gateway.channels.accounts.update",
			"gateway.session.reset",
			"gateway.agent.update",
			"gateway.tools.catalog.update",
		};
		constexpr const char* kFeatureRequiredCatalogAll[] = {
			"gateway.ping",
			"gateway.transport.status",
			"gateway.events.catalog",
			"gateway.config.exists",
			"gateway.config.keys",
			"gateway.config.count",
			"gateway.config.sections",
			"gateway.config.getSection",
			"gateway.config.schema",
			"gateway.config.validate",
			"gateway.config.audit",
			"gateway.config.rollback",
			"gateway.config.backup",
			"gateway.config.diff",
			"gateway.config.snapshot",
			"gateway.config.revision",
			"gateway.config.history",
			"gateway.config.profile",
			"gateway.config.template",
			"gateway.config.bundle",
			"gateway.config.package",
			"gateway.config.archive",
			"gateway.config.manifest",
			"gateway.config.index",
			"gateway.config.state",
			"gateway.config.session",
			"gateway.config.scope",
			"gateway.config.context",
			"gateway.config.channel",
			"gateway.config.route",
			"gateway.config.account",
			"gateway.config.agent",
			"gateway.config.model",
			"gateway.config.config",
			"gateway.config.policy",
			"gateway.config.tool",
			"gateway.config.transport",
			"gateway.config.runtime",
			"gateway.config.stateKey",
			"gateway.config.healthKey",
			"gateway.config.log",
			"gateway.config.metric",
			"gateway.config.trace",
			"gateway.config.auditKey",
			"gateway.config.debug",
			"gateway.config.cache",
			"gateway.config.queueKey",
			"gateway.config.windowKey",
			"gateway.config.cursorKey",
			"gateway.config.anchorKey",
			"gateway.config.offsetKey",
			"gateway.config.markerKey",
			"gateway.config.pointerKey",
			"gateway.config.tokenKey",
			"gateway.config.sequenceKey",
			"gateway.config.streamKey",
			"gateway.config.bundleKey",
			"gateway.config.packageKey",
			"gateway.config.archiveKey",
			"gateway.config.manifestKey",
			"gateway.config.profileKey",
			"gateway.config.templateKey",
			"gateway.config.revisionKey",
			"gateway.config.historyKey",
			"gateway.config.snapshotKey",
			"gateway.config.indexKey",
			"gateway.config.windowScopeKey",
			"gateway.config.cursorScopeKey",
			"gateway.config.anchorScopeKey",
			"gateway.config.offsetScopeKey",
			"gateway.config.pointerScopeKey",
			"gateway.config.markerScopeKey",
			"gateway.config.tokenScopeKey",
			"gateway.config.streamScopeKey",
			"gateway.config.sequenceScopeKey",
			"gateway.config.bundleScopeKey",
			"gateway.config.packageScopeKey",
			"gateway.config.archiveScopeKey",
			"gateway.config.manifestScopeKey",
			"gateway.config.profileScopeKey",
			"gateway.config.templateScopeKey",
			"gateway.config.revisionScopeKey",
			"gateway.config.historyScopeKey",
			"gateway.config.snapshotScopeKey",
			"gateway.config.indexScopeKey",
			"gateway.config.windowScopeId",
			"gateway.config.cursorScopeId",
			"gateway.config.anchorScopeId",
			"gateway.config.offsetScopeId",
			"gateway.config.pointerScopeId",
			"gateway.config.tokenScopeId",
			"gateway.config.sequenceScopeId",
			"gateway.config.streamScopeId",
			"gateway.config.bundleScopeId",
			"gateway.config.packageScopeId",
			"gateway.config.archiveScopeId",
			"gateway.config.manifestScopeId",
			"gateway.config.profileScopeId",
			"gateway.config.templateScopeId",
			"gateway.config.revisionScopeId",
			"gateway.config.historyScopeId",
			"gateway.config.snapshotScopeId",
			"gateway.config.indexScopeId",
			"gateway.config.markerScopeId",
			"gateway.transport.connections.count",
			"gateway.transport.endpoint.get",
			"gateway.transport.endpoint.set",
			"gateway.transport.endpoints.list",
			"gateway.transport.policy.get",
			"gateway.transport.policy.set",
			"gateway.transport.policy.reset",
			"gateway.transport.policy.status",
			"gateway.transport.policy.validate",
			"gateway.transport.policy.history",
			"gateway.transport.policy.metrics",
			"gateway.transport.policy.export",
			"gateway.transport.policy.import",
			"gateway.transport.policy.digest",
			"gateway.transport.policy.preview",
			"gateway.transport.policy.commit",
			"gateway.transport.policy.apply",
			"gateway.transport.policy.stage",
			"gateway.transport.policy.reconcile",
			"gateway.transport.policy.sync",
			"gateway.transport.policy.refresh",
			"gateway.transport.policy.session",
			"gateway.transport.policy.scope",
			"gateway.transport.policy.context",
			"gateway.transport.policy.channel",
			"gateway.transport.policy.route",
			"gateway.transport.policy.account",
			"gateway.transport.policy.agent",
			"gateway.transport.policy.model",
			"gateway.transport.policy.config",
			"gateway.transport.policy.policy",
			"gateway.transport.policy.tool",
			"gateway.transport.policy.transport",
			"gateway.transport.policy.runtime",
			"gateway.transport.policy.stateKey",
			"gateway.transport.policy.healthKey",
			"gateway.transport.policy.log",
			"gateway.transport.policy.metric",
			"gateway.transport.policy.trace",
			"gateway.transport.policy.audit",
			"gateway.transport.policy.debug",
			"gateway.transport.policy.cache",
			"gateway.transport.policy.queueKey",
			"gateway.transport.policy.windowKey",
			"gateway.transport.policy.cursorKey",
			"gateway.transport.policy.anchorKey",
			"gateway.transport.policy.offsetKey",
			"gateway.transport.policy.markerKey",
			"gateway.transport.policy.pointerKey",
			"gateway.transport.policy.tokenKey",
			"gateway.transport.policy.sequenceKey",
			"gateway.transport.policy.streamKey",
			"gateway.transport.policy.bundleKey",
			"gateway.transport.policy.packageKey",
			"gateway.transport.policy.archiveKey",
			"gateway.transport.policy.manifestKey",
			"gateway.transport.policy.profileKey",
			"gateway.transport.policy.templateKey",
			"gateway.transport.policy.revisionKey",
			"gateway.transport.policy.historyKey",
			"gateway.transport.policy.snapshotKey",
			"gateway.transport.policy.indexKey",
			"gateway.transport.policy.windowScopeKey",
			"gateway.transport.policy.cursorScopeKey",
			"gateway.transport.policy.anchorScopeKey",
			"gateway.transport.policy.offsetScopeKey",
			"gateway.transport.policy.pointerScopeKey",
			"gateway.transport.policy.markerScopeKey",
			"gateway.transport.policy.tokenScopeKey",
			"gateway.transport.policy.streamScopeKey",
			"gateway.transport.policy.sequenceScopeKey",
			"gateway.transport.policy.bundleScopeKey",
			"gateway.transport.policy.packageScopeKey",
			"gateway.transport.policy.archiveScopeKey",
			"gateway.transport.policy.manifestScopeKey",
			"gateway.transport.policy.profileScopeKey",
			"gateway.transport.policy.templateScopeKey",
			"gateway.transport.policy.revisionScopeKey",
			"gateway.transport.policy.historyScopeKey",
			"gateway.transport.policy.snapshotScopeKey",
			"gateway.transport.policy.indexScopeKey",
			"gateway.transport.policy.windowScopeId",
			"gateway.transport.policy.cursorScopeId",
			"gateway.transport.policy.anchorScopeId",
			"gateway.transport.policy.offsetScopeId",
			"gateway.transport.policy.pointerScopeId",
			"gateway.transport.policy.tokenScopeId",
			"gateway.transport.policy.sequenceScopeId",
			"gateway.transport.policy.streamScopeId",
			"gateway.transport.policy.bundleScopeId",
			"gateway.transport.policy.packageScopeId",
			"gateway.transport.policy.archiveScopeId",
			"gateway.transport.policy.manifestScopeId",
			"gateway.transport.policy.profileScopeId",
			"gateway.transport.policy.templateScopeId",
			"gateway.transport.policy.revisionScopeId",
			"gateway.transport.policy.historyScopeId",
			"gateway.transport.policy.snapshotScopeId",
			"gateway.transport.policy.indexScopeId",
			"gateway.transport.policy.markerScopeId",
			"gateway.health.details",
			"gateway.logs.count",
			"gateway.logs.levels",
			"gateway.events.get",
			"gateway.events.last",
			"gateway.events.search",
			"gateway.events.summary",
			"gateway.events.types",
			"gateway.events.channels",
			"gateway.events.timeline",
			"gateway.events.latestByType",
			"gateway.events.sample",
			"gateway.events.window",
			"gateway.events.recent",
			"gateway.events.batch",
			"gateway.events.cursor",
			"gateway.events.anchor",
			"gateway.events.offset",
			"gateway.events.marker",
			"gateway.events.sequence",
			"gateway.events.pointer",
			"gateway.events.token",
			"gateway.events.stream",
			"gateway.events.sessionKey",
			"gateway.events.scopeKey",
			"gateway.events.contextKey",
			"gateway.events.channelKey",
			"gateway.events.routeKey",
			"gateway.events.accountKey",
			"gateway.events.agentKey",
			"gateway.events.modelKey",
			"gateway.events.configKey",
			"gateway.events.policyKey",
			"gateway.events.toolKey",
			"gateway.events.transportKey",
			"gateway.events.runtimeKey",
			"gateway.events.stateKey",
			"gateway.events.healthKey",
			"gateway.events.logKey",
			"gateway.events.metricKey",
			"gateway.events.traceKey",
			"gateway.events.auditKey",
			"gateway.events.debugKey",
			"gateway.events.cacheKey",
			"gateway.events.queueKey",
			"gateway.events.windowKey",
			"gateway.events.cursorKey",
			"gateway.events.anchorKey",
			"gateway.events.offsetKey",
			"gateway.events.markerKey",
			"gateway.events.pointerKey",
			"gateway.events.tokenKey",
			"gateway.events.sequenceKey",
			"gateway.events.streamKey",
			"gateway.events.bundleKey",
			"gateway.events.packageKey",
			"gateway.events.archiveKey",
			"gateway.events.manifestKey",
			"gateway.events.profileKey",
			"gateway.events.templateKey",
			"gateway.events.revisionKey",
			"gateway.events.historyKey",
			"gateway.events.snapshotKey",
			"gateway.events.indexKey",
			"gateway.events.windowScopeKey",
			"gateway.events.cursorScopeKey",
			"gateway.events.anchorScopeKey",
			"gateway.events.offsetScopeKey",
			"gateway.events.pointerScopeKey",
			"gateway.events.markerScopeKey",
			"gateway.events.tokenScopeKey",
			"gateway.events.streamScopeKey",
			"gateway.events.sequenceScopeKey",
			"gateway.events.bundleScopeKey",
			"gateway.events.packageScopeKey",
			"gateway.events.archiveScopeKey",
			"gateway.events.manifestScopeKey",
			"gateway.events.profileScopeKey",
			"gateway.events.templateScopeKey",
			"gateway.events.revisionScopeKey",
			"gateway.events.historyScopeKey",
			"gateway.events.snapshotScopeKey",
			"gateway.events.indexScopeKey",
			"gateway.events.windowScopeId",
			"gateway.events.cursorScopeId",
			"gateway.events.anchorScopeId",
			"gateway.events.offsetScopeId",
			"gateway.events.pointerScopeId",
			"gateway.events.tokenScopeId",
			"gateway.events.sequenceScopeId",
			"gateway.events.streamScopeId",
			"gateway.events.bundleScopeId",
			"gateway.events.packageScopeId",
			"gateway.events.archiveScopeId",
			"gateway.events.manifestScopeId",
			"gateway.events.profileScopeId",
			"gateway.events.templateScopeId",
			"gateway.events.revisionScopeId",
			"gateway.events.historyScopeId",
			"gateway.events.snapshotScopeId",
			"gateway.events.indexScopeId",
			"gateway.events.markerScopeId",
			"gateway.agents.create",
			"gateway.sessions.delete",
			"gateway.sessions.compact",
			"gateway.sessions.patch",
			"gateway.sessions.preview",
			"gateway.sessions.usage",
			"gateway.sessions.exists",
			"gateway.sessions.count",
			"gateway.sessions.activate",
			"gateway.events.exists",
			"gateway.events.count",
			"gateway.events.list",
			"gateway.channels.accounts",
			"gateway.channels.accounts.activate",
			"gateway.channels.accounts.deactivate",
			"gateway.channels.accounts.exists",
			"gateway.channels.accounts.update",
			"gateway.channels.accounts.get",
			"gateway.channels.accounts.create",
			"gateway.channels.accounts.delete",
			"gateway.channels.accounts.clear",
			"gateway.channels.accounts.restore",
			"gateway.channels.accounts.count",
			"gateway.channels.accounts.reset",
			"gateway.channels.status.get",
			"gateway.channels.status.exists",
			"gateway.channels.status.count",
			"gateway.channels.adapters.list",
			"gateway.channels.route.exists",
			"gateway.channels.route.get",
			"gateway.channels.route.restore",
			"gateway.channels.route.reset",
			"gateway.channels.route.patch",
			"gateway.channels.routes.clear",
			"gateway.channels.routes.restore",
			"gateway.channels.routes.reset",
			"gateway.channels.routes.count",
			"gateway.tools.call.preview",
			"gateway.tools.exists",
			"gateway.tools.count",
			"gateway.tools.get",
			"gateway.tools.list",
			"gateway.tools.executions.list",
         "gateway.tools.executions.count",
			"gateway.tools.executions.latest",
			"gateway.tools.executions.clear",
			"gateway.tools.categories",
			"gateway.tools.stats",
			"gateway.tools.health",
			"gateway.tools.metrics",
			"gateway.tools.failures",
			"gateway.tools.usage",
			"gateway.tools.latency",
			"gateway.tools.errors",
			"gateway.tools.throughput",
			"gateway.tools.capacity",
			"gateway.tools.queue",
			"gateway.tools.scheduler",
			"gateway.tools.backlog",
			"gateway.tools.window",
			"gateway.tools.pipeline",
			"gateway.tools.dispatch",
			"gateway.tools.router",
			"gateway.tools.selector",
			"gateway.tools.mapper",
			"gateway.tools.binding",
			"gateway.tools.profile",
			"gateway.tools.channel",
			"gateway.tools.route",
			"gateway.tools.account",
			"gateway.tools.agent",
			"gateway.tools.model",
			"gateway.tools.config",
			"gateway.tools.policy",
			"gateway.tools.tool",
			"gateway.tools.transport",
			"gateway.tools.runtime",
			"gateway.tools.state",
			"gateway.tools.healthKey",
			"gateway.tools.log",
			"gateway.tools.metric",
			"gateway.tools.trace",
			"gateway.tools.audit",
			"gateway.tools.debug",
			"gateway.tools.cache",
			"gateway.tools.queueKey",
			"gateway.tools.windowKey",
			"gateway.tools.cursorKey",
			"gateway.tools.anchorKey",
			"gateway.tools.offsetKey",
			"gateway.tools.markerKey",
			"gateway.tools.pointerKey",
			"gateway.tools.tokenKey",
			"gateway.tools.sequenceKey",
			"gateway.tools.streamKey",
			"gateway.tools.bundleKey",
			"gateway.tools.packageKey",
			"gateway.tools.archiveKey",
			"gateway.tools.manifestKey",
			"gateway.tools.profileKey",
			"gateway.tools.templateKey",
			"gateway.tools.revisionKey",
			"gateway.tools.historyKey",
			"gateway.tools.snapshotKey",
			"gateway.tools.indexKey",
			"gateway.tools.windowScopeKey",
			"gateway.tools.cursorScopeKey",
			"gateway.tools.anchorScopeKey",
			"gateway.tools.offsetScopeKey",
			"gateway.tools.pointerScopeKey",
			"gateway.tools.markerScopeKey",
			"gateway.tools.tokenScopeKey",
			"gateway.tools.streamScopeKey",
			"gateway.tools.sequenceScopeKey",
			"gateway.tools.bundleScopeKey",
			"gateway.tools.packageScopeKey",
			"gateway.tools.archiveScopeKey",
			"gateway.tools.manifestScopeKey",
			"gateway.tools.profileScopeKey",
			"gateway.tools.templateScopeKey",
			"gateway.tools.revisionScopeKey",
			"gateway.tools.historyScopeKey",
			"gateway.tools.snapshotScopeKey",
			"gateway.tools.indexScopeKey",
			"gateway.tools.windowScopeId",
			"gateway.tools.cursorScopeId",
			"gateway.tools.anchorScopeId",
			"gateway.tools.offsetScopeId",
			"gateway.tools.pointerScopeId",
			"gateway.tools.tokenScopeId",
			"gateway.tools.sequenceScopeId",
			"gateway.tools.streamScopeId",
			"gateway.tools.bundleScopeId",
			"gateway.tools.packageScopeId",
			"gateway.tools.archiveScopeId",
			"gateway.tools.manifestScopeId",
			"gateway.tools.profileScopeId",
			"gateway.tools.templateScopeId",
			"gateway.tools.revisionScopeId",
			"gateway.tools.historyScopeId",
			"gateway.tools.snapshotScopeId",
			"gateway.tools.indexScopeId",
			"gateway.tools.markerScopeId",
			"gateway.models.exists",
			"gateway.models.count",
			"gateway.models.get",
			"gateway.models.listByProvider",
			"gateway.models.providers",
			"gateway.models.default.get",
			"gateway.models.compatibility",
			"gateway.models.recommended",
			"gateway.models.fallback",
			"gateway.models.selection",
			"gateway.models.routing",
			"gateway.models.preference",
			"gateway.models.priority",
			"gateway.models.affinity",
			"gateway.models.pool",
			"gateway.models.manifest",
			"gateway.models.catalog",
			"gateway.models.inventory",
			"gateway.models.snapshot",
			"gateway.models.registry",
			"gateway.models.index",
			"gateway.models.state",
			"gateway.models.session",
			"gateway.models.scope",
			"gateway.models.context",
			"gateway.models.channel",
			"gateway.models.route",
			"gateway.models.account",
			"gateway.models.agent",
			"gateway.models.model",
			"gateway.models.config",
			"gateway.models.policy",
			"gateway.models.tool",
			"gateway.models.transport",
			"gateway.models.runtime",
			"gateway.models.stateKey",
			"gateway.models.healthKey",
			"gateway.models.log",
			"gateway.models.metric",
			"gateway.models.trace",
			"gateway.models.audit",
			"gateway.models.debug",
			"gateway.models.cache",
			"gateway.models.queueKey",
			"gateway.models.windowKey",
			"gateway.models.cursorKey",
			"gateway.models.anchorKey",
			"gateway.models.offsetKey",
			"gateway.models.markerKey",
			"gateway.models.pointerKey",
			"gateway.models.tokenKey",
			"gateway.models.sequenceKey",
			"gateway.models.streamKey",
			"gateway.models.bundleKey",
			"gateway.models.packageKey",
			"gateway.models.archiveKey",
			"gateway.models.manifestKey",
			"gateway.models.profileKey",
			"gateway.models.templateKey",
			"gateway.models.revisionKey",
			"gateway.models.historyKey",
			"gateway.models.snapshotKey",
			"gateway.models.indexKey",
			"gateway.models.windowScopeKey",
			"gateway.models.cursorScopeKey",
			"gateway.models.anchorScopeKey",
			"gateway.models.offsetScopeKey",
			"gateway.models.pointerScopeKey",
			"gateway.models.markerScopeKey",
			"gateway.models.tokenScopeKey",
			"gateway.models.streamScopeKey",
			"gateway.models.sequenceScopeKey",
			"gateway.models.bundleScopeKey",
			"gateway.models.packageScopeKey",
			"gateway.models.archiveScopeKey",
			"gateway.models.manifestScopeKey",
			"gateway.models.profileScopeKey",
			"gateway.models.templateScopeKey",
			"gateway.models.revisionScopeKey",
			"gateway.models.historyScopeKey",
			"gateway.models.snapshotScopeKey",
			"gateway.models.indexScopeKey",
			"gateway.models.windowScopeId",
			"gateway.models.cursorScopeId",
			"gateway.models.anchorScopeId",
			"gateway.models.offsetScopeId",
			"gateway.models.pointerScopeId",
			"gateway.models.tokenScopeId",
			"gateway.models.sequenceScopeId",
			"gateway.models.streamScopeId",
			"gateway.models.bundleScopeId",
			"gateway.models.packageScopeId",
			"gateway.models.archiveScopeId",
			"gateway.models.manifestScopeId",
			"gateway.models.profileScopeId",
			"gateway.models.templateScopeId",
			"gateway.models.revisionScopeId",
			"gateway.models.historyScopeId",
			"gateway.models.snapshotScopeId",
			"gateway.models.indexScopeId",
			"gateway.models.markerScopeId",
			"gateway.models.failover.status",
			"gateway.models.failover.preview",
			"gateway.models.failover.metrics",
			"gateway.models.failover.simulate",
			"gateway.models.failover.audit",
			"gateway.models.failover.policy",
			"gateway.models.failover.history",
			"gateway.models.failover.recent",
			"gateway.models.failover.window",
			"gateway.models.failover.digest",
			"gateway.models.failover.ledger",
			"gateway.models.failover.profile",
			"gateway.models.failover.baseline",
			"gateway.models.failover.forecast",
			"gateway.models.failover.threshold",
			"gateway.models.failover.guardrail",
			"gateway.models.failover.envelope",
			"gateway.models.failover.margin",
			"gateway.models.failover.reserve",
			"gateway.runtime.orchestration.status",
			"gateway.runtime.orchestration.queue",
			"gateway.runtime.orchestration.assign",
			"gateway.runtime.orchestration.rebalance",
			"gateway.runtime.orchestration.drain",
			"gateway.runtime.orchestration.snapshot",
			"gateway.runtime.orchestration.timeline",
			"gateway.runtime.orchestration.heartbeat",
			"gateway.runtime.orchestration.pulse",
			"gateway.runtime.orchestration.cadence",
			"gateway.runtime.orchestration.beacon",
			"gateway.runtime.orchestration.epoch",
			"gateway.runtime.orchestration.phase",
			"gateway.runtime.orchestration.signal",
			"gateway.runtime.orchestration.vector",
			"gateway.runtime.orchestration.matrix",
			"gateway.runtime.orchestration.lattice",
			"gateway.runtime.orchestration.mesh",
			"gateway.runtime.orchestration.fabric",
			"gateway.runtime.orchestration.load",
			"gateway.runtime.orchestration.saturation",
			"gateway.runtime.orchestration.pressure",
			"gateway.runtime.orchestration.headroom",
			"gateway.runtime.orchestration.balance",
			"gateway.runtime.orchestration.efficiency",
			"gateway.runtime.orchestration.utilization",
			"gateway.runtime.orchestration.capacity",
			"gateway.runtime.orchestration.occupancy",
			"gateway.runtime.orchestration.elasticity",
			"gateway.runtime.orchestration.cohesion",
			"gateway.runtime.orchestration.resilience",
			"gateway.runtime.orchestration.readiness",
			"gateway.runtime.orchestration.contention",
			"gateway.runtime.orchestration.fairness",
			"gateway.runtime.orchestration.equilibrium",
			"gateway.runtime.orchestration.steadiness",
			"gateway.runtime.orchestration.parity",
			"gateway.runtime.orchestration.stabilityIndex",
			"gateway.runtime.orchestration.convergence",
			"gateway.runtime.orchestration.hysteresis",
			"gateway.runtime.orchestration.balanceIndex",
			"gateway.runtime.orchestration.phaseLock",
			"gateway.runtime.orchestration.symmetry",
			"gateway.runtime.orchestration.gradient",
			"gateway.runtime.orchestration.harmonicity",
			"gateway.runtime.orchestration.inertia",
			"gateway.runtime.orchestration.cadenceIndex",
			"gateway.runtime.orchestration.damping",
			"gateway.runtime.orchestration.waveLock",
			"gateway.runtime.orchestration.flux",
			"gateway.runtime.orchestration.vectorField",
			"gateway.runtime.orchestration.phaseEnvelope",
			"gateway.runtime.orchestration.vectorDrift",
			"gateway.runtime.orchestration.phaseBias",
			"gateway.runtime.orchestration.phaseVector",
			"gateway.runtime.orchestration.biasEnvelope",
			"gateway.runtime.orchestration.phaseMatrix",
			"gateway.runtime.orchestration.driftEnvelope",
			"gateway.runtime.orchestration.phaseLattice",
			"gateway.runtime.orchestration.envelopeDrift",
			"gateway.runtime.orchestration.phaseContour",
			"gateway.runtime.orchestration.driftVector",
			"gateway.runtime.orchestration.phaseRibbon",
			"gateway.runtime.orchestration.vectorEnvelope",
			"gateway.runtime.orchestration.phaseHelix",
			"gateway.runtime.orchestration.vectorContour",
			"gateway.runtime.orchestration.phaseSpiral",
			"gateway.runtime.orchestration.vectorRibbon",
			"gateway.runtime.orchestration.phaseArc",
			"gateway.runtime.orchestration.vectorSpiral",
			"gateway.runtime.orchestration.phaseMesh",
			"gateway.runtime.orchestration.vectorArc",
			"gateway.runtime.orchestration.phaseFabric",
			"gateway.runtime.orchestration.vectorMesh",
			"gateway.runtime.orchestration.phaseNet",
			"gateway.runtime.orchestration.vectorNode",
			"gateway.runtime.orchestration.phaseCore",
			"gateway.runtime.orchestration.vectorCore",
			"gateway.runtime.orchestration.phaseFrame",
			"gateway.runtime.orchestration.vectorFrame",
			"gateway.runtime.orchestration.phaseSpan",
			"gateway.runtime.orchestration.vectorSpan",
			"gateway.runtime.orchestration.phaseGrid",
			"gateway.runtime.orchestration.vectorGrid",
			"gateway.runtime.orchestration.phaseLane",
			"gateway.runtime.orchestration.vectorLane",
			"gateway.runtime.orchestration.phaseTrack",
			"gateway.runtime.orchestration.vectorTrack",
			"gateway.runtime.orchestration.phaseRail",
			"gateway.runtime.orchestration.vectorRail",
			"gateway.runtime.orchestration.phaseSpline",
			"gateway.runtime.orchestration.vectorSpline",
			"gateway.runtime.streaming.status",
			"gateway.runtime.streaming.sample",
			"gateway.runtime.streaming.window",
			"gateway.runtime.streaming.backpressure",
			"gateway.runtime.streaming.replay",
			"gateway.runtime.streaming.cursor",
			"gateway.runtime.streaming.metrics",
			"gateway.runtime.streaming.health",
			"gateway.runtime.streaming.snapshot",
			"gateway.runtime.streaming.watermark",
			"gateway.runtime.streaming.checkpoint",
			"gateway.runtime.streaming.resume",
			"gateway.runtime.streaming.recovery",
			"gateway.runtime.streaming.continuity",
			"gateway.runtime.streaming.stability",
			"gateway.runtime.streaming.integrity",
			"gateway.runtime.streaming.coherence",
			"gateway.runtime.streaming.fidelity",
			"gateway.runtime.streaming.accuracy",
			"gateway.config.getKey",
			"gateway.transport.endpoint.exists",
			"gateway.tick",
			"gateway.health",
			"gateway.runtime.orchestration.status",
			"gateway.runtime.orchestration.queue",
			"gateway.runtime.orchestration.assign",
			"gateway.runtime.orchestration.rebalance",
			"gateway.runtime.orchestration.drain",
			"gateway.runtime.orchestration.snapshot",
			"gateway.runtime.orchestration.timeline",
			"gateway.runtime.orchestration.heartbeat",
			"gateway.runtime.orchestration.pulse",
			"gateway.runtime.orchestration.cadence",
			"gateway.runtime.orchestration.beacon",
			"gateway.runtime.orchestration.epoch",
			"gateway.runtime.orchestration.phase",
			"gateway.runtime.orchestration.signal",
			"gateway.runtime.orchestration.vector",
			"gateway.runtime.orchestration.matrix",
			"gateway.runtime.orchestration.lattice",
			"gateway.runtime.orchestration.mesh",
			"gateway.runtime.orchestration.fabric",
			"gateway.runtime.streaming.status",
			"gateway.runtime.streaming.sample",
			"gateway.runtime.streaming.window",
			"gateway.runtime.streaming.backpressure",
			"gateway.runtime.streaming.replay",
			"gateway.runtime.streaming.cursor",
			"gateway.runtime.streaming.metrics",
			"gateway.runtime.streaming.health",
			"gateway.runtime.streaming.snapshot",
			"gateway.runtime.streaming.watermark",
			"gateway.runtime.streaming.checkpoint",
			"gateway.runtime.streaming.resume",
			"gateway.runtime.streaming.recovery",
			"gateway.runtime.streaming.continuity",
			"gateway.runtime.streaming.stability",
			"gateway.runtime.streaming.integrity",
			"gateway.runtime.streaming.coherence",
			"gateway.runtime.streaming.fidelity",
			"gateway.runtime.streaming.accuracy",
			"gateway.runtime.streaming.buffer",
			"gateway.runtime.streaming.throttle",
			"gateway.runtime.streaming.pacing",
			"gateway.runtime.streaming.jitter",
			"gateway.runtime.streaming.drift",
			"gateway.runtime.streaming.variance",
			"gateway.runtime.streaming.deviation",
			"gateway.runtime.streaming.alignment",
			"gateway.runtime.streaming.skew",
			"gateway.runtime.streaming.dispersion",
			"gateway.runtime.streaming.curvature",
			"gateway.runtime.streaming.smoothness",
			"gateway.runtime.streaming.harmonics",
			"gateway.runtime.streaming.phase",
			"gateway.runtime.streaming.tempo",
			"gateway.runtime.streaming.temporal",
			"gateway.runtime.streaming.consistency",
			"gateway.runtime.streaming.spectral",
			"gateway.runtime.streaming.envelope",
			"gateway.runtime.streaming.resonance",
			"gateway.runtime.streaming.vectorField",
			"gateway.runtime.streaming.waveform",
			"gateway.runtime.streaming.horizon",
			"gateway.runtime.streaming.vectorClock",
			"gateway.runtime.streaming.trend",
			"gateway.runtime.streaming.coordination",
			"gateway.runtime.streaming.latencyBand",
			"gateway.runtime.streaming.phaseNoise",
			"gateway.runtime.streaming.beat",
			"gateway.runtime.streaming.modulation",
			"gateway.runtime.streaming.pulseTrain",
			"gateway.runtime.streaming.cohesion",
			"gateway.runtime.streaming.waveIndex",
			"gateway.runtime.streaming.syncBand",
			"gateway.runtime.streaming.waveDrift",
			"gateway.runtime.streaming.syncEnvelope",
			"gateway.runtime.streaming.bandDrift",
			"gateway.runtime.streaming.syncVector",
			"gateway.runtime.streaming.bandEnvelope",
			"gateway.runtime.streaming.syncMatrix",
			"gateway.runtime.streaming.bandVector",
			"gateway.runtime.streaming.syncContour",
			"gateway.runtime.streaming.bandMatrix",
			"gateway.runtime.streaming.syncRibbon",
			"gateway.runtime.streaming.bandContour",
			"gateway.runtime.streaming.syncHelix",
			"gateway.runtime.streaming.bandRibbon",
			"gateway.runtime.streaming.syncSpiral",
			"gateway.runtime.streaming.bandHelix",
			"gateway.runtime.streaming.syncArc",
			"gateway.runtime.streaming.bandSpiral",
			"gateway.runtime.streaming.syncMesh",
			"gateway.runtime.streaming.bandLattice",
			"gateway.runtime.streaming.syncFabric",
			"gateway.runtime.streaming.bandArc",
			"gateway.runtime.streaming.syncNet",
			"gateway.runtime.streaming.bandNode",
			"gateway.runtime.streaming.syncCore",
			"gateway.runtime.streaming.bandCore",
			"gateway.runtime.streaming.syncFrame",
			"gateway.runtime.streaming.bandFrame",
			"gateway.runtime.streaming.syncSpan",
			"gateway.runtime.streaming.bandSpan",
			"gateway.runtime.streaming.syncGrid",
			"gateway.runtime.streaming.bandGrid",
			"gateway.runtime.streaming.syncLane",
			"gateway.runtime.streaming.bandLane",
			"gateway.runtime.streaming.syncTrack",
			"gateway.runtime.streaming.bandTrack",
			"gateway.runtime.streaming.syncRail",
			"gateway.runtime.streaming.bandRail",
			"gateway.runtime.streaming.syncSpline",
			"gateway.runtime.streaming.bandSpline",
			"gateway.models.failover.status",
			"gateway.models.failover.preview",
			"gateway.models.failover.metrics",
			"gateway.models.failover.simulate",
			"gateway.models.failover.audit",
			"gateway.models.failover.policy",
			"gateway.models.failover.history",
			"gateway.models.failover.recent",
			"gateway.models.failover.window",
			"gateway.models.failover.digest",
			"gateway.models.failover.ledger",
			"gateway.models.failover.profile",
			"gateway.models.failover.baseline",
			"gateway.models.failover.forecast",
			"gateway.models.failover.threshold",
			"gateway.models.failover.guardrail",
			"gateway.models.failover.envelope",
			"gateway.models.failover.margin",
			"gateway.models.failover.reserve",
			"gateway.models.failover.override",
			"gateway.models.failover.override.clear",
			"gateway.models.failover.override.status",
			"gateway.models.failover.override.history",
			"gateway.models.failover.override.metrics",
			"gateway.models.failover.override.window",
			"gateway.models.failover.override.digest",
			"gateway.models.failover.override.timeline",
			"gateway.models.failover.override.catalog",
			"gateway.models.failover.override.registry",
			"gateway.models.failover.override.matrix",
			"gateway.models.failover.override.snapshot",
			"gateway.models.failover.override.pointer",
			"gateway.models.failover.override.state",
			"gateway.models.failover.override.profile",
			"gateway.models.failover.override.audit",
			"gateway.models.failover.override.checkpoint",
			"gateway.models.failover.override.baseline",
			"gateway.models.failover.override.manifest",
			"gateway.models.failover.override.ledger",
			"gateway.models.failover.override.snapshotIndex",
			"gateway.models.failover.override.digestIndex",
			"gateway.models.failover.override.cursor",
			"gateway.models.failover.override.vector",
			"gateway.models.failover.override.vectorDrift",
			"gateway.models.failover.override.phaseBias",
			"gateway.models.failover.override.biasEnvelope",
			"gateway.models.failover.override.driftEnvelope",
			"gateway.models.failover.override.envelopeDrift",
			"gateway.models.failover.override.driftVector",
			"gateway.models.failover.override.vectorEnvelope",
			"gateway.models.failover.override.vectorContour",
			"gateway.models.failover.override.vectorRibbon",
			"gateway.models.failover.override.vectorSpiral",
			"gateway.models.failover.override.vectorArc",
			"gateway.models.failover.override.vectorMesh",
			"gateway.models.failover.override.vectorNode",
			"gateway.models.failover.override.vectorCore",
			"gateway.models.failover.override.vectorFrame",
			"gateway.models.failover.override.vectorSpan",
			"gateway.models.failover.override.vectorGrid",
			"gateway.models.failover.override.vectorLane",
			"gateway.models.failover.override.vectorTrack",
			"gateway.models.failover.override.vectorRail",
			"gateway.models.failover.override.vectorSpline",
			"gateway.shutdown",
		};
		constexpr const char* kFeatureRequiredConfigCluster[] = {
			"gateway.config.validate",
			"gateway.config.audit",
			"gateway.config.snapshot",
			"gateway.config.schema",
		};
		constexpr const char* kFeatureRequiredTransportCluster[] = {
			"gateway.transport.status",
			"gateway.transport.policy.get",
			"gateway.transport.policy.history",
			"gateway.transport.policy.validate",
		};
		constexpr const char* kFeatureRequiredEventCluster[] = {
			"gateway.events.catalog",
			"gateway.events.list",
			"gateway.events.types",
			"gateway.events.summary",
		};
		constexpr const char* kFeatureRequiredModelToolCluster[] = {
			"gateway.models.get",
			"gateway.models.listByProvider",
			"gateway.tools.get",
			"gateway.tools.list",
		};

		bool ValidateObjectWithTokens(
			const std::string& payload,
			const char* objectField,
			const char* const* tokens,
			std::size_t tokenCount,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, objectField, '{')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			for (std::size_t index = 0; index < tokenCount; ++index) {
				std::size_t tokenPos = 0;
				if (!ContainsFieldToken(payload, tokens[index], tokenPos)) {
					SetIssue(issue, "schema_invalid_response", errorMessage);
					return false;
				}
			}

			return true;
		}

		template <std::size_t N>
		bool ValidateObjectWithTokens(
			const std::string& payload,
			const char* objectField,
			const char* const (&tokens)[N],
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			return ValidateObjectWithTokens(payload, objectField, tokens, N, issue, errorMessage);
		}

		template <std::size_t N>
		bool ValidateObjectWithBooleanAndTokens(
			const std::string& payload,
			const char* objectField,
			const char* booleanField,
			const char* const (&tokens)[N],
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, objectField, '{') || !IsFieldBoolean(payload, booleanField)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return PayloadContainsAllFieldTokens(payload, tokens)
				? true
				: (SetIssue(issue, "schema_invalid_response", errorMessage), false);
		}

		template <std::size_t N>
		bool ValidateArrayWithOptionalEntryTokens(
			const std::string& payload,
			const char* arrayField,
			const char* const (&tokens)[N],
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, arrayField, '[')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, arrayField)) {
				return true;
			}

			for (const char* token : tokens) {
				std::size_t tokenPos = 0;
				if (!ContainsFieldToken(payload, token, tokenPos)) {
					SetIssue(issue, "schema_invalid_response", errorMessage);
					return false;
				}
			}

			return true;
		}

		bool ValidateTwoStringFields(
			const std::string& payload,
			const char* first,
			const char* second,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, first, '"') || !IsFieldValueType(payload, second, '"')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanNumberBoolean(
			const std::string& payload,
			const char* boolField1,
			const char* numberField,
			const char* boolField2,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, boolField1) || !IsFieldNumber(payload, numberField) || !IsFieldBoolean(payload, boolField2)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateArrayAndCount(
			const std::string& payload,
			const char* arrayField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, arrayField, '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateNumericFields(
			const std::string& payload,
			std::initializer_list<const char*> fieldNames,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			for (const char* fieldName : fieldNames) {
				if (!IsFieldNumber(payload, fieldName)) {
					SetIssue(issue, "schema_invalid_response", errorMessage);
					return false;
				}
			}

			return true;
		}

		bool ValidateStringArrayAndCount(
			const std::string& payload,
			const char* stringField,
			const char* arrayField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, stringField, '"') || !IsFieldValueType(payload, arrayField, '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateNumberAndString(
			const std::string& payload,
			const char* numberField,
			const char* stringField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldNumber(payload, numberField) || !IsFieldValueType(payload, stringField, '"')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateStringAndBoolean(
			const std::string& payload,
			const char* stringField,
			const char* booleanField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, stringField, '"') || !IsFieldBoolean(payload, booleanField)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanArrayAndCount(
			const std::string& payload,
			const char* booleanField,
			const char* arrayField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, booleanField) || !IsFieldValueType(payload, arrayField, '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanAndString(
			const std::string& payload,
			const char* booleanField,
			const char* stringField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, booleanField) || !IsFieldValueType(payload, stringField, '"')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanAndNumber(
			const std::string& payload,
			const char* booleanField,
			const char* numberField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, booleanField) || !IsFieldNumber(payload, numberField)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}
	}

	bool GatewayProtocolSchemaValidator::ValidateResponseForMethod(
		const std::string& method,
		const ResponseFrame& response,
		SchemaValidationIssue& issue) {
		issue = {};

		if (!ValidateResponseEnvelope(response, issue)) {
			return false;
		}

		const std::string payload = response.payloadJson.value_or("{}");

		const std::unordered_map<std::string, std::function<bool()>> groupedValidators = {
			{ "gateway.channels.status", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"channels",
					kChannelFieldTokens,
					issue,
					"`gateway.channels.status` requires channel entries with `id`, `label`, `connected`, and `accounts` fields.");
			} },
			{ "gateway.channels.routes", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"routes",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.routes` requires route entries with `channel`, `accountId`, `agentId`, and `sessionId` fields.");
			} },
			{ "gateway.channels.accounts", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"accounts",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts` requires account entries with `channel`, `accountId`, `label`, `active`, and `connected` fields.");
			} },
			{ "gateway.channels.adapters.list", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"adapters",
					kChannelAdapterFieldTokens,
					issue,
					"`gateway.channels.adapters.list` requires adapter entries with `id`, `label`, and `defaultAccountId` fields.");
			} },
			{ "gateway.channels.accounts.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"account",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.get` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
			} },
            { "gateway.tools.executions.list", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"executions",
					kToolExecutionFieldTokens,
					issue,
					"`gateway.tools.executions.list` requires execution entries with `tool`, `executed`, `status`, `output`, and `argsProvided` fields.");
			} },
            { "gateway.tools.executions.count", [&]() {
				return ValidateNumericFields(
					payload,
					{ "count", "succeeded", "failed" },
					issue,
					"`gateway.tools.executions.count` requires numeric fields `count`, `succeeded`, and `failed`.");
			} },
			{ "gateway.tools.executions.latest", [&]() {
				const bool hasFoundAndCount =
					IsFieldBoolean(payload, "found") &&
					IsFieldNumber(payload, "count");
				if (!hasFoundAndCount) {
					SetIssue(
						issue,
						"schema_invalid_response",
						"`gateway.tools.executions.latest` requires `found` boolean and `count` number fields.");
					return false;
				}

				return ValidateObjectWithTokens(
					payload,
					"execution",
					kToolExecutionFieldTokens,
					issue,
					"`gateway.tools.executions.latest` requires `execution` fields `tool`, `executed`, `status`, `output`, and `argsProvided`.");
			} },
			{ "gateway.tools.executions.clear", [&]() {
				return ValidateNumericFields(
					payload,
					{ "cleared", "remaining" },
					issue,
					"`gateway.tools.executions.clear` requires numeric fields `cleared` and `remaining`.");
			} },
			{ "gateway.channels.status.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"channel",
					kChannelFieldTokens,
					issue,
					"`gateway.channels.status.get` requires channel fields `id`, `label`, `connected`, and `accounts`.");
			} },
			{ "gateway.channels.accounts.activate", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"activated",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.activate` requires `account` object and `activated` boolean.");
			} },
			{ "gateway.channels.accounts.deactivate", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"deactivated",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.deactivate` requires `account` object and `deactivated` boolean.");
			} },
			{ "gateway.channels.accounts.update", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"updated",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.update` requires `account` object and `updated` boolean.");
			} },
			{ "gateway.channels.accounts.create", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"created",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.create` requires `account` object and `created` boolean.");
			} },
			{ "gateway.channels.accounts.delete", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"deleted",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.delete` requires `account` object and `deleted` boolean.");
			} },
			{ "gateway.channels.route.resolve", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"route",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.resolve` requires route object fields `channel`, `accountId`, `agentId`, and `sessionId`.");
			} },
			{ "gateway.channels.route.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"route",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.get` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
			} },
			{ "gateway.channels.route.set", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"saved",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.set` requires `route` object and `saved` boolean.");
			} },
			{ "gateway.channels.route.delete", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"deleted",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.delete` requires `route` object and `deleted` boolean.");
			} },
			{ "gateway.channels.route.restore", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"restored",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.restore` requires `route` object and `restored` boolean.");
			} },
			{ "gateway.channels.route.patch", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"updated",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.patch` requires `route` object and `updated` boolean.");
			} },
			{ "gateway.channels.route.reset", [&]() {
				if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldBoolean(payload, "restored")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.reset` requires `route` object, `deleted` boolean, and `restored` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kRouteFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.reset` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.resolve", [&]() {
				return ValidateObjectWithTokens(payload, "session", kSessionFieldTokens, issue, "`gateway.sessions.resolve` requires session fields `id`, `scope`, and `active`.");
			} },
			{ "gateway.sessions.create", [&]() {
				return ValidateObjectWithTokens(payload, "session", kSessionFieldTokens, issue, "`gateway.sessions.create` requires session fields `id`, `scope`, and `active`.");
			} },
			{ "gateway.sessions.reset", [&]() {
				return ValidateObjectWithTokens(payload, "session", kSessionFieldTokens, issue, "`gateway.sessions.reset` requires session fields `id`, `scope`, and `active`.");
			} },
			{ "gateway.agents.get", [&]() {
				return ValidateObjectWithTokens(payload, "agent", kAgentFieldTokens, issue, "`gateway.agents.get` requires agent fields `id`, `name`, and `active`.");
			} },
			{ "gateway.agents.activate", [&]() {
				return ValidateObjectWithTokens(payload, "agent", kAgentFieldTokens, issue, "`gateway.agents.activate` requires agent fields `id`, `name`, and `active`.");
			} },
			{ "gateway.events.stream", [&]() {
				return ValidateTwoStringFields(payload, "stream", "event", issue, "`gateway.events.stream` requires `stream` string and `event` string.");
			} },
			{ "gateway.events.sessionKey", [&]() {
				return ValidateTwoStringFields(payload, "sessionKey", "event", issue, "`gateway.events.sessionKey` requires `sessionKey` string and `event` string.");
			} },
			{ "gateway.events.scopeKey", [&]() {
				return ValidateTwoStringFields(payload, "scopeKey", "event", issue, "`gateway.events.scopeKey` requires `scopeKey` string and `event` string.");
			} },
			{ "gateway.events.contextKey", [&]() {
				return ValidateTwoStringFields(payload, "contextKey", "event", issue, "`gateway.events.contextKey` requires `contextKey` string and `event` string.");
			} },
			{ "gateway.events.channelKey", [&]() {
				return ValidateTwoStringFields(payload, "channelKey", "event", issue, "`gateway.events.channelKey` requires `channelKey` string and `event` string.");
			} },
			{ "gateway.events.routeKey", [&]() {
				return ValidateTwoStringFields(payload, "routeKey", "event", issue, "`gateway.events.routeKey` requires `routeKey` string and `event` string.");
			} },
			{ "gateway.events.accountKey", [&]() {
				return ValidateTwoStringFields(payload, "accountKey", "event", issue, "`gateway.events.accountKey` requires `accountKey` string and `event` string.");
			} },
			{ "gateway.events.agentKey", [&]() {
				return ValidateTwoStringFields(payload, "agentKey", "event", issue, "`gateway.events.agentKey` requires `agentKey` string and `event` string.");
			} },
			{ "gateway.events.modelKey", [&]() {
				return ValidateTwoStringFields(payload, "modelKey", "event", issue, "`gateway.events.modelKey` requires `modelKey` string and `event` string.");
			} },
			{ "gateway.events.configKey", [&]() {
				return ValidateTwoStringFields(payload, "configKey", "event", issue, "`gateway.events.configKey` requires `configKey` string and `event` string.");
			} },
			{ "gateway.events.policyKey", [&]() {
				return ValidateTwoStringFields(payload, "policyKey", "event", issue, "`gateway.events.policyKey` requires `policyKey` string and `event` string.");
			} },
			{ "gateway.events.toolKey", [&]() {
				return ValidateTwoStringFields(payload, "toolKey", "event", issue, "`gateway.events.toolKey` requires `toolKey` string and `event` string.");
			} },
			{ "gateway.events.transportKey", [&]() {
				return ValidateTwoStringFields(payload, "transportKey", "event", issue, "`gateway.events.transportKey` requires `transportKey` string and `event` string.");
			} },
			{ "gateway.events.runtimeKey", [&]() {
				return ValidateTwoStringFields(payload, "runtimeKey", "event", issue, "`gateway.events.runtimeKey` requires `runtimeKey` string and `event` string.");
			} },
			{ "gateway.events.stateKey", [&]() {
				return ValidateTwoStringFields(payload, "stateKey", "event", issue, "`gateway.events.stateKey` requires `stateKey` string and `event` string.");
			} },
			{ "gateway.events.healthKey", [&]() {
				return ValidateTwoStringFields(payload, "healthKey", "event", issue, "`gateway.events.healthKey` requires `healthKey` string and `event` string.");
			} },
			{ "gateway.events.logKey", [&]() {
				return ValidateTwoStringFields(payload, "logKey", "event", issue, "`gateway.events.logKey` requires `logKey` string and `event` string.");
			} },
			{ "gateway.events.metricKey", [&]() {
				return ValidateTwoStringFields(payload, "metricKey", "event", issue, "`gateway.events.metricKey` requires `metricKey` string and `event` string.");
			} },
			{ "gateway.events.traceKey", [&]() {
				return ValidateTwoStringFields(payload, "traceKey", "event", issue, "`gateway.events.traceKey` requires `traceKey` string and `event` string.");
			} },
			{ "gateway.events.auditKey", [&]() {
				return ValidateTwoStringFields(payload, "auditKey", "event", issue, "`gateway.events.auditKey` requires `auditKey` string and `event` string.");
			} },
			{ "gateway.events.debugKey", [&]() {
				return ValidateTwoStringFields(payload, "debugKey", "event", issue, "`gateway.events.debugKey` requires `debugKey` string and `event` string.");
			} },
			{ "gateway.events.cacheKey", [&]() {
				return ValidateTwoStringFields(payload, "cacheKey", "event", issue, "`gateway.events.cacheKey` requires `cacheKey` string and `event` string.");
			} },
			{ "gateway.events.queueKey", [&]() {
				return ValidateTwoStringFields(payload, "queueKey", "event", issue, "`gateway.events.queueKey` requires `queueKey` string and `event` string.");
			} },
			{ "gateway.events.windowKey", [&]() {
				return ValidateTwoStringFields(payload, "windowKey", "event", issue, "`gateway.events.windowKey` requires `windowKey` string and `event` string.");
			} },
			{ "gateway.events.cursorKey", [&]() {
				return ValidateTwoStringFields(payload, "cursorKey", "event", issue, "`gateway.events.cursorKey` requires `cursorKey` string and `event` string.");
			} },
			{ "gateway.events.anchorKey", [&]() {
				return ValidateTwoStringFields(payload, "anchorKey", "event", issue, "`gateway.events.anchorKey` requires `anchorKey` string and `event` string.");
			} },
			{ "gateway.events.offsetKey", [&]() {
				return ValidateTwoStringFields(payload, "offsetKey", "event", issue, "`gateway.events.offsetKey` requires `offsetKey` string and `event` string.");
			} },
			{ "gateway.events.markerKey", [&]() {
				return ValidateTwoStringFields(payload, "markerKey", "event", issue, "`gateway.events.markerKey` requires `markerKey` string and `event` string.");
			} },
			{ "gateway.events.pointerKey", [&]() {
				return ValidateTwoStringFields(payload, "pointerKey", "event", issue, "`gateway.events.pointerKey` requires `pointerKey` string and `event` string.");
			} },
			{ "gateway.events.tokenKey", [&]() {
				return ValidateTwoStringFields(payload, "tokenKey", "event", issue, "`gateway.events.tokenKey` requires `tokenKey` string and `event` string.");
			} },
			{ "gateway.events.sequenceKey", [&]() {
				return ValidateTwoStringFields(payload, "sequenceKey", "event", issue, "`gateway.events.sequenceKey` requires `sequenceKey` string and `event` string.");
			} },
			{ "gateway.events.streamKey", [&]() {
				return ValidateTwoStringFields(payload, "streamKey", "event", issue, "`gateway.events.streamKey` requires `streamKey` string and `event` string.");
			} },
			{ "gateway.events.bundleKey", [&]() {
				return ValidateTwoStringFields(payload, "bundleKey", "event", issue, "`gateway.events.bundleKey` requires `bundleKey` string and `event` string.");
			} },
			{ "gateway.events.packageKey", [&]() {
				return ValidateTwoStringFields(payload, "packageKey", "event", issue, "`gateway.events.packageKey` requires `packageKey` string and `event` string.");
			} },
			{ "gateway.events.archiveKey", [&]() {
				return ValidateTwoStringFields(payload, "archiveKey", "event", issue, "`gateway.events.archiveKey` requires `archiveKey` string and `event` string.");
			} },
			{ "gateway.events.manifestKey", [&]() {
				return ValidateTwoStringFields(payload, "manifestKey", "event", issue, "`gateway.events.manifestKey` requires `manifestKey` string and `event` string.");
			} },
			{ "gateway.events.profileKey", [&]() {
				return ValidateTwoStringFields(payload, "profileKey", "event", issue, "`gateway.events.profileKey` requires `profileKey` string and `event` string.");
			} },
			{ "gateway.events.templateKey", [&]() {
				return ValidateTwoStringFields(payload, "templateKey", "event", issue, "`gateway.events.templateKey` requires `templateKey` string and `event` string.");
			} },
			{ "gateway.events.revisionKey", [&]() {
				return ValidateTwoStringFields(payload, "revisionKey", "event", issue, "`gateway.events.revisionKey` requires `revisionKey` string and `event` string.");
			} },
			{ "gateway.events.historyKey", [&]() {
				return ValidateTwoStringFields(payload, "historyKey", "event", issue, "`gateway.events.historyKey` requires `historyKey` string and `event` string.");
			} },
			{ "gateway.events.snapshotKey", [&]() {
				return ValidateTwoStringFields(payload, "snapshotKey", "event", issue, "`gateway.events.snapshotKey` requires `snapshotKey` string and `event` string.");
			} },
			{ "gateway.events.indexKey", [&]() {
				return ValidateTwoStringFields(payload, "indexKey", "event", issue, "`gateway.events.indexKey` requires `indexKey` string and `event` string.");
			} },
			{ "gateway.events.windowScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeKey", "event", issue, "`gateway.events.windowScopeKey` requires `windowScopeKey` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeKey", "event", issue, "`gateway.events.cursorScopeKey` requires `cursorScopeKey` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeKey", "event", issue, "`gateway.events.anchorScopeKey` requires `anchorScopeKey` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeKey", "event", issue, "`gateway.events.offsetScopeKey` requires `offsetScopeKey` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeKey", "event", issue, "`gateway.events.pointerScopeKey` requires `pointerScopeKey` string and `event` string.");
			} },
			{ "gateway.events.markerScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "markerScopeKey", "event", issue, "`gateway.events.markerScopeKey` requires `markerScopeKey` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeKey", "event", issue, "`gateway.events.tokenScopeKey` requires `tokenScopeKey` string and `event` string.");
			} },
			{ "gateway.events.streamScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeKey", "event", issue, "`gateway.events.streamScopeKey` requires `streamScopeKey` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeKey", "event", issue, "`gateway.events.sequenceScopeKey` requires `sequenceScopeKey` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeKey", "event", issue, "`gateway.events.bundleScopeKey` requires `bundleScopeKey` string and `event` string.");
			} },
			{ "gateway.events.packageScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeKey", "event", issue, "`gateway.events.packageScopeKey` requires `packageScopeKey` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeKey", "event", issue, "`gateway.events.archiveScopeKey` requires `archiveScopeKey` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeKey", "event", issue, "`gateway.events.manifestScopeKey` requires `manifestScopeKey` string and `event` string.");
			} },
			{ "gateway.events.profileScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeKey", "event", issue, "`gateway.events.profileScopeKey` requires `profileScopeKey` string and `event` string.");
			} },
			{ "gateway.events.templateScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeKey", "event", issue, "`gateway.events.templateScopeKey` requires `templateScopeKey` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeKey", "event", issue, "`gateway.events.revisionScopeKey` requires `revisionScopeKey` string and `event` string.");
			} },
			{ "gateway.events.historyScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeKey", "event", issue, "`gateway.events.historyScopeKey` requires `historyScopeKey` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeKey", "event", issue, "`gateway.events.snapshotScopeKey` requires `snapshotScopeKey` string and `event` string.");
			} },
			{ "gateway.events.indexScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeKey", "event", issue, "`gateway.events.indexScopeKey` requires `indexScopeKey` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.markerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "markerScopeId", "event", issue, "`gateway.events.markerScopeId` requires `markerScopeId` string and `event` string.");
			} },
			{ "gateway.events.token", [&]() {
				return ValidateTwoStringFields(payload, "token", "event", issue, "`gateway.events.token` requires `token` string and `event` string.");
			} },
			{ "gateway.events.pointer", [&]() {
				return ValidateTwoStringFields(payload, "pointer", "event", issue, "`gateway.events.pointer` requires `pointer` string and `event` string.");
			} },
			{ "gateway.events.marker", [&]() {
				return ValidateTwoStringFields(payload, "marker", "event", issue, "`gateway.events.marker` requires `marker` string and `event` string.");
			} },
			{ "gateway.events.anchor", [&]() {
				return ValidateTwoStringFields(payload, "anchor", "event", issue, "`gateway.events.anchor` requires `anchor` string and `event` string.");
			} },
			{ "gateway.events.cursor", [&]() {
				return ValidateTwoStringFields(payload, "cursor", "event", issue, "`gateway.events.cursor` requires `cursor` string and `event` string.");
			} },
			{ "gateway.transport.policy.refresh", [&]() {
				return ValidateBooleanNumberBoolean(payload, "refreshed", "version", "applied", issue, "`gateway.transport.policy.refresh` requires `refreshed` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.session", [&]() {
				return ValidateBooleanNumberBoolean(payload, "sessionApplied", "version", "applied", issue, "`gateway.transport.policy.session` requires `sessionApplied` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.scope", [&]() {
				return ValidateBooleanNumberBoolean(payload, "scoped", "version", "applied", issue, "`gateway.transport.policy.scope` requires `scoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.context", [&]() {
				return ValidateBooleanNumberBoolean(payload, "contextual", "version", "applied", issue, "`gateway.transport.policy.context` requires `contextual` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.channel", [&]() {
				return ValidateBooleanNumberBoolean(payload, "channelScoped", "version", "applied", issue, "`gateway.transport.policy.channel` requires `channelScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.route", [&]() {
				return ValidateBooleanNumberBoolean(payload, "routed", "version", "applied", issue, "`gateway.transport.policy.route` requires `routed` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.account", [&]() {
				return ValidateBooleanNumberBoolean(payload, "accountScoped", "version", "applied", issue, "`gateway.transport.policy.account` requires `accountScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.agent", [&]() {
				return ValidateBooleanNumberBoolean(payload, "agentScoped", "version", "applied", issue, "`gateway.transport.policy.agent` requires `agentScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.model", [&]() {
				return ValidateBooleanNumberBoolean(payload, "modelScoped", "version", "applied", issue, "`gateway.transport.policy.model` requires `modelScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.config", [&]() {
				return ValidateBooleanNumberBoolean(payload, "configScoped", "version", "applied", issue, "`gateway.transport.policy.config` requires `configScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.policy", [&]() {
				return ValidateBooleanNumberBoolean(payload, "policyScoped", "version", "applied", issue, "`gateway.transport.policy.policy` requires `policyScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.tool", [&]() {
				return ValidateBooleanNumberBoolean(payload, "toolScoped", "version", "applied", issue, "`gateway.transport.policy.tool` requires `toolScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.transport", [&]() {
				return ValidateBooleanNumberBoolean(payload, "transportScoped", "version", "applied", issue, "`gateway.transport.policy.transport` requires `transportScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.runtime", [&]() {
				return ValidateBooleanNumberBoolean(payload, "runtimeScoped", "version", "applied", issue, "`gateway.transport.policy.runtime` requires `runtimeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.stateKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "stateScoped", "version", "applied", issue, "`gateway.transport.policy.stateKey` requires `stateScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.healthKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "healthScoped", "version", "applied", issue, "`gateway.transport.policy.healthKey` requires `healthScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.log", [&]() {
				return ValidateBooleanNumberBoolean(payload, "logScoped", "version", "applied", issue, "`gateway.transport.policy.log` requires `logScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.metric", [&]() {
				return ValidateBooleanNumberBoolean(payload, "metricScoped", "version", "applied", issue, "`gateway.transport.policy.metric` requires `metricScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.trace", [&]() {
				return ValidateBooleanNumberBoolean(payload, "traceScoped", "version", "applied", issue, "`gateway.transport.policy.trace` requires `traceScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.audit", [&]() {
				return ValidateBooleanNumberBoolean(payload, "auditScoped", "version", "applied", issue, "`gateway.transport.policy.audit` requires `auditScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.debug", [&]() {
				return ValidateBooleanNumberBoolean(payload, "debugScoped", "version", "applied", issue, "`gateway.transport.policy.debug` requires `debugScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.cache", [&]() {
				return ValidateBooleanNumberBoolean(payload, "cacheScoped", "version", "applied", issue, "`gateway.transport.policy.cache` requires `cacheScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.queueKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "queueScoped", "version", "applied", issue, "`gateway.transport.policy.queueKey` requires `queueScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.windowKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "windowScoped", "version", "applied", issue, "`gateway.transport.policy.windowKey` requires `windowScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.cursorKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "cursorScoped", "version", "applied", issue, "`gateway.transport.policy.cursorKey` requires `cursorScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.anchorKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "anchorScoped", "version", "applied", issue, "`gateway.transport.policy.anchorKey` requires `anchorScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.offsetKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "offsetScoped", "version", "applied", issue, "`gateway.transport.policy.offsetKey` requires `offsetScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.markerKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "markerScoped", "version", "applied", issue, "`gateway.transport.policy.markerKey` requires `markerScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.pointerKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "pointerScoped", "version", "applied", issue, "`gateway.transport.policy.pointerKey` requires `pointerScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.tokenKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "tokenScoped", "version", "applied", issue, "`gateway.transport.policy.tokenKey` requires `tokenScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.sequenceKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "sequenceScoped", "version", "applied", issue, "`gateway.transport.policy.sequenceKey` requires `sequenceScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.streamKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "streamScoped", "version", "applied", issue, "`gateway.transport.policy.streamKey` requires `streamScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.bundleKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "bundleScoped", "version", "applied", issue, "`gateway.transport.policy.bundleKey` requires `bundleScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.packageKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "packageScoped", "version", "applied", issue, "`gateway.transport.policy.packageKey` requires `packageScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.archiveKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "archiveScoped", "version", "applied", issue, "`gateway.transport.policy.archiveKey` requires `archiveScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.manifestKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "manifestScoped", "version", "applied", issue, "`gateway.transport.policy.manifestKey` requires `manifestScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.profileKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "profileScoped", "version", "applied", issue, "`gateway.transport.policy.profileKey` requires `profileScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.templateKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "templateScoped", "version", "applied", issue, "`gateway.transport.policy.templateKey` requires `templateScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.revisionKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "revisionScoped", "version", "applied", issue, "`gateway.transport.policy.revisionKey` requires `revisionScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.historyKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "historyScoped", "version", "applied", issue, "`gateway.transport.policy.historyKey` requires `historyScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.snapshotKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "snapshotScoped", "version", "applied", issue, "`gateway.transport.policy.snapshotKey` requires `snapshotScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.indexKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "indexScoped", "version", "applied", issue, "`gateway.transport.policy.indexKey` requires `indexScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.windowScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "windowScopeScoped", "version", "applied", issue, "`gateway.transport.policy.windowScopeKey` requires `windowScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.cursorScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "cursorScopeScoped", "version", "applied", issue, "`gateway.transport.policy.cursorScopeKey` requires `cursorScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.anchorScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "anchorScopeScoped", "version", "applied", issue, "`gateway.transport.policy.anchorScopeKey` requires `anchorScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.offsetScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "offsetScopeScoped", "version", "applied", issue, "`gateway.transport.policy.offsetScopeKey` requires `offsetScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.pointerScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "pointerScopeScoped", "version", "applied", issue, "`gateway.transport.policy.pointerScopeKey` requires `pointerScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.markerScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "markerScopeScoped", "version", "applied", issue, "`gateway.transport.policy.markerScopeKey` requires `markerScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.tokenScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "tokenScopeScoped", "version", "applied", issue, "`gateway.transport.policy.tokenScopeKey` requires `tokenScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
            { "gateway.transport.policy.streamScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "streamScopeScoped", "version", "applied", issue, "`gateway.transport.policy.streamScopeKey` requires `streamScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.sequenceScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "sequenceScopeScoped", "version", "applied", issue, "`gateway.transport.policy.sequenceScopeKey` requires `sequenceScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.bundleScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "bundleScopeScoped", "version", "applied", issue, "`gateway.transport.policy.bundleScopeKey` requires `bundleScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.packageScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "packageScopeScoped", "version", "applied", issue, "`gateway.transport.policy.packageScopeKey` requires `packageScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.archiveScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "archiveScopeScoped", "version", "applied", issue, "`gateway.transport.policy.archiveScopeKey` requires `archiveScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.manifestScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "manifestScopeScoped", "version", "applied", issue, "`gateway.transport.policy.manifestScopeKey` requires `manifestScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.profileScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "profileScopeScoped", "version", "applied", issue, "`gateway.transport.policy.profileScopeKey` requires `profileScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.templateScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "templateScopeScoped", "version", "applied", issue, "`gateway.transport.policy.templateScopeKey` requires `templateScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.revisionScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "revisionScopeScoped", "version", "applied", issue, "`gateway.transport.policy.revisionScopeKey` requires `revisionScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.historyScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "historyScopeScoped", "version", "applied", issue, "`gateway.transport.policy.historyScopeKey` requires `historyScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.snapshotScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "snapshotScopeScoped", "version", "applied", issue, "`gateway.transport.policy.snapshotScopeKey` requires `snapshotScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.indexScopeKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "indexScopeScoped", "version", "applied", issue, "`gateway.transport.policy.indexScopeKey` requires `indexScopeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.windowScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "windowScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.windowScopeId` requires `windowScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.cursorScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "cursorScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.cursorScopeId` requires `cursorScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.anchorScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "anchorScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.anchorScopeId` requires `anchorScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.offsetScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "offsetScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.offsetScopeId` requires `offsetScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.pointerScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "pointerScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.pointerScopeId` requires `pointerScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.tokenScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "tokenScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.tokenScopeId` requires `tokenScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.sequenceScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "sequenceScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.sequenceScopeId` requires `sequenceScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.streamScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "streamScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.streamScopeId` requires `streamScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.bundleScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "bundleScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.bundleScopeId` requires `bundleScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.packageScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "packageScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.packageScopeId` requires `packageScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.archiveScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "archiveScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.archiveScopeId` requires `archiveScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.manifestScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "manifestScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.manifestScopeId` requires `manifestScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.profileScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "profileScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.profileScopeId` requires `profileScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.templateScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "templateScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.templateScopeId` requires `templateScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.revisionScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "revisionScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.revisionScopeId` requires `revisionScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.historyScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "historyScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.historyScopeId` requires `historyScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.snapshotScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "snapshotScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.snapshotScopeId` requires `snapshotScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.indexScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "indexScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.indexScopeId` requires `indexScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.markerScopeId", [&]() {
				return ValidateBooleanNumberBoolean(payload, "markerScopeScopedId", "version", "applied", issue, "`gateway.transport.policy.markerScopeId` requires `markerScopeScopedId` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.sync", [&]() {
				return ValidateBooleanNumberBoolean(payload, "synced", "version", "applied", issue, "`gateway.transport.policy.sync` requires `synced` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.reconcile", [&]() {
				return ValidateBooleanNumberBoolean(payload, "reconciled", "version", "applied", issue, "`gateway.transport.policy.reconcile` requires `reconciled` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.stage", [&]() {
				return ValidateBooleanNumberBoolean(payload, "staged", "version", "applied", issue, "`gateway.transport.policy.stage` requires `staged` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.commit", [&]() {
				return ValidateBooleanNumberBoolean(payload, "committed", "version", "applied", issue, "`gateway.transport.policy.commit` requires `committed` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.import", [&]() {
				return ValidateBooleanNumberBoolean(payload, "imported", "version", "applied", issue, "`gateway.transport.policy.import` requires `imported` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.models.state", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.state` requires `models` array and `count` number.");
			} },
			{ "gateway.models.session", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.session` requires `models` array and `count` number.");
			} },
			{ "gateway.models.scope", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.scope` requires `models` array and `count` number.");
			} },
			{ "gateway.models.context", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.context` requires `models` array and `count` number.");
			} },
			{ "gateway.models.channel", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.channel` requires `models` array and `count` number.");
			} },
			{ "gateway.models.route", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.route` requires `models` array and `count` number.");
			} },
			{ "gateway.models.account", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.account` requires `models` array and `count` number.");
			} },
			{ "gateway.models.agent", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.agent` requires `models` array and `count` number.");
			} },
			{ "gateway.models.model", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.model` requires `models` array and `count` number.");
			} },
			{ "gateway.models.config", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.config` requires `models` array and `count` number.");
			} },
			{ "gateway.models.policy", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.policy` requires `models` array and `count` number.");
			} },
			{ "gateway.models.tool", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.tool` requires `models` array and `count` number.");
			} },
			{ "gateway.models.transport", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.transport` requires `models` array and `count` number.");
			} },
			{ "gateway.models.runtime", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.runtime` requires `models` array and `count` number.");
			} },
			{ "gateway.models.stateKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.stateKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.healthKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.healthKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.log", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.log` requires `models` array and `count` number.");
			} },
			{ "gateway.models.metric", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.metric` requires `models` array and `count` number.");
			} },
			{ "gateway.models.trace", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.trace` requires `models` array and `count` number.");
			} },
			{ "gateway.models.audit", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.audit` requires `models` array and `count` number.");
			} },
			{ "gateway.models.debug", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.debug` requires `models` array and `count` number.");
			} },
			{ "gateway.models.cache", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.cache` requires `models` array and `count` number.");
			} },
			{ "gateway.models.queueKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.queueKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.windowKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.windowKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.cursorKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.cursorKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.anchorKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.anchorKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.offsetKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.offsetKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.markerKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.markerKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.pointerKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.pointerKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.tokenKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.tokenKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.sequenceKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.sequenceKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.streamKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.streamKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.bundleKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.bundleKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.packageKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.packageKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.archiveKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.archiveKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.manifestKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.manifestKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.profileKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.profileKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.templateKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.templateKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.revisionKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.revisionKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.historyKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.historyKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.snapshotKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.snapshotKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.indexKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.indexKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.windowScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.windowScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.cursorScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.cursorScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.anchorScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.anchorScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.offsetScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.offsetScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.pointerScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.pointerScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.markerScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.markerScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.tokenScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.tokenScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.streamScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.streamScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.sequenceScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.sequenceScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.bundleScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.bundleScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.packageScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.packageScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.archiveScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.archiveScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.manifestScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.manifestScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.profileScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.profileScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.templateScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.templateScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.revisionScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.revisionScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.historyScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.historyScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.snapshotScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.snapshotScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.indexScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.indexScopeKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.windowScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.windowScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.cursorScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.cursorScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.anchorScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.anchorScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.offsetScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.offsetScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.pointerScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.pointerScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.tokenScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.tokenScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.sequenceScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.sequenceScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.streamScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.streamScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.bundleScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.bundleScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.packageScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.packageScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.archiveScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.archiveScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.manifestScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.manifestScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.profileScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.profileScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.templateScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.templateScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.revisionScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.revisionScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.historyScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.historyScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.snapshotScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.snapshotScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.indexScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.indexScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.markerScopeId", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.markerScopeId` requires `models` array and `count` number.");
			} },
			{ "gateway.models.index", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.index` requires `models` array and `count` number.");
			} },
			{ "gateway.models.registry", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.registry` requires `models` array and `count` number.");
			} },
			{ "gateway.models.snapshot", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.snapshot` requires `models` array and `count` number.");
			} },
			{ "gateway.models.inventory", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.inventory` requires `models` array and `count` number.");
			} },
			{ "gateway.models.catalog", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.catalog` requires `models` array and `count` number.");
			} },
			{ "gateway.events.recent", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.recent` requires `events` array and `count` number.");
			} },
			{ "gateway.events.window", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.window` requires `events` array and `count` number.");
			} },
			{ "gateway.events.sample", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.sample` requires `events` array and `count` number.");
			} },
			{ "gateway.events.timeline", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.timeline` requires `events` array and `count` number.");
			} },
			{ "gateway.config.state", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.state` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.session", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.session` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.scope", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.scope` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.context", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.context` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.channel", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.channel` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.route", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.route` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.account", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.account` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.agent", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.agent` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.model", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.model` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.config", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.config` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.policy", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.policy` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.tool", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.tool` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.transport", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.transport` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.runtime", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.runtime` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.stateKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.stateKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.healthKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.healthKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.log", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.log` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.metric", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.metric` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.trace", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.trace` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.auditKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.auditKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.debug", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.debug` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.cache", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.cache` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.queueKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.queueKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.windowKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.windowKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.cursorKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.cursorKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.anchorKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.anchorKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.offsetKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.offsetKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.markerKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.markerKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.pointerKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.pointerKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.tokenKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.tokenKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.sequenceKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.sequenceKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.streamKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.streamKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.bundleKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.bundleKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.packageKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.packageKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.archiveKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.archiveKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.manifestKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.manifestKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.profileKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.profileKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.templateKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.templateKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.revisionKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.revisionKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.historyKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.historyKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.snapshotKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.snapshotKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.indexKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.indexKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.windowScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.windowScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.cursorScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.cursorScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.anchorScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.anchorScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.offsetScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.offsetScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.pointerScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.pointerScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.markerScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.markerScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.tokenScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.tokenScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.streamScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.streamScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.sequenceScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.sequenceScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.bundleScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.bundleScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.packageScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.packageScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.archiveScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.archiveScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.manifestScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.manifestScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.profileScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.profileScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.templateScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.templateScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.revisionScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.revisionScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.historyScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.historyScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.snapshotScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.snapshotScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.indexScopeKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.indexScopeKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.windowScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.windowScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.cursorScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.cursorScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.anchorScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.anchorScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.offsetScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.offsetScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.pointerScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.pointerScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.tokenScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.tokenScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.sequenceScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.sequenceScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.streamScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.streamScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.bundleScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.bundleScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.packageScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.packageScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.archiveScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.archiveScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.manifestScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.manifestScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.profileScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.profileScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.templateScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.templateScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.revisionScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.revisionScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.historyScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.historyScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.snapshotScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.snapshotScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.indexScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.indexScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.markerScopeId", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.markerScopeId` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.index", [&]() {
				return ValidateArrayAndCount(payload, "keys", issue, "`gateway.config.index` requires `keys` array and `count` number.");
			} },
			{ "gateway.tools.selector", [&]() {
				return ValidateNumericFields(payload, { "selected", "fallback", "tools" }, issue, "`gateway.tools.selector` requires numeric fields `selected`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.mapper", [&]() {
				return ValidateNumericFields(payload, { "mapped", "fallback", "tools" }, issue, "`gateway.tools.mapper` requires numeric fields `mapped`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.binding", [&]() {
				return ValidateNumericFields(payload, { "bound", "fallback", "tools" }, issue, "`gateway.tools.binding` requires numeric fields `bound`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.profile", [&]() {
				return ValidateNumericFields(payload, { "profiled", "fallback", "tools" }, issue, "`gateway.tools.profile` requires numeric fields `profiled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.channel", [&]() {
				return ValidateNumericFields(payload, { "channelled", "fallback", "tools" }, issue, "`gateway.tools.channel` requires numeric fields `channelled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.route", [&]() {
				return ValidateNumericFields(payload, { "routed", "fallback", "tools" }, issue, "`gateway.tools.route` requires numeric fields `routed`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.account", [&]() {
				return ValidateNumericFields(payload, { "accounted", "fallback", "tools" }, issue, "`gateway.tools.account` requires numeric fields `accounted`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.agent", [&]() {
				return ValidateNumericFields(payload, { "agented", "fallback", "tools" }, issue, "`gateway.tools.agent` requires numeric fields `agented`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.model", [&]() {
				return ValidateNumericFields(payload, { "modelled", "fallback", "tools" }, issue, "`gateway.tools.model` requires numeric fields `modelled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.config", [&]() {
				return ValidateNumericFields(payload, { "configured", "fallback", "tools" }, issue, "`gateway.tools.config` requires numeric fields `configured`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.policy", [&]() {
				return ValidateNumericFields(payload, { "policied", "fallback", "tools" }, issue, "`gateway.tools.policy` requires numeric fields `policied`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.tool", [&]() {
				return ValidateNumericFields(payload, { "tooled", "fallback", "tools" }, issue, "`gateway.tools.tool` requires numeric fields `tooled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.transport", [&]() {
				return ValidateNumericFields(payload, { "transported", "fallback", "tools" }, issue, "`gateway.tools.transport` requires numeric fields `transported`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.runtime", [&]() {
				return ValidateNumericFields(payload, { "runtimed", "fallback", "tools" }, issue, "`gateway.tools.runtime` requires numeric fields `runtimed`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.state", [&]() {
				return ValidateNumericFields(payload, { "stated", "fallback", "tools" }, issue, "`gateway.tools.state` requires numeric fields `stated`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.healthKey", [&]() {
				return ValidateNumericFields(payload, { "healthChecked", "fallback", "tools" }, issue, "`gateway.tools.healthKey` requires numeric fields `healthChecked`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.log", [&]() {
				return ValidateNumericFields(payload, { "logged", "fallback", "tools" }, issue, "`gateway.tools.log` requires numeric fields `logged`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.metric", [&]() {
				return ValidateNumericFields(payload, { "metered", "fallback", "tools" }, issue, "`gateway.tools.metric` requires numeric fields `metered`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.trace", [&]() {
				return ValidateNumericFields(payload, { "traced", "fallback", "tools" }, issue, "`gateway.tools.trace` requires numeric fields `traced`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.audit", [&]() {
				return ValidateNumericFields(payload, { "audited", "fallback", "tools" }, issue, "`gateway.tools.audit` requires numeric fields `audited`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.debug", [&]() {
				return ValidateNumericFields(payload, { "debugged", "fallback", "tools" }, issue, "`gateway.tools.debug` requires numeric fields `debugged`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.cache", [&]() {
				return ValidateNumericFields(payload, { "cached", "fallback", "tools" }, issue, "`gateway.tools.cache` requires numeric fields `cached`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.queueKey", [&]() {
				return ValidateNumericFields(payload, { "queuedKey", "fallback", "tools" }, issue, "`gateway.tools.queueKey` requires numeric fields `queuedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.windowKey", [&]() {
				return ValidateNumericFields(payload, { "windowedKey", "fallback", "tools" }, issue, "`gateway.tools.windowKey` requires numeric fields `windowedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.cursorKey", [&]() {
				return ValidateNumericFields(payload, { "cursoredKey", "fallback", "tools" }, issue, "`gateway.tools.cursorKey` requires numeric fields `cursoredKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.anchorKey", [&]() {
				return ValidateNumericFields(payload, { "anchoredKey", "fallback", "tools" }, issue, "`gateway.tools.anchorKey` requires numeric fields `anchoredKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.offsetKey", [&]() {
				return ValidateNumericFields(payload, { "offsettedKey", "fallback", "tools" }, issue, "`gateway.tools.offsetKey` requires numeric fields `offsettedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.markerKey", [&]() {
				return ValidateNumericFields(payload, { "markeredKey", "fallback", "tools" }, issue, "`gateway.tools.markerKey` requires numeric fields `markeredKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.pointerKey", [&]() {
				return ValidateNumericFields(payload, { "pointedKey", "fallback", "tools" }, issue, "`gateway.tools.pointerKey` requires numeric fields `pointedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.tokenKey", [&]() {
				return ValidateNumericFields(payload, { "tokenedKey", "fallback", "tools" }, issue, "`gateway.tools.tokenKey` requires numeric fields `tokenedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.sequenceKey", [&]() {
				return ValidateNumericFields(payload, { "sequencedKey", "fallback", "tools" }, issue, "`gateway.tools.sequenceKey` requires numeric fields `sequencedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.streamKey", [&]() {
				return ValidateNumericFields(payload, { "streamedKey", "fallback", "tools" }, issue, "`gateway.tools.streamKey` requires numeric fields `streamedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.bundleKey", [&]() {
				return ValidateNumericFields(payload, { "bundledKey", "fallback", "tools" }, issue, "`gateway.tools.bundleKey` requires numeric fields `bundledKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.packageKey", [&]() {
				return ValidateNumericFields(payload, { "packagedKey", "fallback", "tools" }, issue, "`gateway.tools.packageKey` requires numeric fields `packagedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.archiveKey", [&]() {
				return ValidateNumericFields(payload, { "archivedKey", "fallback", "tools" }, issue, "`gateway.tools.archiveKey` requires numeric fields `archivedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.manifestKey", [&]() {
				return ValidateNumericFields(payload, { "manifestedKey", "fallback", "tools" }, issue, "`gateway.tools.manifestKey` requires numeric fields `manifestedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.profileKey", [&]() {
				return ValidateNumericFields(payload, { "profiledKey", "fallback", "tools" }, issue, "`gateway.tools.profileKey` requires numeric fields `profiledKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.templateKey", [&]() {
				return ValidateNumericFields(payload, { "templatedKey", "fallback", "tools" }, issue, "`gateway.tools.templateKey` requires numeric fields `templatedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.revisionKey", [&]() {
				return ValidateNumericFields(payload, { "revisionedKey", "fallback", "tools" }, issue, "`gateway.tools.revisionKey` requires numeric fields `revisionedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.historyKey", [&]() {
				return ValidateNumericFields(payload, { "historiedKey", "fallback", "tools" }, issue, "`gateway.tools.historyKey` requires numeric fields `historiedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.snapshotKey", [&]() {
				return ValidateNumericFields(payload, { "snapshottedKey", "fallback", "tools" }, issue, "`gateway.tools.snapshotKey` requires numeric fields `snapshottedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.indexKey", [&]() {
				return ValidateNumericFields(payload, { "indexedKey", "fallback", "tools" }, issue, "`gateway.tools.indexKey` requires numeric fields `indexedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.windowScopeKey", [&]() {
				return ValidateNumericFields(payload, { "windowScopedKey", "fallback", "tools" }, issue, "`gateway.tools.windowScopeKey` requires numeric fields `windowScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.cursorScopeKey", [&]() {
				return ValidateNumericFields(payload, { "cursorScopedKey", "fallback", "tools" }, issue, "`gateway.tools.cursorScopeKey` requires numeric fields `cursorScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.anchorScopeKey", [&]() {
				return ValidateNumericFields(payload, { "anchorScopedKey", "fallback", "tools" }, issue, "`gateway.tools.anchorScopeKey` requires numeric fields `anchorScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.offsetScopeKey", [&]() {
				return ValidateNumericFields(payload, { "offsetScopedKey", "fallback", "tools" }, issue, "`gateway.tools.offsetScopeKey` requires numeric fields `offsetScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.pointerScopeKey", [&]() {
				return ValidateNumericFields(payload, { "pointerScopedKey", "fallback", "tools" }, issue, "`gateway.tools.pointerScopeKey` requires numeric fields `pointerScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.markerScopeKey", [&]() {
				return ValidateNumericFields(payload, { "markerScopedKey", "fallback", "tools" }, issue, "`gateway.tools.markerScopeKey` requires numeric fields `markerScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.tokenScopeKey", [&]() {
				return ValidateNumericFields(payload, { "tokenScopedKey", "fallback", "tools" }, issue, "`gateway.tools.tokenScopeKey` requires numeric fields `tokenScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.streamScopeKey", [&]() {
				return ValidateNumericFields(payload, { "streamScopedKey", "fallback", "tools" }, issue, "`gateway.tools.streamScopeKey` requires numeric fields `streamScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.sequenceScopeKey", [&]() {
				return ValidateNumericFields(payload, { "sequenceScopedKey", "fallback", "tools" }, issue, "`gateway.tools.sequenceScopeKey` requires numeric fields `sequenceScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.bundleScopeKey", [&]() {
				return ValidateNumericFields(payload, { "bundleScopedKey", "fallback", "tools" }, issue, "`gateway.tools.bundleScopeKey` requires numeric fields `bundleScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.packageScopeKey", [&]() {
				return ValidateNumericFields(payload, { "packageScopedKey", "fallback", "tools" }, issue, "`gateway.tools.packageScopeKey` requires numeric fields `packageScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.archiveScopeKey", [&]() {
				return ValidateNumericFields(payload, { "archiveScopedKey", "fallback", "tools" }, issue, "`gateway.tools.archiveScopeKey` requires numeric fields `archiveScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.manifestScopeKey", [&]() {
				return ValidateNumericFields(payload, { "manifestScopedKey", "fallback", "tools" }, issue, "`gateway.tools.manifestScopeKey` requires numeric fields `manifestScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.profileScopeKey", [&]() {
				return ValidateNumericFields(payload, { "profileScopedKey", "fallback", "tools" }, issue, "`gateway.tools.profileScopeKey` requires numeric fields `profileScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.templateScopeKey", [&]() {
				return ValidateNumericFields(payload, { "templateScopedKey", "fallback", "tools" }, issue, "`gateway.tools.templateScopeKey` requires numeric fields `templateScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.revisionScopeKey", [&]() {
				return ValidateNumericFields(payload, { "revisionScopedKey", "fallback", "tools" }, issue, "`gateway.tools.revisionScopeKey` requires numeric fields `revisionScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.historyScopeKey", [&]() {
				return ValidateNumericFields(payload, { "historyScopedKey", "fallback", "tools" }, issue, "`gateway.tools.historyScopeKey` requires numeric fields `historyScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.snapshotScopeKey", [&]() {
				return ValidateNumericFields(payload, { "snapshotScopedKey", "fallback", "tools" }, issue, "`gateway.tools.snapshotScopeKey` requires numeric fields `snapshotScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.indexScopeKey", [&]() {
				return ValidateNumericFields(payload, { "indexScopedKey", "fallback", "tools" }, issue, "`gateway.tools.indexScopeKey` requires numeric fields `indexScopedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.windowScopeId", [&]() {
				return ValidateNumericFields(payload, { "windowScopedId", "fallback", "tools" }, issue, "`gateway.tools.windowScopeId` requires numeric fields `windowScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.cursorScopeId", [&]() {
				return ValidateNumericFields(payload, { "cursorScopedId", "fallback", "tools" }, issue, "`gateway.tools.cursorScopeId` requires numeric fields `cursorScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.anchorScopeId", [&]() {
				return ValidateNumericFields(payload, { "anchorScopedId", "fallback", "tools" }, issue, "`gateway.tools.anchorScopeId` requires numeric fields `anchorScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.offsetScopeId", [&]() {
				return ValidateNumericFields(payload, { "offsetScopedId", "fallback", "tools" }, issue, "`gateway.tools.offsetScopeId` requires numeric fields `offsetScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.pointerScopeId", [&]() {
				return ValidateNumericFields(payload, { "pointerScopedId", "fallback", "tools" }, issue, "`gateway.tools.pointerScopeId` requires numeric fields `pointerScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.tokenScopeId", [&]() {
				return ValidateNumericFields(payload, { "tokenScopedId", "fallback", "tools" }, issue, "`gateway.tools.tokenScopeId` requires numeric fields `tokenScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.sequenceScopeId", [&]() {
				return ValidateNumericFields(payload, { "sequenceScopedId", "fallback", "tools" }, issue, "`gateway.tools.sequenceScopeId` requires numeric fields `sequenceScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.streamScopeId", [&]() {
				return ValidateNumericFields(payload, { "streamScopedId", "fallback", "tools" }, issue, "`gateway.tools.streamScopeId` requires numeric fields `streamScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.bundleScopeId", [&]() {
				return ValidateNumericFields(payload, { "bundleScopedId", "fallback", "tools" }, issue, "`gateway.tools.bundleScopeId` requires numeric fields `bundleScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.packageScopeId", [&]() {
				return ValidateNumericFields(payload, { "packageScopedId", "fallback", "tools" }, issue, "`gateway.tools.packageScopeId` requires numeric fields `packageScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.archiveScopeId", [&]() {
				return ValidateNumericFields(payload, { "archiveScopedId", "fallback", "tools" }, issue, "`gateway.tools.archiveScopeId` requires numeric fields `archiveScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.manifestScopeId", [&]() {
				return ValidateNumericFields(payload, { "manifestScopedId", "fallback", "tools" }, issue, "`gateway.tools.manifestScopeId` requires numeric fields `manifestScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.profileScopeId", [&]() {
				return ValidateNumericFields(payload, { "profileScopedId", "fallback", "tools" }, issue, "`gateway.tools.profileScopeId` requires numeric fields `profileScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.templateScopeId", [&]() {
				return ValidateNumericFields(payload, { "templateScopedId", "fallback", "tools" }, issue, "`gateway.tools.templateScopeId` requires numeric fields `templateScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.revisionScopeId", [&]() {
				return ValidateNumericFields(payload, { "revisionScopedId", "fallback", "tools" }, issue, "`gateway.tools.revisionScopeId` requires numeric fields `revisionScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.historyScopeId", [&]() {
				return ValidateNumericFields(payload, { "historyScopedId", "fallback", "tools" }, issue, "`gateway.tools.historyScopeId` requires numeric fields `historyScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.snapshotScopeId", [&]() {
				return ValidateNumericFields(payload, { "snapshotScopedId", "fallback", "tools" }, issue, "`gateway.tools.snapshotScopeId` requires numeric fields `snapshotScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.indexScopeId", [&]() {
				return ValidateNumericFields(payload, { "indexScopedId", "fallback", "tools" }, issue, "`gateway.tools.indexScopeId` requires numeric fields `indexScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.markerScopeId", [&]() {
				return ValidateNumericFields(payload, { "markerScopedId", "fallback", "tools" }, issue, "`gateway.tools.markerScopeId` requires numeric fields `markerScopedId`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.router", [&]() {
				return ValidateNumericFields(payload, { "routed", "fallback", "tools" }, issue, "`gateway.tools.router` requires numeric fields `routed`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.dispatch", [&]() {
				return ValidateNumericFields(payload, { "queued", "dispatched", "tools" }, issue, "`gateway.tools.dispatch` requires numeric fields `queued`, `dispatched`, and `tools`.");
			} },
			{ "gateway.tools.window", [&]() {
				return ValidateNumericFields(payload, { "windowSec", "calls", "tools" }, issue, "`gateway.tools.window` requires numeric fields `windowSec`, `calls`, and `tools`.");
			} },
			{ "gateway.tools.backlog", [&]() {
				return ValidateNumericFields(payload, { "pending", "capacity", "tools" }, issue, "`gateway.tools.backlog` requires numeric fields `pending`, `capacity`, and `tools`.");
			} },
			{ "gateway.tools.scheduler", [&]() {
				return ValidateNumericFields(payload, { "ticks", "queued", "tools" }, issue, "`gateway.tools.scheduler` requires numeric fields `ticks`, `queued`, and `tools`.");
			} },
			{ "gateway.tools.queue", [&]() {
				return ValidateNumericFields(payload, { "queued", "running", "tools" }, issue, "`gateway.tools.queue` requires numeric fields `queued`, `running`, and `tools`.");
			} },
			{ "gateway.tools.capacity", [&]() {
				return ValidateNumericFields(payload, { "total", "used", "free" }, issue, "`gateway.tools.capacity` requires numeric fields `total`, `used`, and `free`.");
			} },
			{ "gateway.tools.errors", [&]() {
				return ValidateNumericFields(payload, { "errors", "tools", "rate" }, issue, "`gateway.tools.errors` requires numeric fields `errors`, `tools`, and `rate`.");
			} },
			{ "gateway.tools.usage", [&]() {
				return ValidateNumericFields(payload, { "calls", "tools", "avgMs" }, issue, "`gateway.tools.usage` requires numeric fields `calls`, `tools`, and `avgMs`.");
			} },
			{ "gateway.tools.failures", [&]() {
				return ValidateNumericFields(payload, { "failed", "total", "rate" }, issue, "`gateway.tools.failures` requires numeric fields `failed`, `total`, and `rate`.");
			} },
			{ "gateway.tools.stats", [&]() {
				return ValidateNumericFields(payload, { "enabled", "disabled", "total" }, issue, "`gateway.tools.stats` requires numeric fields `enabled`, `disabled`, and `total`.");
			} },
			{ "gateway.config.manifest", [&]() {
				return ValidateStringArrayAndCount(payload, "manifest", "sections", issue, "`gateway.config.manifest` requires `manifest` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.archive", [&]() {
				return ValidateStringArrayAndCount(payload, "archive", "sections", issue, "`gateway.config.archive` requires `archive` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.package", [&]() {
				return ValidateStringArrayAndCount(payload, "package", "sections", issue, "`gateway.config.package` requires `package` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.bundle", [&]() {
				return ValidateStringArrayAndCount(payload, "name", "sections", issue, "`gateway.config.bundle` requires `name` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.template", [&]() {
				return ValidateStringArrayAndCount(payload, "template", "keys", issue, "`gateway.config.template` requires `template` string, `keys` array, and `count` number.");
			} },
			{ "gateway.channels.accounts.clear", [&]() {
				return ValidateNumericFields(payload, { "cleared", "remaining" }, issue, "`gateway.channels.accounts.clear` requires numeric fields `cleared` and `remaining`.");
			} },
			{ "gateway.channels.routes.clear", [&]() {
				return ValidateNumericFields(payload, { "cleared", "remaining" }, issue, "`gateway.channels.routes.clear` requires numeric fields `cleared` and `remaining`.");
			} },
			{ "gateway.channels.accounts.restore", [&]() {
				return ValidateNumericFields(payload, { "restored", "total" }, issue, "`gateway.channels.accounts.restore` requires numeric fields `restored` and `total`.");
			} },
			{ "gateway.channels.routes.restore", [&]() {
				return ValidateNumericFields(payload, { "restored", "total" }, issue, "`gateway.channels.routes.restore` requires numeric fields `restored` and `total`.");
			} },
			{ "gateway.channels.accounts.reset", [&]() {
				return ValidateNumericFields(payload, { "cleared", "restored", "total" }, issue, "`gateway.channels.accounts.reset` requires numeric fields `cleared`, `restored`, and `total`.");
			} },
			{ "gateway.channels.routes.reset", [&]() {
				return ValidateNumericFields(payload, { "cleared", "restored", "total" }, issue, "`gateway.channels.routes.reset` requires numeric fields `cleared`, `restored`, and `total`.");
			} },
			{ "gateway.channels.accounts.count", [&]() {
				return ValidateNumberAndString(payload, "count", "channel", issue, "`gateway.channels.accounts.count` requires `channel` string and `count` number.");
			} },
			{ "gateway.channels.routes.count", [&]() {
				return ValidateNumberAndString(payload, "count", "channel", issue, "`gateway.channels.routes.count` requires `channel` string and `count` number.");
			} },
			{ "gateway.channels.status.count", [&]() {
				return ValidateNumberAndString(payload, "count", "channel", issue, "`gateway.channels.status.count` requires `channel` string and `count` number.");
			} },
			{ "gateway.channels.status.exists", [&]() {
				if (!IsFieldValueType(payload, "channel", '"') || !IsFieldBoolean(payload, "exists")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.status.exists` requires `channel` string and `exists` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.accounts.exists", [&]() {
				if (!IsFieldValueType(payload, "channel", '"') || !IsFieldValueType(payload, "accountId", '"') || !IsFieldBoolean(payload, "exists")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.exists` requires `channel` string, `accountId` string, and `exists` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.route.exists", [&]() {
				if (!IsFieldValueType(payload, "channel", '"') || !IsFieldValueType(payload, "accountId", '"') || !IsFieldBoolean(payload, "exists")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.exists` requires `channel` string, `accountId` string, and `exists` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.get", [&]() {
				return ValidateObjectWithTokens(payload, "model", kModelFieldTokens, issue, "`gateway.models.get` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
			} },
			{ "gateway.models.default.get", [&]() {
				return ValidateObjectWithTokens(payload, "model", kModelFieldTokens, issue, "`gateway.models.default.get` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
			} },
			{ "gateway.models.recommended", [&]() {
				if (!IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.recommended` requires `model` object and `reason` string.");
					return false;
				}
				return ValidateObjectWithTokens(payload, "model", kModelFieldTokens, issue, "`gateway.models.recommended` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
			} },
			{ "gateway.events.sequence", [&]() {
				return ValidateNumberAndString(payload, "sequence", "event", issue, "`gateway.events.sequence` requires `sequence` number and `event` string.");
			} },
			{ "gateway.events.offset", [&]() {
				return ValidateNumberAndString(payload, "offset", "event", issue, "`gateway.events.offset` requires `offset` number and `event` string.");
			} },
			{ "gateway.events.latestByType", [&]() {
				return ValidateTwoStringFields(payload, "type", "event", issue, "`gateway.events.latestByType` requires `type` string and `event` string.");
			} },
			{ "gateway.events.last", [&]() {
				if (!IsFieldValueType(payload, "event", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.last` requires `event` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.get", [&]() {
				if (!IsFieldValueType(payload, "event", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.get` requires `event` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.catalog", [&]() {
				return ValidateArrayWithOptionalEntryTokens(payload, "tools", kToolFieldTokens, issue, "`gateway.tools.catalog` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
			} },
			{ "gateway.tools.list", [&]() {
				return ValidateArrayWithOptionalEntryTokens(payload, "tools", kToolFieldTokens, issue, "`gateway.tools.list` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
			} },
			{ "gateway.tools.get", [&]() {
				return ValidateObjectWithTokens(payload, "tool", kToolFieldTokens, issue, "`gateway.tools.get` requires tool fields `id`, `label`, `category`, and `enabled`.");
			} },
			{ "gateway.agents.files.list", [&]() {
				if (!IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.list` requires `files` array and `count` number fields.");
					return false;
				}
				return ValidateArrayWithOptionalEntryTokens(payload, "files", kFileListFieldTokens, issue, "`gateway.agents.files.list` requires file entries with `path`, `size`, and `updatedMs` fields.");
			} },
			{ "gateway.agents.files.get", [&]() {
				return ValidateObjectWithTokens(payload, "file", kFileFieldTokens, issue, "`gateway.agents.files.get` requires `file` fields `path`, `size`, `updatedMs`, and `content`.");
			} },
			{ "gateway.agents.files.set", [&]() {
				return ValidateObjectWithBooleanAndTokens(payload, "file", "saved", kFileFieldTokens, issue, "`gateway.agents.files.set` requires `file` object and `saved` boolean.");
			} },
			{ "gateway.agents.files.delete", [&]() {
				return ValidateObjectWithBooleanAndTokens(payload, "file", "deleted", kFileFieldTokens, issue, "`gateway.agents.files.delete` requires `file` object and `deleted` boolean.");
			} },
			{ "gateway.transport.endpoint.get", [&]() {
				if (!IsFieldValueType(payload, "endpoint", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.endpoint.get` requires `endpoint` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.endpoint.exists", [&]() {
				return ValidateStringAndBoolean(payload, "endpoint", "exists", issue, "`gateway.transport.endpoint.exists` requires `endpoint` string and `exists` boolean.");
			} },
			{ "gateway.transport.endpoint.set", [&]() {
				return ValidateStringAndBoolean(payload, "endpoint", "updated", issue, "`gateway.transport.endpoint.set` requires `endpoint` string and `updated` boolean.");
			} },
			{ "gateway.transport.endpoints.list", [&]() {
				return ValidateArrayAndCount(payload, "endpoints", issue, "`gateway.transport.endpoints.list` requires `endpoints` array and `count` number.");
			} },
			{ "gateway.config.validate", [&]() {
				return ValidateBooleanArrayAndCount(payload, "valid", "errors", issue, "`gateway.config.validate` requires `valid` boolean, `errors` array, and `count` number.");
			} },
			{ "gateway.transport.policy.validate", [&]() {
				return ValidateBooleanArrayAndCount(payload, "valid", "errors", issue, "`gateway.transport.policy.validate` requires `valid` boolean, `errors` array, and `count` number.");
			} },
			{ "gateway.config.history", [&]() {
				return ValidateArrayAndCount(payload, "revisions", issue, "`gateway.config.history` requires `revisions` array and `count` number.");
			} },
			{ "gateway.config.diff", [&]() {
				return ValidateArrayAndCount(payload, "changed", issue, "`gateway.config.diff` requires `changed` array and `count` number.");
			} },
			{ "gateway.transport.policy.apply", [&]() {
				if (!IsFieldBoolean(payload, "applied") || !IsFieldNumber(payload, "version") || !IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.apply` requires `applied` boolean, `version` number, and `reason` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.preview", [&]() {
				if (!IsFieldValueType(payload, "path", '"') || !IsFieldBoolean(payload, "applied") || !IsFieldValueType(payload, "notes", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.preview` requires `path` string, `applied` boolean, and `notes` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.digest", [&]() {
				if (!IsFieldValueType(payload, "digest", '"') || !IsFieldNumber(payload, "version")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.digest` requires `digest` string and `version` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.export", [&]() {
				if (!IsFieldValueType(payload, "path", '"') || !IsFieldNumber(payload, "version") || !IsFieldBoolean(payload, "exported")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.export` requires `path` string, `version` number, and `exported` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.set", [&]() {
				return ValidateBooleanAndString(payload, "applied", "reason", issue, "`gateway.transport.policy.set` requires `applied` boolean and `reason` string.");
			} },
			{ "gateway.transport.policy.reset", [&]() {
				if (!IsFieldBoolean(payload, "reset") || !IsFieldBoolean(payload, "applied")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.reset` requires `reset` boolean and `applied` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.status", [&]() {
				if (!IsFieldBoolean(payload, "mutable") || !IsFieldValueType(payload, "lastApplied", '"') || !IsFieldNumber(payload, "policyVersion")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.status` requires `mutable` boolean, `lastApplied` string, and `policyVersion` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.pipeline", [&]() {
				return ValidateNumericFields(payload, { "queued", "running", "failed", "tools" }, issue, "`gateway.tools.pipeline` requires numeric fields `queued`, `running`, `failed`, and `tools`.");
			} },
			{ "gateway.tools.throughput", [&]() {
				return ValidateNumericFields(payload, { "calls", "windowSec", "perMinute", "tools" }, issue, "`gateway.tools.throughput` requires numeric fields `calls`, `windowSec`, `perMinute`, and `tools`.");
			} },
			{ "gateway.tools.metrics", [&]() {
				return ValidateNumericFields(payload, { "invocations", "enabled", "disabled", "total" }, issue, "`gateway.tools.metrics` requires numeric fields `invocations`, `enabled`, `disabled`, and `total`.");
			} },
			{ "gateway.tools.health", [&]() {
				if (!IsFieldBoolean(payload, "healthy") || !IsFieldNumber(payload, "enabled") || !IsFieldNumber(payload, "total")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.tools.health` requires `healthy` boolean plus numeric `enabled` and `total` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.latency", [&]() {
				return ValidateNumericFields(payload, { "minMs", "maxMs", "avgMs", "samples" }, issue, "`gateway.tools.latency` requires numeric fields `minMs`, `maxMs`, `avgMs`, and `samples`.");
			} },
			{ "gateway.models.manifest", [&]() {
				if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "manifestVersion")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.manifest` requires `models` array and `manifestVersion` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.pool", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.pool` requires `models` array and `count` number.");
			} },
			{ "gateway.models.affinity", [&]() {
				if (!IsFieldValueType(payload, "models", '[') || !IsFieldValueType(payload, "affinity", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.affinity` requires `models` array and `affinity` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.preference", [&]() {
				if (!IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "provider", '"') || !IsFieldValueType(payload, "source", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.preference` requires `model` string, `provider` string, and `source` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.routing", [&]() {
				if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "fallback", '"') || !IsFieldValueType(payload, "strategy", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.routing` requires `primary` string, `fallback` string, and `strategy` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.selection", [&]() {
				return ValidateTwoStringFields(payload, "selected", "strategy", issue, "`gateway.models.selection` requires `selected` string and `strategy` string.");
			} },
			{ "gateway.models.fallback", [&]() {
				if (!IsFieldValueType(payload, "preferred", '"') || !IsFieldValueType(payload, "fallback", '"') || !IsFieldBoolean(payload, "configured")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.fallback` requires `preferred` string, `fallback` string, and `configured` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.profile", [&]() {
				return ValidateTwoStringFields(payload, "name", "source", issue, "`gateway.config.profile` requires `name` string and `source` string.");
			} },
			{ "gateway.config.revision", [&]() {
				return ValidateNumberAndString(payload, "revision", "source", issue, "`gateway.config.revision` requires `revision` number and `source` string.");
			} },
			{ "gateway.config.backup", [&]() {
				if (!IsFieldBoolean(payload, "saved") || !IsFieldNumber(payload, "version") || !IsFieldValueType(payload, "path", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.backup` requires `saved` boolean, `version` number, and `path` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.rollback", [&]() {
				return ValidateBooleanAndNumber(payload, "rolledBack", "version", issue, "`gateway.config.rollback` requires `rolledBack` boolean and `version` number.");
			} },
			{ "gateway.events.summary", [&]() {
				return ValidateNumericFields(payload, { "total", "lifecycle", "updates" }, issue, "`gateway.events.summary` requires numeric fields `total`, `lifecycle`, and `updates`.");
			} },
			{ "gateway.events.channels", [&]() {
				return ValidateNumericFields(payload, { "channelEvents", "accountEvents", "count" }, issue, "`gateway.events.channels` requires numeric fields `channelEvents`, `accountEvents`, and `count`.");
			} },
			{ "gateway.models.priority", [&]() {
				if (!ValidateArrayAndCount(payload, "models", issue, "`gateway.models.priority` requires `models` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "default", "reasoner" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.priority` is missing required model ids.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.providers", [&]() {
				if (!ValidateArrayAndCount(payload, "providers", issue, "`gateway.models.providers` requires `providers` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "seed" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.providers` is missing required provider members.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.compatibility", [&]() {
				if (!IsFieldValueType(payload, "default", '"') || !IsFieldValueType(payload, "reasoner", '"') || !IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.compatibility` requires `default` string, `reasoner` string, and `count` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.schema", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.schema` requires `gateway` and `agent` object fields.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "bind", "port", "model", "streaming" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.schema` requires `bind`, `port`, `model`, and `streaming` schema members.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.snapshot", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.snapshot` requires `gateway` and `agent` object fields.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "bind", "port", "model", "streaming" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.snapshot` requires `bind`, `port`, `model`, and `streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.logs.levels", [&]() {
				if (!ValidateArrayAndCount(payload, "levels", issue, "`gateway.logs.levels` requires `levels` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "info", "debug" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.logs.levels` is missing required level members.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.categories", [&]() {
				if (!ValidateArrayAndCount(payload, "categories", issue, "`gateway.tools.categories` requires `categories` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "messaging", "knowledge" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.tools.categories` is missing required category members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.types", [&]() {
				if (!ValidateArrayAndCount(payload, "types", issue, "`gateway.events.types` requires `types` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "lifecycle", "update" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.types` is missing required type members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.list", [&]() {
				if (!ValidateArrayAndCount(payload, "events", issue, "`gateway.events.list` requires `events` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, kCatalogEventNames)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.list` is missing required event members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.catalog", [&]() {
				if (!IsFieldValueType(payload, "events", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.catalog` requires array field `events`.");
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, kCatalogEventNames)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.catalog` is missing required event members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.batch", [&]() {
				if (!ValidateArrayAndCount(payload, "batches", issue, "`gateway.events.batch` requires `batches` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "lifecycle", "updates" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.batch` is missing required batch names.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.count", [&]() {
				return ValidateNumberAndString(payload, "count", "section", issue, "`gateway.config.count` requires `section` string and `count` number.");
			} },
			{ "gateway.models.count", [&]() {
				return ValidateNumberAndString(payload, "count", "provider", issue, "`gateway.models.count` requires `provider` string and `count` number.");
			} },
			{ "gateway.config.getKey", [&]() {
				return ValidateTwoStringFields(payload, "key", "value", issue, "`gateway.config.getKey` requires `key` string and `value` string.");
			} },
			{ "gateway.events.search", [&]() {
				return ValidateStringArrayAndCount(payload, "term", "events", issue, "`gateway.events.search` requires `term` string, `events` array, and `count` number.");
			} },
			{ "gateway.config.audit", [&]() {
				if (!IsFieldBoolean(payload, "enabled") || !IsFieldValueType(payload, "source", '"') || !IsFieldNumber(payload, "lastUpdatedMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.audit` requires `enabled` boolean, `source` string, and `lastUpdatedMs` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.create", [&]() {
				if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "created")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.create` requires `agent` object and `created` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.create` requires `agent` fields `id`, `name`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.delete", [&]() {
				if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldNumber(payload, "remaining")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.delete` requires `agent` object, `deleted` boolean, and `remaining` number.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.delete` requires `agent` fields `id`, `name`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.update", [&]() {
				if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "updated")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.update` requires `agent` object and `updated` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.update` requires `agent` fields `id`, `name`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.list", [&]() {
				if (!IsFieldValueType(payload, "agents", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "activeAgentId", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires `agents` array, `count` number, and `activeAgentId` string fields.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "agents")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires agent entries with `id`, `name`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.files.exists", [&]() {
				return ValidateStringAndBoolean(payload, "path", "exists", issue, "`gateway.agents.files.exists` requires `path` string and `exists` boolean fields.");
			} },
			{ "gateway.sessions.delete", [&]() {
				if (!IsFieldValueType(payload, "session", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldNumber(payload, "remaining")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.delete` requires `session` object, `deleted` boolean, and `remaining` number fields.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.delete` requires `session` fields `id`, `scope`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.compact", [&]() {
				if (!IsFieldNumber(payload, "compacted") || !IsFieldNumber(payload, "remaining") || !IsFieldBoolean(payload, "dryRun")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.compact` requires numeric `compacted`/`remaining` and boolean `dryRun` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.patch", [&]() {
				if (!IsFieldValueType(payload, "session", '{') || !IsFieldBoolean(payload, "patched")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.patch` requires `session` object and `patched` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.patch` requires `session` fields `id`, `scope`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.preview", [&]() {
				const bool hasRoot = IsFieldValueType(payload, "session", '{') && IsFieldValueType(payload, "title", '"') && IsFieldBoolean(payload, "hasMessages") && IsFieldNumber(payload, "unread");
				if (!hasRoot || !PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.preview` requires `session` with `id/scope/active`, `title`, `hasMessages`, and `unread` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.usage", [&]() {
				const bool hasRoot = IsFieldValueType(payload, "sessionId", '"') && IsFieldNumber(payload, "messages") && IsFieldValueType(payload, "tokens", '{') && IsFieldNumber(payload, "lastActiveMs");
				const bool hasTokens = IsFieldNumber(payload, "input") && IsFieldNumber(payload, "output") && IsFieldNumber(payload, "total");
				if (!(hasRoot && hasTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.usage` requires `sessionId`, `messages`, `tokens.{input,output,total}`, and `lastActiveMs` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.session.list", [&]() {
				if (!IsFieldValueType(payload, "sessions", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "activeSessionId", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires `sessions` array, `count` number, and `activeSessionId` string fields.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "sessions")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires session entries with `id`, `scope`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.get", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires object fields `gateway` and `agent`.");
					return false;
				}
				if (!IsFieldValueType(payload, "bind", '"') || !IsFieldNumber(payload, "port") || !IsFieldValueType(payload, "model", '"') || !IsFieldBoolean(payload, "streaming")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires `gateway.bind` string, `gateway.port` number, `agent.model` string, and `agent.streaming` boolean fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.set", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "updated")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.set` requires `gateway`, `agent`, and `updated` fields.");
					return false;
				}
				if (!IsFieldValueType(payload, "bind", '"') || !IsFieldNumber(payload, "port") || !IsFieldValueType(payload, "model", '"') || !IsFieldBoolean(payload, "streaming")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.set` requires `gateway.bind`, `gateway.port`, `agent.model`, and `agent.streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.getSection", [&]() {
				if (!IsFieldValueType(payload, "section", '"') || !IsFieldValueType(payload, "config", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.getSection` requires `section` string and `config` object.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.sections", [&]() {
				if (!ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.sections` requires `sections` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "gateway", "agent" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.sections` is missing required section members.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.connections.count", [&]() {
				if (!IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.connections.count` requires numeric field `count`.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.get", [&]() {
				if (!IsFieldBoolean(payload, "exclusiveAddrUse") || !IsFieldBoolean(payload, "keepAlive") || !IsFieldBoolean(payload, "noDelay") || !IsFieldNumber(payload, "idleTimeoutMs") || !IsFieldNumber(payload, "handshakeTimeoutMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.get` requires policy booleans and timeout number fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.history", [&]() {
				if (!ValidateArrayAndCount(payload, "entries", issue, "`gateway.transport.policy.history` requires `entries` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "version", "applied", "reason" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.history` requires entry fields `version`, `applied`, and `reason`.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.status", [&]() {
				const bool hasCoreFields = IsFieldBoolean(payload, "running") && IsFieldValueType(payload, "endpoint", '"') && IsFieldNumber(payload, "connections");
				const bool hasTimeoutFields = IsFieldValueType(payload, "timeouts", '{') && IsFieldNumber(payload, "handshake") && IsFieldNumber(payload, "idle");
				const bool hasCloseFields = IsFieldValueType(payload, "closes", '{') && IsFieldNumber(payload, "invalidUtf8") && IsFieldNumber(payload, "messageTooBig") && IsFieldNumber(payload, "extensionRejected");
               const bool hasCompressionFields =
					IsFieldValueType(payload, "compression", '{') &&
					IsFieldValueType(payload, "policy", '"') &&
					IsFieldBoolean(payload, "perMessageDeflate");
				if (!(hasCoreFields && hasTimeoutFields && hasCloseFields && hasCompressionFields)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.status` requires `running`, `endpoint`, `connections`, `timeouts.{handshake,idle}`, `closes.{invalidUtf8,messageTooBig,extensionRejected}`, and `compression.{policy,perMessageDeflate}`.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.call.preview", [&]() {
				return IsFieldValueType(payload, "tool", '"') && IsFieldBoolean(payload, "allowed") && IsFieldValueType(payload, "reason", '"') && IsFieldBoolean(payload, "argsProvided") && IsFieldValueType(payload, "policy", '"')
					? true
					: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.preview` requires `tool`, `allowed`, `reason`, `argsProvided`, and `policy` fields."), false);
			} },
			{ "gateway.tools.call.execute", [&]() {
				return IsFieldValueType(payload, "tool", '"') && IsFieldBoolean(payload, "executed") && IsFieldValueType(payload, "status", '"') && IsFieldValueType(payload, "output", '"') && IsFieldBoolean(payload, "argsProvided")
					? true
					: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.execute` requires `tool`, `executed`, `status`, `output`, and `argsProvided` fields."), false);
			} },
			{ "gateway.features.list", [&]() {
				if (!IsFieldValueType(payload, "methods", '[') || !IsFieldValueType(payload, "events", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.features.list` requires array fields `methods` and `events`.");
					return false;
				}

				if (!PayloadContainsAllStringValues(payload, kFeatureRequiredCatalogAll) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredConfigCluster) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredTransportCluster) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredEventCluster) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredModelToolCluster)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.features.list` catalog is missing required method/event members.");
					return false;
				}

				return true;
			} },
			{ "gateway.ping", [&]() {
				if (!IsFieldBoolean(payload, "pong")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.ping` response requires boolean field `pong`.");
					return false;
				}
				return true;
			} },
			{ "gateway.protocol.version", [&]() {
				if (!IsFieldNumber(payload, "minProtocol") || !IsFieldNumber(payload, "maxProtocol")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.protocol.version` requires numeric protocol fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.logout", [&]() {
				if (!IsFieldBoolean(payload, "loggedOut") || !IsFieldNumber(payload, "affected")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.logout` requires `loggedOut` boolean and `affected` number fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.logs.tail", [&]() {
				if (!IsFieldValueType(payload, "entries", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.logs.tail` requires array field `entries`.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "entries")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "ts", "level", "source", "message" }) || !IsFieldNumber(payload, "ts") || !IsFieldValueType(payload, "level", '"') || !IsFieldValueType(payload, "source", '"') || !IsFieldValueType(payload, "message", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.logs.tail` requires log entries with `ts`, `level`, `source`, and `message` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.list", [&]() {
				if (!IsFieldValueType(payload, "models", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.list` requires array field `models`.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "models")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kModelFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.list` requires model entries with `id`, `provider`, `displayName`, and `streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.listByProvider", [&]() {
				if (!IsFieldValueType(payload, "provider", '"') || !IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.listByProvider` requires `provider` string, `models` array, and `count` number.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "models")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kModelFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.listByProvider` requires model entries with `id`, `provider`, `displayName`, and `streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.health", [&]() {
				return IsFieldValueType(payload, "status", '"') && IsFieldBoolean(payload, "running")
					? true
					: (SetIssue(issue, "schema_invalid_response", "`gateway.health` requires `status` string and `running` boolean."), false);
			} },
			{ "gateway.health.details", [&]() {
				if (!IsFieldValueType(payload, "status", '"') || !IsFieldBoolean(payload, "running") || !IsFieldValueType(payload, "transport", '{') || !IsFieldValueType(payload, "endpoint", '"') || !IsFieldNumber(payload, "connections")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.health.details` requires `status`, `running`, and `transport.{endpoint,connections}` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.status", [&]() {
				if (!IsFieldValueType(payload, "state", '"') || !IsFieldValueType(payload, "activeSession", '"') || !IsFieldValueType(payload, "activeAgent", '"') || !IsFieldNumber(payload, "queueDepth")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.status` requires `state`, `activeSession`, `activeAgent`, and `queueDepth` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.queue", [&]() {
				if (!IsFieldNumber(payload, "queued") || !IsFieldNumber(payload, "running") || !IsFieldNumber(payload, "capacity")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.queue` requires `queued`, `running`, and `capacity` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.assign", [&]() {
				if (!IsFieldValueType(payload, "agentId", '"') || !IsFieldValueType(payload, "sessionId", '"') || !IsFieldBoolean(payload, "assigned")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.assign` requires `agentId`, `sessionId`, and `assigned` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.rebalance", [&]() {
				if (!IsFieldNumber(payload, "moved") || !IsFieldNumber(payload, "remaining") || !IsFieldValueType(payload, "strategy", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.rebalance` requires `moved`, `remaining`, and `strategy` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.drain", [&]() {
				if (!IsFieldNumber(payload, "drained") || !IsFieldNumber(payload, "remaining") || !IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.drain` requires `drained`, `remaining`, and `reason` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.snapshot", [&]() {
				if (!IsFieldNumber(payload, "sessions") || !IsFieldNumber(payload, "agents") || !IsFieldValueType(payload, "active", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.snapshot` requires `sessions`, `agents`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.timeline", [&]() {
				if (!IsFieldValueType(payload, "ticks", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "source", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.timeline` requires `ticks`, `count`, and `source` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.heartbeat", [&]() {
				if (!IsFieldBoolean(payload, "alive") || !IsFieldNumber(payload, "intervalMs") || !IsFieldNumber(payload, "jitterMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.heartbeat` requires `alive`, `intervalMs`, and `jitterMs` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.pulse", [&]() {
				if (!IsFieldNumber(payload, "pulse") || !IsFieldNumber(payload, "driftMs") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.pulse` requires `pulse`, `driftMs`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.cadence", [&]() {
				if (!IsFieldNumber(payload, "periodMs") || !IsFieldNumber(payload, "varianceMs") || !IsFieldBoolean(payload, "aligned")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.cadence` requires `periodMs`, `varianceMs`, and `aligned` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.beacon", [&]() {
				if (!IsFieldValueType(payload, "beacon", '"') || !IsFieldNumber(payload, "seq") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.beacon` requires `beacon`, `seq`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.epoch", [&]() {
				if (!IsFieldNumber(payload, "epoch") || !IsFieldNumber(payload, "startedMs") || !IsFieldBoolean(payload, "active")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.epoch` requires `epoch`, `startedMs`, and `active` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.phase", [&]() {
				if (!IsFieldValueType(payload, "phase", '"') || !IsFieldNumber(payload, "step") || !IsFieldBoolean(payload, "locked")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phase` requires `phase`, `step`, and `locked` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.signal", [&]() {
				if (!IsFieldValueType(payload, "signal", '"') || !IsFieldNumber(payload, "priority") || !IsFieldBoolean(payload, "latched")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.signal` requires `signal`, `priority`, and `latched` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.vector", [&]() {
				if (!IsFieldValueType(payload, "axis", '"') || !IsFieldNumber(payload, "magnitude") || !IsFieldBoolean(payload, "normalized")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vector` requires `axis`, `magnitude`, and `normalized` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.matrix", [&]() {
				if (!IsFieldNumber(payload, "rows") || !IsFieldNumber(payload, "cols") || !IsFieldBoolean(payload, "balanced")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.matrix` requires `rows`, `cols`, and `balanced` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.lattice", [&]() {
				if (!IsFieldNumber(payload, "layers") || !IsFieldNumber(payload, "nodes") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.lattice` requires `layers`, `nodes`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.mesh", [&]() {
				if (!IsFieldNumber(payload, "nodes") || !IsFieldNumber(payload, "edges") || !IsFieldBoolean(payload, "connected")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.mesh` requires `nodes`, `edges`, and `connected` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.fabric", [&]() {
				if (!IsFieldNumber(payload, "threads") || !IsFieldNumber(payload, "links") || !IsFieldBoolean(payload, "resilient")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.fabric` requires `threads`, `links`, and `resilient` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.load", [&]() {
				if (!IsFieldNumber(payload, "queueLoad") || !IsFieldNumber(payload, "agentLoad") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.load` requires `queueLoad`, `agentLoad`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.saturation", [&]() {
				if (!IsFieldNumber(payload, "saturation") || !IsFieldNumber(payload, "capacity") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.saturation` requires `saturation`, `capacity`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.pressure", [&]() {
				if (!IsFieldNumber(payload, "pressure") || !IsFieldNumber(payload, "threshold") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.pressure` requires `pressure`, `threshold`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.headroom", [&]() {
				if (!IsFieldNumber(payload, "headroom") || !IsFieldNumber(payload, "used") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.headroom` requires `headroom`, `used`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.balance", [&]() {
				if (!IsFieldBoolean(payload, "balanced") || !IsFieldNumber(payload, "skew") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.balance` requires `balanced`, `skew`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.efficiency", [&]() {
				if (!IsFieldNumber(payload, "efficiency") || !IsFieldNumber(payload, "waste") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.efficiency` requires `efficiency`, `waste`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.utilization", [&]() {
				if (!IsFieldNumber(payload, "utilization") || !IsFieldNumber(payload, "capacity") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.utilization` requires `utilization`, `capacity`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.capacity", [&]() {
				if (!IsFieldNumber(payload, "capacity") || !IsFieldNumber(payload, "used") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.capacity` requires `capacity`, `used`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.occupancy", [&]() {
				if (!IsFieldNumber(payload, "occupancy") || !IsFieldNumber(payload, "slots") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.occupancy` requires `occupancy`, `slots`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.elasticity", [&]() {
				if (!IsFieldNumber(payload, "elasticity") || !IsFieldNumber(payload, "headroom") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.elasticity` requires `elasticity`, `headroom`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.cohesion", [&]() {
				if (!IsFieldNumber(payload, "cohesion") || !IsFieldNumber(payload, "groups") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.cohesion` requires `cohesion`, `groups`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.resilience", [&]() {
				if (!IsFieldNumber(payload, "resilience") || !IsFieldNumber(payload, "faults") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.resilience` requires `resilience`, `faults`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.readiness", [&]() {
				if (!IsFieldBoolean(payload, "ready") || !IsFieldNumber(payload, "queueDepth") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.readiness` requires `ready`, `queueDepth`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.contention", [&]() {
				if (!IsFieldNumber(payload, "contention") || !IsFieldNumber(payload, "waiters") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.contention` requires `contention`, `waiters`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.fairness", [&]() {
				if (!IsFieldNumber(payload, "fairness") || !IsFieldNumber(payload, "skew") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.fairness` requires `fairness`, `skew`, and `state` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.equilibrium", [&]() {
				if (!IsFieldNumber(payload, "equilibrium") || !IsFieldNumber(payload, "delta") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.equilibrium` requires `equilibrium`, `delta`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.steadiness", [&]() {
				if (!IsFieldBoolean(payload, "steady") || !IsFieldNumber(payload, "variance") || !IsFieldNumber(payload, "windowMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.steadiness` requires `steady`, `variance`, and `windowMs` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.parity", [&]() {
				if (!IsFieldNumber(payload, "parity") || !IsFieldNumber(payload, "gap") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.parity` requires `parity`, `gap`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.stabilityIndex", [&]() {
				if (!IsFieldNumber(payload, "stabilityIndex") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.stabilityIndex` requires `stabilityIndex`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.convergence", [&]() {
				if (!IsFieldNumber(payload, "convergence") || !IsFieldNumber(payload, "drift") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.convergence` requires `convergence`, `drift`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.hysteresis", [&]() {
				if (!IsFieldNumber(payload, "hysteresis") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.hysteresis` requires `hysteresis`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.balanceIndex", [&]() {
				if (!IsFieldNumber(payload, "balanceIndex") || !IsFieldNumber(payload, "skew") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.balanceIndex` requires `balanceIndex`, `skew`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.phaseLock", [&]() {
				if (!IsFieldBoolean(payload, "locked") || !IsFieldValueType(payload, "phase", '"') || !IsFieldNumber(payload, "drift")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseLock` requires `locked`, `phase`, and `drift` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.symmetry", [&]() {
				if (!IsFieldNumber(payload, "symmetry") || !IsFieldNumber(payload, "offset") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.symmetry` requires `symmetry`, `offset`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.gradient", [&]() {
				if (!IsFieldNumber(payload, "gradient") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.gradient` requires `gradient`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.harmonicity", [&]() {
				if (!IsFieldNumber(payload, "harmonicity") || !IsFieldNumber(payload, "detune") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.harmonicity` requires `harmonicity`, `detune`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.inertia", [&]() {
				if (!IsFieldNumber(payload, "inertia") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.inertia` requires `inertia`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.cadenceIndex", [&]() {
				if (!IsFieldNumber(payload, "cadenceIndex") || !IsFieldNumber(payload, "jitter") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.cadenceIndex` requires `cadenceIndex`, `jitter`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.damping", [&]() {
				if (!IsFieldNumber(payload, "damping") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.damping` requires `damping`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.waveLock", [&]() {
				if (!IsFieldBoolean(payload, "locked") || !IsFieldValueType(payload, "phase", '"') || !IsFieldNumber(payload, "slip")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.waveLock` requires `locked`, `phase`, and `slip` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.flux", [&]() {
				if (!IsFieldNumber(payload, "flux") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.flux` requires `flux`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.vectorField", [&]() {
				if (!IsFieldNumber(payload, "vectors") || !IsFieldNumber(payload, "magnitude") || !IsFieldValueType(payload, "state", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorField` requires `vectors`, `magnitude`, and `state` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.phaseEnvelope", [&]() {
				if (!IsFieldValueType(payload, "phase", '"') || !IsFieldNumber(payload, "amplitude") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseEnvelope` requires `phase`, `amplitude`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.vectorPhase", [&]() {
				if (!IsFieldNumber(payload, "vectorPhase") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorPhase` requires `vectorPhase`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.biasDrift", [&]() {
				if (!IsFieldNumber(payload, "biasDrift") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.biasDrift` requires `biasDrift`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.vectorDrift", [&]() {
				if (!IsFieldNumber(payload, "vectorDrift") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorDrift` requires `vectorDrift`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.phaseBias", [&]() {
				if (!IsFieldValueType(payload, "phase", '"') || !IsFieldNumber(payload, "bias") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseBias` requires `phase`, `bias`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.phaseVector", [&]() {
				if (!IsFieldNumber(payload, "phaseVector") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseVector` requires `phaseVector`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.biasEnvelope", [&]() {
				if (!IsFieldNumber(payload, "biasEnvelope") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.biasEnvelope` requires `biasEnvelope`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
           { "gateway.runtime.orchestration.phaseMatrix", [&]() {
				if (!IsFieldNumber(payload, "phaseMatrix") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseMatrix` requires `phaseMatrix`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.driftEnvelope", [&]() {
				if (!IsFieldNumber(payload, "driftEnvelope") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.driftEnvelope` requires `driftEnvelope`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseLattice", [&]() {
				if (!IsFieldNumber(payload, "phaseLattice") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseLattice` requires `phaseLattice`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.envelopeDrift", [&]() {
				if (!IsFieldNumber(payload, "envelopeDrift") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.envelopeDrift` requires `envelopeDrift`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseContour", [&]() {
				if (!IsFieldNumber(payload, "phaseContour") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseContour` requires `phaseContour`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.driftVector", [&]() {
				if (!IsFieldNumber(payload, "driftVector") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.driftVector` requires `driftVector`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseRibbon", [&]() {
				if (!IsFieldNumber(payload, "phaseRibbon") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseRibbon` requires `phaseRibbon`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorEnvelope", [&]() {
				if (!IsFieldNumber(payload, "vectorEnvelope") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorEnvelope` requires `vectorEnvelope`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseHelix", [&]() {
				if (!IsFieldNumber(payload, "phaseHelix") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseHelix` requires `phaseHelix`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorContour", [&]() {
				if (!IsFieldNumber(payload, "vectorContour") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorContour` requires `vectorContour`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseSpiral", [&]() {
				if (!IsFieldNumber(payload, "phaseSpiral") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseSpiral` requires `phaseSpiral`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorRibbon", [&]() {
				if (!IsFieldNumber(payload, "vectorRibbon") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorRibbon` requires `vectorRibbon`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseArc", [&]() {
				if (!IsFieldNumber(payload, "phaseArc") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseArc` requires `phaseArc`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorSpiral", [&]() {
				if (!IsFieldNumber(payload, "vectorSpiral") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorSpiral` requires `vectorSpiral`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseMesh", [&]() {
				if (!IsFieldNumber(payload, "phaseMesh") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseMesh` requires `phaseMesh`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorArc", [&]() {
				if (!IsFieldNumber(payload, "vectorArc") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorArc` requires `vectorArc`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseFabric", [&]() {
				if (!IsFieldNumber(payload, "phaseFabric") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseFabric` requires `phaseFabric`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorMesh", [&]() {
				if (!IsFieldNumber(payload, "vectorMesh") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorMesh` requires `vectorMesh`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseNet", [&]() {
				if (!IsFieldNumber(payload, "phaseNet") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseNet` requires `phaseNet`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorNode", [&]() {
				if (!IsFieldNumber(payload, "vectorNode") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorNode` requires `vectorNode`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseCore", [&]() {
				if (!IsFieldNumber(payload, "phaseCore") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseCore` requires `phaseCore`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorCore", [&]() {
				if (!IsFieldNumber(payload, "vectorCore") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorCore` requires `vectorCore`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseFrame", [&]() {
				if (!IsFieldNumber(payload, "phaseFrame") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseFrame` requires `phaseFrame`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorFrame", [&]() {
				if (!IsFieldNumber(payload, "vectorFrame") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorFrame` requires `vectorFrame`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseSpan", [&]() {
				if (!IsFieldNumber(payload, "phaseSpan") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseSpan` requires `phaseSpan`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorSpan", [&]() {
				if (!IsFieldNumber(payload, "vectorSpan") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorSpan` requires `vectorSpan`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseGrid", [&]() {
				if (!IsFieldNumber(payload, "phaseGrid") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseGrid` requires `phaseGrid`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorGrid", [&]() {
				if (!IsFieldNumber(payload, "vectorGrid") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorGrid` requires `vectorGrid`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseLane", [&]() {
				if (!IsFieldNumber(payload, "phaseLane") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseLane` requires `phaseLane`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorLane", [&]() {
				if (!IsFieldNumber(payload, "vectorLane") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorLane` requires `vectorLane`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseTrack", [&]() {
				if (!IsFieldNumber(payload, "phaseTrack") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseTrack` requires `phaseTrack`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorTrack", [&]() {
				if (!IsFieldNumber(payload, "vectorTrack") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorTrack` requires `vectorTrack`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseRail", [&]() {
				if (!IsFieldNumber(payload, "phaseRail") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseRail` requires `phaseRail`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorRail", [&]() {
				if (!IsFieldNumber(payload, "vectorRail") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorRail` requires `vectorRail`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.orchestration.phaseSpline", [&]() {
				if (!IsFieldNumber(payload, "phaseSpline") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.phaseSpline` requires `phaseSpline`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.orchestration.vectorSpline", [&]() {
				if (!IsFieldNumber(payload, "vectorSpline") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.orchestration.vectorSpline` requires `vectorSpline`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.status", [&]() {
				if (!IsFieldBoolean(payload, "enabled") || !IsFieldValueType(payload, "mode", '"') || !IsFieldNumber(payload, "heartbeatMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.status` requires `enabled`, `mode`, and `heartbeatMs` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.sample", [&]() {
				if (!IsFieldValueType(payload, "chunks", '[') || !IsFieldNumber(payload, "count") || !IsFieldBoolean(payload, "final")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.sample` requires `chunks`, `count`, and `final` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.window", [&]() {
				if (!IsFieldNumber(payload, "windowMs") || !IsFieldNumber(payload, "frames") || !IsFieldNumber(payload, "dropped")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.window` requires `windowMs`, `frames`, and `dropped` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.backpressure", [&]() {
				if (!IsFieldNumber(payload, "pressure") || !IsFieldBoolean(payload, "throttled") || !IsFieldNumber(payload, "bufferedFrames")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.backpressure` requires `pressure`, `throttled`, and `bufferedFrames` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.replay", [&]() {
				if (!IsFieldNumber(payload, "replayed") || !IsFieldValueType(payload, "cursor", '"') || !IsFieldBoolean(payload, "complete")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.replay` requires `replayed`, `cursor`, and `complete` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.cursor", [&]() {
				if (!IsFieldValueType(payload, "cursor", '"') || !IsFieldNumber(payload, "lagMs") || !IsFieldBoolean(payload, "hasMore")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.cursor` requires `cursor`, `lagMs`, and `hasMore` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.metrics", [&]() {
				if (!IsFieldNumber(payload, "frames") || !IsFieldNumber(payload, "bytes") || !IsFieldNumber(payload, "avgChunkMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.metrics` requires `frames`, `bytes`, and `avgChunkMs` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.health", [&]() {
				if (!IsFieldBoolean(payload, "healthy") || !IsFieldNumber(payload, "stalls") || !IsFieldNumber(payload, "recoveries")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.health` requires `healthy`, `stalls`, and `recoveries` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.snapshot", [&]() {
				if (!IsFieldNumber(payload, "frames") || !IsFieldValueType(payload, "cursor", '"') || !IsFieldBoolean(payload, "sealed")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.snapshot` requires `frames`, `cursor`, and `sealed` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.watermark", [&]() {
				if (!IsFieldNumber(payload, "high") || !IsFieldNumber(payload, "low") || !IsFieldNumber(payload, "current")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.watermark` requires `high`, `low`, and `current` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.checkpoint", [&]() {
				if (!IsFieldValueType(payload, "checkpoint", '"') || !IsFieldNumber(payload, "frames") || !IsFieldBoolean(payload, "persisted")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.checkpoint` requires `checkpoint`, `frames`, and `persisted` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.resume", [&]() {
				if (!IsFieldBoolean(payload, "resumed") || !IsFieldValueType(payload, "cursor", '"') || !IsFieldNumber(payload, "replayed")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.resume` requires `resumed`, `cursor`, and `replayed` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.recovery", [&]() {
				if (!IsFieldBoolean(payload, "recovering") || !IsFieldNumber(payload, "attempts") || !IsFieldNumber(payload, "lastMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.recovery` requires `recovering`, `attempts`, and `lastMs` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.continuity", [&]() {
				if (!IsFieldBoolean(payload, "continuous") || !IsFieldNumber(payload, "gaps") || !IsFieldNumber(payload, "lastSeq")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.continuity` requires `continuous`, `gaps`, and `lastSeq` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.stability", [&]() {
				if (!IsFieldBoolean(payload, "stable") || !IsFieldNumber(payload, "variance") || !IsFieldNumber(payload, "samples")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.stability` requires `stable`, `variance`, and `samples` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.integrity", [&]() {
				if (!IsFieldBoolean(payload, "valid") || !IsFieldNumber(payload, "violations") || !IsFieldNumber(payload, "checked")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.integrity` requires `valid`, `violations`, and `checked` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.coherence", [&]() {
				if (!IsFieldBoolean(payload, "coherent") || !IsFieldNumber(payload, "drift") || !IsFieldNumber(payload, "segments")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.coherence` requires `coherent`, `drift`, and `segments` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.fidelity", [&]() {
				if (!IsFieldNumber(payload, "fidelity") || !IsFieldNumber(payload, "drops") || !IsFieldBoolean(payload, "verified")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.fidelity` requires `fidelity`, `drops`, and `verified` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.accuracy", [&]() {
				if (!IsFieldNumber(payload, "accuracy") || !IsFieldNumber(payload, "mismatches") || !IsFieldBoolean(payload, "calibrated")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.accuracy` requires `accuracy`, `mismatches`, and `calibrated` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.buffer", [&]() {
				if (!IsFieldNumber(payload, "bufferedFrames") || !IsFieldNumber(payload, "bufferedBytes") || !IsFieldNumber(payload, "highWatermark")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.buffer` requires `bufferedFrames`, `bufferedBytes`, and `highWatermark` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.throttle", [&]() {
				if (!IsFieldBoolean(payload, "throttled") || !IsFieldNumber(payload, "limitPerSec") || !IsFieldNumber(payload, "currentPerSec")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.throttle` requires `throttled`, `limitPerSec`, and `currentPerSec` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.pacing", [&]() {
				if (!IsFieldNumber(payload, "paceMs") || !IsFieldNumber(payload, "burst") || !IsFieldBoolean(payload, "adaptive")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.pacing` requires `paceMs`, `burst`, and `adaptive` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.jitter", [&]() {
				if (!IsFieldNumber(payload, "jitterMs") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.jitter` requires `jitterMs`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.drift", [&]() {
				if (!IsFieldNumber(payload, "driftMs") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "corrected")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.drift` requires `driftMs`, `windowMs`, and `corrected` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.variance", [&]() {
				if (!IsFieldNumber(payload, "variance") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.variance` requires `variance`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.deviation", [&]() {
				if (!IsFieldNumber(payload, "deviation") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "withinBudget")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.deviation` requires `deviation`, `samples`, and `withinBudget` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.alignment", [&]() {
				if (!IsFieldBoolean(payload, "aligned") || !IsFieldNumber(payload, "offsetMs") || !IsFieldNumber(payload, "windowMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.alignment` requires `aligned`, `offsetMs`, and `windowMs` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.skew", [&]() {
				if (!IsFieldNumber(payload, "skewMs") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.skew` requires `skewMs`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.dispersion", [&]() {
				if (!IsFieldNumber(payload, "dispersion") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.dispersion` requires `dispersion`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.curvature", [&]() {
				if (!IsFieldNumber(payload, "curvature") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.curvature` requires `curvature`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.smoothness", [&]() {
				if (!IsFieldNumber(payload, "smoothness") || !IsFieldNumber(payload, "jitterMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.smoothness` requires `smoothness`, `jitterMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.harmonics", [&]() {
				if (!IsFieldNumber(payload, "harmonics") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.harmonics` requires `harmonics`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.phase", [&]() {
				if (!IsFieldValueType(payload, "phase", '"') || !IsFieldNumber(payload, "step") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.phase` requires `phase`, `step`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.tempo", [&]() {
				if (!IsFieldNumber(payload, "tempo") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.tempo` requires `tempo`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.temporal", [&]() {
				if (!IsFieldNumber(payload, "temporal") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.temporal` requires `temporal`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.consistency", [&]() {
				if (!IsFieldBoolean(payload, "consistent") || !IsFieldNumber(payload, "deviation") || !IsFieldNumber(payload, "samples")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.consistency` requires `consistent`, `deviation`, and `samples` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.spectral", [&]() {
				if (!IsFieldNumber(payload, "spectral") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.spectral` requires `spectral`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.envelope", [&]() {
				if (!IsFieldNumber(payload, "floor") || !IsFieldNumber(payload, "ceiling") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.envelope` requires `floor`, `ceiling`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.resonance", [&]() {
				if (!IsFieldNumber(payload, "resonance") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.resonance` requires `resonance`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.vectorField", [&]() {
				if (!IsFieldNumber(payload, "vectors") || !IsFieldNumber(payload, "magnitude") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.vectorField` requires `vectors`, `magnitude`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.waveform", [&]() {
				if (!IsFieldValueType(payload, "waveform", '"') || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.waveform` requires `waveform`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.horizon", [&]() {
				if (!IsFieldNumber(payload, "horizonMs") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.horizon` requires `horizonMs`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.vectorClock", [&]() {
				if (!IsFieldNumber(payload, "clock") || !IsFieldNumber(payload, "lag") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.vectorClock` requires `clock`, `lag`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.trend", [&]() {
				if (!IsFieldValueType(payload, "trend", '"') || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.trend` requires `trend`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.coordination", [&]() {
				if (!IsFieldBoolean(payload, "coordinated") || !IsFieldNumber(payload, "lag") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.coordination` requires `coordinated`, `lag`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.latencyBand", [&]() {
				if (!IsFieldNumber(payload, "minMs") || !IsFieldNumber(payload, "maxMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.latencyBand` requires `minMs`, `maxMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.phaseNoise", [&]() {
				if (!IsFieldNumber(payload, "phaseNoise") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.phaseNoise` requires `phaseNoise`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.beat", [&]() {
				if (!IsFieldNumber(payload, "beatHz") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.beat` requires `beatHz`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.modulation", [&]() {
				if (!IsFieldNumber(payload, "modulation") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "bounded")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.modulation` requires `modulation`, `samples`, and `bounded` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.pulseTrain", [&]() {
				if (!IsFieldNumber(payload, "pulseHz") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.pulseTrain` requires `pulseHz`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
         { "gateway.runtime.streaming.cohesion", [&]() {
				if (!IsFieldBoolean(payload, "cohesive") || !IsFieldNumber(payload, "delta") || !IsFieldNumber(payload, "samples")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.cohesion` requires `cohesive`, `delta`, and `samples` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.waveIndex", [&]() {
				if (!IsFieldNumber(payload, "waveIndex") || !IsFieldNumber(payload, "windowMs") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.waveIndex` requires `waveIndex`, `windowMs`, and `stable` fields.");
					return false;
				}
				return true;
			} },
            { "gateway.runtime.streaming.syncDrift", [&]() {
				if (!IsFieldNumber(payload, "syncDrift") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncDrift` requires `syncDrift`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandStability", [&]() {
				if (!IsFieldNumber(payload, "bandStability") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandStability` requires `bandStability`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
            { "gateway.runtime.streaming.syncEnvelope", [&]() {
				if (!IsFieldNumber(payload, "syncEnvelope") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncEnvelope` requires `syncEnvelope`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandDrift", [&]() {
				if (!IsFieldNumber(payload, "bandDrift") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandDrift` requires `bandDrift`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
            { "gateway.runtime.streaming.syncVector", [&]() {
				if (!IsFieldNumber(payload, "syncVector") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncVector` requires `syncVector`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandEnvelope", [&]() {
				if (!IsFieldNumber(payload, "bandEnvelope") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandEnvelope` requires `bandEnvelope`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncMatrix", [&]() {
				if (!IsFieldNumber(payload, "syncMatrix") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncMatrix` requires `syncMatrix`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandVector", [&]() {
				if (!IsFieldNumber(payload, "bandVector") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandVector` requires `bandVector`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncContour", [&]() {
				if (!IsFieldNumber(payload, "syncContour") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncContour` requires `syncContour`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandMatrix", [&]() {
				if (!IsFieldNumber(payload, "bandMatrix") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandMatrix` requires `bandMatrix`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncRibbon", [&]() {
				if (!IsFieldNumber(payload, "syncRibbon") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncRibbon` requires `syncRibbon`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandContour", [&]() {
				if (!IsFieldNumber(payload, "bandContour") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandContour` requires `bandContour`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncHelix", [&]() {
				if (!IsFieldNumber(payload, "syncHelix") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncHelix` requires `syncHelix`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandRibbon", [&]() {
				if (!IsFieldNumber(payload, "bandRibbon") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandRibbon` requires `bandRibbon`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncSpiral", [&]() {
				if (!IsFieldNumber(payload, "syncSpiral") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncSpiral` requires `syncSpiral`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandHelix", [&]() {
				if (!IsFieldNumber(payload, "bandHelix") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandHelix` requires `bandHelix`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncArc", [&]() {
				if (!IsFieldNumber(payload, "syncArc") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncArc` requires `syncArc`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandSpiral", [&]() {
				if (!IsFieldNumber(payload, "bandSpiral") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandSpiral` requires `bandSpiral`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncMesh", [&]() {
				if (!IsFieldNumber(payload, "syncMesh") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncMesh` requires `syncMesh`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandLattice", [&]() {
				if (!IsFieldNumber(payload, "bandLattice") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandLattice` requires `bandLattice`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncFabric", [&]() {
				if (!IsFieldNumber(payload, "syncFabric") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncFabric` requires `syncFabric`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandArc", [&]() {
				if (!IsFieldNumber(payload, "bandArc") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandArc` requires `bandArc`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncNet", [&]() {
				if (!IsFieldNumber(payload, "syncNet") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncNet` requires `syncNet`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandNode", [&]() {
				if (!IsFieldNumber(payload, "bandNode") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandNode` requires `bandNode`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncCore", [&]() {
				if (!IsFieldNumber(payload, "syncCore") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncCore` requires `syncCore`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandCore", [&]() {
				if (!IsFieldNumber(payload, "bandCore") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandCore` requires `bandCore`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncFrame", [&]() {
				if (!IsFieldNumber(payload, "syncFrame") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncFrame` requires `syncFrame`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandFrame", [&]() {
				if (!IsFieldNumber(payload, "bandFrame") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandFrame` requires `bandFrame`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncSpan", [&]() {
				if (!IsFieldNumber(payload, "syncSpan") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncSpan` requires `syncSpan`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandSpan", [&]() {
				if (!IsFieldNumber(payload, "bandSpan") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandSpan` requires `bandSpan`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncGrid", [&]() {
				if (!IsFieldNumber(payload, "syncGrid") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncGrid` requires `syncGrid`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandGrid", [&]() {
				if (!IsFieldNumber(payload, "bandGrid") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandGrid` requires `bandGrid`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncLane", [&]() {
				if (!IsFieldNumber(payload, "syncLane") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncLane` requires `syncLane`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandLane", [&]() {
				if (!IsFieldNumber(payload, "bandLane") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandLane` requires `bandLane`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncTrack", [&]() {
				if (!IsFieldNumber(payload, "syncTrack") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncTrack` requires `syncTrack`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandTrack", [&]() {
				if (!IsFieldNumber(payload, "bandTrack") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandTrack` requires `bandTrack`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncRail", [&]() {
				if (!IsFieldNumber(payload, "syncRail") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncRail` requires `syncRail`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandRail", [&]() {
				if (!IsFieldNumber(payload, "bandRail") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandRail` requires `bandRail`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.runtime.streaming.syncSpline", [&]() {
				if (!IsFieldNumber(payload, "syncSpline") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncSpline` requires `syncSpline`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.bandSpline", [&]() {
				if (!IsFieldNumber(payload, "bandSpline") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.bandSpline` requires `bandSpline`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.syncBand", [&]() {
				if (!IsFieldNumber(payload, "syncBand") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.syncBand` requires `syncBand`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.runtime.streaming.waveDrift", [&]() {
				if (!IsFieldNumber(payload, "waveDrift") || !IsFieldNumber(payload, "samples") || !IsFieldBoolean(payload, "stable")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.runtime.streaming.waveDrift` requires `waveDrift`, `samples`, and `stable` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.status", [&]() {
				if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "fallbacks", '[') || !IsFieldNumber(payload, "maxRetries") || !IsFieldValueType(payload, "strategy", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.status` requires `primary`, `fallbacks`, `maxRetries`, and `strategy` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.preview", [&]() {
				if (!IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "attempts", '[') || !IsFieldValueType(payload, "selected", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.preview` requires `model`, `attempts`, and `selected` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.metrics", [&]() {
				if (!IsFieldNumber(payload, "attempts") || !IsFieldNumber(payload, "fallbackHits") || !IsFieldNumber(payload, "successRate")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.metrics` requires `attempts`, `fallbackHits`, and `successRate` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.simulate", [&]() {
				if (!IsFieldValueType(payload, "requested", '"') || !IsFieldValueType(payload, "resolved", '"') || !IsFieldBoolean(payload, "usedFallback")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.simulate` requires `requested`, `resolved`, and `usedFallback` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.audit", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "lastModel", '"') || !IsFieldValueType(payload, "lastOutcome", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.audit` requires `entries`, `lastModel`, and `lastOutcome` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.policy", [&]() {
				if (!IsFieldValueType(payload, "policy", '"') || !IsFieldNumber(payload, "maxRetries") || !IsFieldBoolean(payload, "stickyPrimary")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.policy` requires `policy`, `maxRetries`, and `stickyPrimary` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.history", [&]() {
				if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "last", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.history` requires `events`, `count`, and `last` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.recent", [&]() {
				if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "active", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.recent` requires `models`, `count`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.window", [&]() {
				if (!IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "attempts") || !IsFieldNumber(payload, "fallbacks")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.window` requires `windowSec`, `attempts`, and `fallbacks` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.digest", [&]() {
				if (!IsFieldValueType(payload, "digest", '"') || !IsFieldNumber(payload, "entries") || !IsFieldBoolean(payload, "fresh")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.digest` requires `digest`, `entries`, and `fresh` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.ledger", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldNumber(payload, "primaryHits") || !IsFieldNumber(payload, "fallbackHits")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.ledger` requires `entries`, `primaryHits`, and `fallbackHits` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.profile", [&]() {
				if (!IsFieldValueType(payload, "profile", '"') || !IsFieldValueType(payload, "weights", '[') || !IsFieldNumber(payload, "version")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.profile` requires `profile`, `weights`, and `version` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.baseline", [&]() {
				if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "secondary", '"') || !IsFieldNumber(payload, "confidence")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.baseline` requires `primary`, `secondary`, and `confidence` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.forecast", [&]() {
				if (!IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "projectedFallbacks") || !IsFieldValueType(payload, "risk", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.forecast` requires `windowSec`, `projectedFallbacks`, and `risk` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.threshold", [&]() {
				if (!IsFieldNumber(payload, "minSuccessRate") || !IsFieldNumber(payload, "maxFallbacks") || !IsFieldBoolean(payload, "active")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.threshold` requires `minSuccessRate`, `maxFallbacks`, and `active` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.guardrail", [&]() {
				if (!IsFieldValueType(payload, "rule", '"') || !IsFieldNumber(payload, "limit") || !IsFieldBoolean(payload, "enforced")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.guardrail` requires `rule`, `limit`, and `enforced` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.envelope", [&]() {
				if (!IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "floor") || !IsFieldNumber(payload, "ceiling")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.envelope` requires `windowSec`, `floor`, and `ceiling` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.margin", [&]() {
				if (!IsFieldNumber(payload, "headroom") || !IsFieldNumber(payload, "buffer") || !IsFieldBoolean(payload, "safe")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.margin` requires `headroom`, `buffer`, and `safe` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.reserve", [&]() {
				if (!IsFieldNumber(payload, "reserve") || !IsFieldBoolean(payload, "available") || !IsFieldNumber(payload, "priority")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.reserve` requires `reserve`, `available`, and `priority` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override` requires `active`, `model`, and `reason` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.clear", [&]() {
				if (!IsFieldBoolean(payload, "cleared") || !IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.clear` requires `cleared`, `active`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.status", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "source", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.status` requires `active`, `model`, and `source` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.history", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "lastModel", '"') || !IsFieldBoolean(payload, "active")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.history` requires `entries`, `lastModel`, and `active` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.metrics", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "switches") || !IsFieldValueType(payload, "lastModel", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.metrics` requires `active`, `switches`, and `lastModel` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.window", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "windowSec") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.window` requires `active`, `windowSec`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.digest", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "digest", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.digest` requires `active`, `digest`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.timeline", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "lastModel", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.timeline` requires `entries`, `active`, and `lastModel` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.catalog", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.catalog` requires `active`, `count`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.registry", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.registry` requires `active`, `entries`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.matrix", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "rows") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.matrix` requires `active`, `rows`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.snapshot", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "revision") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.snapshot` requires `active`, `revision`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.pointer", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "pointer", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.pointer` requires `active`, `pointer`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.state", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "state", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.state` requires `active`, `state`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.profile", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "profile", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.profile` requires `active`, `profile`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.audit", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.audit` requires `active`, `entries`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.checkpoint", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "checkpoint", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.checkpoint` requires `active`, `checkpoint`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.baseline", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "baseline", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.baseline` requires `active`, `baseline`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.manifest", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "manifest", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.manifest` requires `active`, `manifest`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.ledger", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.ledger` requires `active`, `entries`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.snapshotIndex", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "index") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.snapshotIndex` requires `active`, `index`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.digestIndex", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "digestIndex") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.digestIndex` requires `active`, `digestIndex`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.cursor", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "cursor", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.cursor` requires `active`, `cursor`, and `model` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.override.vector", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "vector", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vector` requires `active`, `vector`, and `model` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.override.vectorDrift", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorDrift") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorDrift` requires `active`, `vectorDrift`, and `model` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.override.phaseBias", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "phaseBias") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.phaseBias` requires `active`, `phaseBias`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.biasEnvelope", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "biasEnvelope") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.biasEnvelope` requires `active`, `biasEnvelope`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.driftEnvelope", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "driftEnvelope") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.driftEnvelope` requires `active`, `driftEnvelope`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.envelopeDrift", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "envelopeDrift") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.envelopeDrift` requires `active`, `envelopeDrift`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.driftVector", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "driftVector") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.driftVector` requires `active`, `driftVector`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorEnvelope", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorEnvelope") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorEnvelope` requires `active`, `vectorEnvelope`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorContour", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorContour") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorContour` requires `active`, `vectorContour`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorRibbon", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorRibbon") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorRibbon` requires `active`, `vectorRibbon`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpiral", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpiral") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpiral` requires `active`, `vectorSpiral`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorArc", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorArc") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorArc` requires `active`, `vectorArc`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorMesh", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorMesh") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorMesh` requires `active`, `vectorMesh`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorNode", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorNode") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorNode` requires `active`, `vectorNode`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorCore", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorCore") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorCore` requires `active`, `vectorCore`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorFrame", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorFrame") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorFrame` requires `active`, `vectorFrame`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpan", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpan") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpan` requires `active`, `vectorSpan`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorGrid", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorGrid") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorGrid` requires `active`, `vectorGrid`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorLane", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorLane") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorLane` requires `active`, `vectorLane`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorTrack", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorTrack") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorTrack` requires `active`, `vectorTrack`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorRail", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorRail") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorRail` requires `active`, `vectorRail`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpline", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpline") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpline` requires `active`, `vectorSpline`, and `model` fields.");
					return false;
				}
				return true;
			} },
		};

		if (const auto groupedIt = groupedValidators.find(method); groupedIt != groupedValidators.end()) {
			return groupedIt->second();
		}



		return true;
	}

} // namespace blazeclaw::gateway::protocol
