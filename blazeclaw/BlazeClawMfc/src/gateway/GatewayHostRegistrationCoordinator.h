#pragma once

namespace blazeclaw::gateway {

	class GatewayHost;

	/// Sequences `GatewayHost` dispatcher registration by **domain phases** (channels, tooling, gateway
	/// catalog, agents/sessions, config/security, runtime/transport, supplementary). This is the **only**
	/// `friend` entry that may call the private `Register*Handlers` methods—keep **`GatewayHost.cpp`** a
	/// **thin façade** (transport, routing, lifecycle, event helpers, and `RegisterDefaultHandlers` →
	/// `RegisterDefaultHandlerSequence`); do not move bulk handler lambdas back here (`GatewayHost.cpp.md`).
	/// `RegisterDefaultHandlers` binds `GatewayRuntimeContext` before invoking this sequence (S3).
	/// The call order matches `RegisterDefaultHandlers` / `GatewayHost.cpp.md`; implementation lives in
	/// `GatewayHostRegistrationCoordinator.cpp`.
	namespace GatewayHostRegistration {

		void RegisterDefaultHandlerSequence(GatewayHost& host);

	} // namespace GatewayHostRegistration

} // namespace blazeclaw::gateway
