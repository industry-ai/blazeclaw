Developer notes

This workspace uses vcpkg for dependency management in CI. To install dependencies locally run:

  .\vcpkg\vcpkg.exe install nlohmann-json catch2

Test project and CI notes

- The test project BlazeClawMfc.Tests uses Catch2 (v3) and nlohmann::json via vcpkg.
- An Azure Pipelines definition (azure-pipelines.yml) has been added to install vcpkg packages, build the solution, and run the test executable.

Local setup example (PowerShell):

  cd E:\gitRepo\blazeClaw
  .\vcpkg\vcpkg.exe install --triplet x64-windows nlohmann-json catch2
  msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64

Notes:
- The project currently includes lightweight third_party stubs to ease local builds when vcpkg is not installed. It's recommended to use vcpkg as the source of truth and remove stubs once vcpkg is relied upon in CI.
- The tests are executed as part of CI in the pipeline; the pipeline runs the produced test executable and returns its exit code.

BlazeClawMfc planning docs:
- `blazeclaw/BlazeClawMfc/PROJECT_REVIEW.md`
- `blazeclaw/BlazeClawMfc/CHAT_RUNTIME_ASYNC_WORK_QUEUE_PLAN.md`
- `blazeclaw/BlazeClawMfc/CHAT_UI_INCREMENTAL_RENDER_PLAN.md`
- `blazeclaw/BlazeClawMfc/DYNAMIC_TASK_DELTA_FULL_EXECUTION_PLAN.md`

Architecture comparison docs:
- `blazeclaw/docs/README.md` (index)
- `blazeclaw/docs/architecture.md` (layer model; OpenClaw comparison §11; optimization recommendations §12; phased plan §13)
- `blazeclaw/docs/blazeclaw-openclaw-architecture-framework-gap-analysis.md` (BlazeClaw vs OpenClaw stacks, mapping, gaps, optimization priorities)
- `blazeclaw/docs/PROTOCOL_CODEGEN.md` (gateway manifest/codegen, default handler coordinator, thin façade checklist, Phase A–B scripts §7, `OkResponse` / `ErrorResponse` / `ReplayFromStored`, `EncodeValidatedEvent`, serializers)
- `blazeclaw/BlazeClawMfc/tools/GatewayUpstreamDiff/` (`Diff-OpenClawGateway.ps1`, `Verify-GatewayDispatcherMethods.ps1`)
- `blazeclaw/docs/SERVICE_LAYER_BOUNDARIES.md` (`ServiceManager` ↔ `GatewayHost`, `WireAllGatewayServiceCallbacks`)
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp.md` (deep dive: **thin façade** invariant—`RegisterDefaultHandlers` → `RegisterDefaultHandlerSequence` only from `GatewayHost.cpp`; split `GatewayHost.Handlers.*`; no god lambdas; shared protocol surface; named handler types)
