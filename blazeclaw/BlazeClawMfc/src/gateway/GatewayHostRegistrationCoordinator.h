#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

/// Sequences `GatewayHost` dispatcher registration by **domain phases** (channels, tooling, gateway
/// catalog, agents/sessions, config/security, runtime/transport, supplementary). The call order matches
/// `RegisterDefaultHandlers` / `GatewayHost.cpp.md`; implementation lives in
/// `GatewayHostRegistrationCoordinator.cpp`.
namespace GatewayHostRegistration {

void RegisterDefaultHandlerSequence(GatewayHost& host);

} // namespace GatewayHostRegistration

} // namespace blazeclaw::gateway
