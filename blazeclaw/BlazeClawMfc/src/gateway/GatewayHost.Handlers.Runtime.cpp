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

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpline2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpline2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRail2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpline2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpline2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRail2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseTrack2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRail2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRail2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorTrack2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLane2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncTrack2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandTrack2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLane2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGrid2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLane2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLane2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGrid2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseBand2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGrid2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGrid2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorBand2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseArc2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBand2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandBand2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorArc2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMesh2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncArc2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandArc2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorMesh2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLink2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMesh2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandMesh2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLink2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNode3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLink2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLink2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseHub2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNode3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorHub2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGate2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncHub2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandHub2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGate2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRelay2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGate2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGate2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRelay2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phasePortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phasePortal\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRelay2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRelay2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorPortal\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseAnchor2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncPortal\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandPortal\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorAnchor2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseBridge\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncAnchor2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandAnchor2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorBridge\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNode\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBridge\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandBridge\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLink\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNode2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLink\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseThread\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLink\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLink\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorThread\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseChain\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncThread\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandThread\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorChain\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpline\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncChain\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandChain\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpline\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRail\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpline\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpline\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRail\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseTrack\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRail\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRail\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorTrack\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLane\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncTrack\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandTrack\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLane\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGrid\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLane\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLane\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGrid\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpan\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGrid\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGrid\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpan\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseFrame\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpan\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpan\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorFrame\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseCore\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncFrame\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandFrame\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorCore\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNet",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNet\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncCore\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandCore\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseFabric",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseFabric\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNet",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNet\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorMesh\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMesh\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncFabric",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncFabric\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandArc\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorArc\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseArc\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMesh\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLattice",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLattice\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpiral\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpiral\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncArc\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpiral\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRibbon\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseHelix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseHelix\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpiral\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandHelix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandHelix\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorContour\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRibbon\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncHelix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncHelix\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRibbon\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseContour\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRibbon\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandContour\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.driftVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"driftVector\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLattice",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLattice\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncContour\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandMatrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandMatrix\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.envelopeDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"envelopeDrift\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseVector\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMatrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMatrix\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandVector\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.biasEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"biasEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorPhase",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorPhase\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncEnvelope\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandDrift\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.biasDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"biasDrift\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorField",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectors\":2,\"magnitude\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncDrift\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandStability",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandStability\":100,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"amplitude\":1,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorDrift\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBias",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"bias\":0,\"stable\":true}",
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

        m_dispatcher.Register(
            "gateway.runtime.streaming.cohesion",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cohesive\":true,\"delta\":0,\"samples\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.waveIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"waveIndex\":1,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBand",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBand\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.waveDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"waveDrift\":0,\"samples\":2,\"stable\":true}",
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

        m_dispatcher.Register(
            "gateway.runtime.orchestration.capacity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"capacity\":8,\"used\":0,\"state\":\"ready\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.alignment",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"aligned\":true,\"offsetMs\":0,\"windowMs\":1000}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.timeline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"entries\":0,\"active\":false,\"lastModel\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.occupancy",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"occupancy\":0,\"slots\":8,\"state\":\"idle\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.skew",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"skewMs\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.catalog",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"count\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.elasticity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"elasticity\":100,\"headroom\":8,\"state\":\"expandable\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.dispersion",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"dispersion\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.registry",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"entries\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.cohesion",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cohesion\":100,\"groups\":1,\"state\":\"coherent\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.curvature",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"curvature\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.matrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"rows\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.resilience",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"resilience\":100,\"faults\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.smoothness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"smoothness\":100,\"jitterMs\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.snapshot",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"revision\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.readiness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"ready\":true,\"queueDepth\":0,\"state\":\"ready\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.harmonics",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"harmonics\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.pointer",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"pointer\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.contention",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"contention\":0,\"waiters\":0,\"state\":\"clear\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.phase",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"step\":1,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.state",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"state\":\"none\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.fairness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"fairness\":100,\"skew\":0,\"state\":\"balanced\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.tempo",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"tempo\":1,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.profile",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"profile\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.equilibrium",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"equilibrium\":100,\"delta\":0,\"state\":\"balanced\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.steadiness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"steady\":true,\"variance\":0,\"windowMs\":1000}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.temporal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"temporal\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.consistency",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"consistent\":true,\"deviation\":0,\"samples\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.audit",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"entries\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.parity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"parity\":100,\"gap\":0,\"state\":\"aligned\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.stabilityIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"stabilityIndex\":100,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.spectral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"spectral\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.envelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"floor\":0,\"ceiling\":100,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.checkpoint",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"checkpoint\":\"cp-override-1\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.convergence",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"convergence\":100,\"drift\":0,\"state\":\"locked\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.hysteresis",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"hysteresis\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.resonance",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"resonance\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.vectorField",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectors\":2,\"magnitude\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.baseline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"baseline\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.balanceIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"balanceIndex\":100,\"skew\":0,\"state\":\"balanced\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLock",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"locked\":true,\"phase\":\"steady\",\"drift\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.waveform",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"waveform\":\"flat\",\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.horizon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"horizonMs\":1000,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.manifest",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"manifest\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.symmetry",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"symmetry\":100,\"offset\":0,\"state\":\"aligned\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.gradient",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"gradient\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.vectorClock",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"clock\":1,\"lag\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.trend",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"trend\":\"flat\",\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.ledger",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"entries\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.harmonicity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"harmonicity\":100,\"detune\":0,\"state\":\"aligned\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.inertia",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"inertia\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.coordination",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"coordinated\":true,\"lag\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.latencyBand",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"minMs\":0,\"maxMs\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.snapshotIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"index\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.cadenceIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cadenceIndex\":100,\"jitter\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.damping",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"damping\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.phaseNoise",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNoise\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.beat",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"beatHz\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.digestIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"digestIndex\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.waveLock",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"locked\":true,\"phase\":\"steady\",\"slip\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.flux",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"flux\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMatrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMatrix\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.driftEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"driftEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.modulation",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"modulation\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncVector\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandEnvelope\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.pulseTrain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"pulseHz\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.cursor",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"cursor\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vector\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorDrift\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.phaseBias",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"phaseBias\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.biasEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"biasEnvelope\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.driftEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"driftEnvelope\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.envelopeDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"envelopeDrift\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.driftVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"driftVector\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorEnvelope\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorContour\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRibbon\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpiral\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorArc\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorMesh\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorCore\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorFrame\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpan\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGrid\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLane\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorTrack\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRail\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpline\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorChain\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorThread\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLink\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorBridge\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorAnchor2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorPortal\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRelay2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGate2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorHub2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLink2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorMesh2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorArc2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorBand2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGrid2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLane2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorTrack2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRail2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpline2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });
    }

} // namespace blazeclaw::gateway
