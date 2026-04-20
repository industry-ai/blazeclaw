Developer notes

## vcpkg (canonical)

| Setting | Value |
|---------|--------|
| **Triplet** | `x64-windows` |
| **Packages** | `nlohmann-json`, `catch2` |

Install at the **repository root** (clone [vcpkg](https://github.com/microsoft/vcpkg) into `vcpkg\`):

```powershell
.\vcpkg\vcpkg.exe install --triplet x64-windows nlohmann-json catch2
```

Use **vcpkg MSBuild integration** so `$(VcpkgIncludeRoot)` resolves (e.g. `vcpkg integrate install` from that clone). Full detail: **`blazeclaw/docs/BUILD_AND_CI.md`**.

## Tests and CI

- **BlazeClawMfc.Tests** uses **Catch2** and **nlohmann::json** via vcpkg includes.
- **Azure Pipelines:** [`azure-pipelines.yml`](../azure-pipelines.yml) — vcpkg bootstrap, **`x64-windows`** install, **matrix** **Debug** + **Release** **VSBuild** with **`CodePage=65001`**, Catch2 on **Debug** only (**`Invoke-BlazeClawOptimizationValidation.ps1 -SkipPhaseA -SkipPhaseB -SkipBuild -RunTests -Configuration Debug`** — runs **Phase F** skill-plan check before tests unless **`-SkipPhaseF`**).
- **GitHub Actions:** [`.github/workflows/blazeclaw-chat-nightly-smoke.yml`](../.github/workflows/blazeclaw-chat-nightly-smoke.yml) — smoke checks; see **BUILD_AND_CI.md** for how this differs from Azure.

Local build example (UTF-8 code page, matches CI and **`Invoke-BlazeClawOptimizationValidation.ps1`**):

```powershell
cd E:\gitRepo\blazeClaw
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

**Release\|x64** (vcpkg headers only for JSON — catches integration gaps):

```powershell
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /p:CodePage=65001
```

## third_party fallbacks

`BlazeClawMfc/third_party/` may include **catch2** / **nlohmann** include fallbacks when vcpkg is not configured. **CI is expected to use vcpkg.** Planned removal of those stubs is documented in **`blazeclaw/docs/BUILD_AND_CI.md`** (Phase E). Longer-lived vendor paths (ONNX, llama.cpp) stay separate.

BlazeClawMfc planning docs:
- `blazeclaw/BlazeClawMfc/PROJECT_REVIEW.md`
- `blazeclaw/BlazeClawMfc/CHAT_RUNTIME_ASYNC_WORK_QUEUE_PLAN.md`
- `blazeclaw/BlazeClawMfc/CHAT_UI_INCREMENTAL_RENDER_PLAN.md`
- `blazeclaw/BlazeClawMfc/DYNAMIC_TASK_DELTA_FULL_EXECUTION_PLAN.md`

Architecture comparison docs:
- `blazeclaw/docs/index.md` (project review: optimization summary + step-by-step Phases A–F + next-wave 7–9 + follow-on 10–11)
- `blazeclaw/docs/README.md` (file catalog for `blazeclaw/docs/`)
- `blazeclaw/docs/architecture.md` (layer model; OpenClaw comparison §11; optimization recommendations §12; phased plan §13)
- `blazeclaw/docs/blazeclaw-openclaw-architecture-framework-gap-analysis.md` (BlazeClaw vs OpenClaw stacks, mapping, gaps, optimization priorities)
- `blazeclaw/docs/PROTOCOL_CODEGEN.md` (gateway manifest/codegen, default handler coordinator, thin façade checklist, Phase A–B scripts §7, `OkResponse` / `ErrorResponse` / `ReplayFromStored`, `EncodeValidatedEvent`, serializers)
- `blazeclaw/BlazeClawMfc/tools/GatewayUpstreamDiff/` (`Diff-OpenClawGateway.ps1`, `Verify-GatewayDispatcherMethods.ps1`)
- `blazeclaw/BlazeClawMfc/tools/Verify-SkillPortingPlans.ps1` (Phase F: every `blazeclaw/skills/<name>/` has `PORTING_PLAN.md`)
- `blazeclaw/BlazeClawMfc/tools/Invoke-BlazeClawOptimizationValidation.ps1` (Phases A+B+F+E in one run; **`-SkipPhaseF`** optional; optional `-RunTests` — Catch2 CWD `blazeclaw/`)
- `blazeclaw/docs/SERVICE_LAYER_BOUNDARIES.md` (`ServiceManager` ↔ `GatewayHost`, `WireAllGatewayServiceCallbacks`)
- `blazeclaw/docs/GATEWAY_CORE_WIRING.md` (Phase C wiring, Phase D task-delta recency & streaming/startup notes)
- `blazeclaw/docs/BUILD_AND_CI.md` (Phase E: vcpkg, Azure Pipelines, stub deprecation plan)
- `blazeclaw/docs/SKILL_PORTING.md` (Phase F: PORTING_PLAN pattern, skill index)
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp.md` (deep dive: **thin façade** invariant—`RegisterDefaultHandlers` → `RegisterDefaultHandlerSequence` only from `GatewayHost.cpp`; split `GatewayHost.Handlers.*`; no god lambdas; shared protocol surface; named handler types)
