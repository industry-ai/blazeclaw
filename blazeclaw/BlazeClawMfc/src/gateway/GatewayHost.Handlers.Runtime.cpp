#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

    namespace {
        std::string EscapeJsonLocal(const std::string& value) {
            std::string escaped;
            escaped.reserve(value.size() + 8);

            for (const char ch : value) {
                switch (ch) {
                case '"':
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(ch);
                    break;
                }
            }

            return escaped;
        }
    }

    void GatewayHost::RegisterRuntimeHandlers() {
        m_dispatcher.Register("gateway.runtime.orchestration.status", [this](const protocol::RequestFrame& request) {
            const auto sessions = m_sessionRegistry.List();
            const auto agents = m_agentRegistry.List();
            const std::string activeSession = sessions.empty() ? "main" : sessions.front().id;
            const std::string activeAgent = agents.empty() ? "default" : agents.front().id;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"state\":\"idle\",\"activeSession\":\"" +
                    EscapeJsonLocal(activeSession) + "\",\"activeAgent\":\"" +
                    EscapeJsonLocal(activeAgent) + "\",\"queueDepth\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.status", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":" +
                    std::string(m_runtimeAgentStreaming ? "true" : "false") +
                    ",\"mode\":\"chunked\",\"heartbeatMs\":1500}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"primary\":\"default\",\"fallbacks\":[\"reasoner\"],\"maxRetries\":2,\"strategy\":\"ordered\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.queue", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"queued\":0,\"running\":0,\"capacity\":8}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.sample", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"chunks\":[\"hello\",\"world\"],\"count\":2,\"final\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.preview", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"model\":\"default\",\"attempts\":[\"default\",\"reasoner\"],\"selected\":\"default\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.assign", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"agentId\":\"default\",\"sessionId\":\"main\",\"assigned\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.window", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"windowMs\":5000,\"frames\":2,\"dropped\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"attempts\":2,\"fallbackHits\":1,\"successRate\":1.0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.rebalance", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"moved\":0,\"remaining\":2,\"strategy\":\"sticky\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.backpressure", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pressure\":0,\"throttled\":false,\"bufferedFrames\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.simulate", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"requested\":\"default\",\"resolved\":\"default\",\"usedFallback\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.drain", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"drained\":0,\"remaining\":0,\"reason\":\"idle\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.replay", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"replayed\":2,\"cursor\":\"stream-cursor-1\",\"complete\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.audit", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"entries\":2,\"lastModel\":\"default\",\"lastOutcome\":\"primary\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.snapshot",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"sessions\":2,\"agents\":2,\"active\":\"main\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.cursor",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cursor\":\"stream-cursor-1\",\"lagMs\":0,\"hasMore\":false}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.policy",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"policy\":\"ordered\",\"maxRetries\":2,\"stickyPrimary\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.timeline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"ticks\":[0,1],\"count\":2,\"source\":\"scheduler\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.metrics",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"frames\":2,\"bytes\":10,\"avgChunkMs\":5}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.history",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"events\":[\"primary\",\"fallback\"],\"count\":2,\"last\":\"primary\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.heartbeat",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"alive\":true,\"intervalMs\":1000,\"jitterMs\":25}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.health",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"healthy\":true,\"stalls\":0,\"recoveries\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.recent",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"models\":[\"default\",\"reasoner\"],\"count\":2,\"active\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.pulse",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"pulse\":1,\"driftMs\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.snapshot",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"frames\":2,\"cursor\":\"stream-cursor-2\",\"sealed\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.window",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"windowSec\":60,\"attempts\":2,\"fallbacks\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.cadence",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"periodMs\":1000,\"varianceMs\":5,\"aligned\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.watermark",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"high\":16,\"low\":4,\"current\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.digest",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"digest\":\"sha256:failover-v1\",\"entries\":2,\"fresh\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.beacon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"beacon\":\"orch-1\",\"seq\":1,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.checkpoint",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"checkpoint\":\"cp-1\",\"frames\":2,\"persisted\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.ledger",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"entries\":2,\"primaryHits\":1,\"fallbackHits\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.epoch",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"epoch\":1,\"startedMs\":1735689600000,\"active\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.resume",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"resumed\":true,\"cursor\":\"stream-cursor-3\",\"replayed\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.profile",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"profile\":\"balanced\",\"weights\":[70,30],\"version\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phase",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"step\":1,\"locked\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.recovery",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"recovering\":false,\"attempts\":0,\"lastMs\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.baseline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"primary\":\"default\",\"secondary\":\"reasoner\",\"confidence\":100}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.signal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"signal\":\"ok\",\"priority\":1,\"latched\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.continuity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"continuous\":true,\"gaps\":0,\"lastSeq\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.forecast",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"windowSec\":60,\"projectedFallbacks\":1,\"risk\":\"low\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"axis\":\"primary\",\"magnitude\":1,\"normalized\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.stability",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"stable\":true,\"variance\":0,\"samples\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.threshold",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"minSuccessRate\":90,\"maxFallbacks\":2,\"active\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.matrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"rows\":2,\"cols\":2,\"balanced\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.integrity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"valid\":true,\"violations\":0,\"checked\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.guardrail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"rule\":\"max_fallbacks\",\"limit\":2,\"enforced\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.lattice",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"layers\":2,\"nodes\":4,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.coherence",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"coherent\":true,\"drift\":0,\"segments\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.envelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"windowSec\":60,\"floor\":90,\"ceiling\":100}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.mesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"nodes\":4,\"edges\":3,\"connected\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.fidelity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"fidelity\":100,\"drops\":0,\"verified\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.margin",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"headroom\":10,\"buffer\":2,\"safe\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.fabric",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"threads\":6,\"links\":8,\"resilient\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.accuracy",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"accuracy\":99,\"mismatches\":0,\"calibrated\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.reserve",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"reserve\":1,\"available\":true,\"priority\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.load",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"queueLoad\":0,\"agentLoad\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.buffer",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bufferedFrames\":0,\"bufferedBytes\":0,\"highWatermark\":16}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"model\":\"default\",\"reason\":\"none\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.saturation",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"saturation\":0,\"capacity\":8,\"state\":\"stable\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.throttle",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"throttled\":false,\"limitPerSec\":120,\"currentPerSec\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.clear",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cleared\":true,\"active\":false,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.pressure",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"pressure\":0,\"threshold\":80,\"state\":\"normal\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.pacing",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"paceMs\":50,\"burst\":1,\"adaptive\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.status",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"model\":\"default\",\"source\":\"runtime\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.headroom",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"headroom\":8,\"used\":0,\"state\":\"ready\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.jitter",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"jitterMs\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.history",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"entries\":0,\"lastModel\":\"default\",\"active\":false}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.balance",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"balanced\":true,\"skew\":0,\"state\":\"stable\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.drift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"driftMs\":0,\"windowMs\":1000,\"corrected\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.metrics",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"switches\":0,\"lastModel\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.efficiency",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"efficiency\":100,\"waste\":0,\"state\":\"optimized\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.variance",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"variance\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.window",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"windowSec\":60,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.utilization",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"utilization\":0,\"capacity\":8,\"state\":\"idle\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.deviation",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"deviation\":0,\"samples\":2,\"withinBudget\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.digest",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"digest\":\"sha256:override-v1\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });
    }

} // namespace blazeclaw::gateway
