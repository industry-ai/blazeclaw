# BlazeClawShim.vcxproj Implementation Plan

## Objective
Implement `BlazeClawShim.vcxproj` as a production-ready static library project that cleanly provides the self-evolving C++ reminder shim for BlazeClaw runtime consumers.

## Scope
- Project file hardening (`BlazeClawShim.vcxproj`)
- Source/header layout for shim API
- Build integration with `BlazeClaw.sln` and `BlazeClawMfc`
- Validation with `msbuild`
- Documentation for maintainers and skill authors

## Current State Summary
- `BlazeClawShim` exists as a static library project.
- `self_evolving_shim.cpp` is present.
- Project is referenced by `BlazeClawMfc`.
- Basic filters and README exist.

## Implementation Phases

### Phase 1 — Project Baseline Hardening
1. Add missing standard VC++ project metadata to `BlazeClawShim.vcxproj`:
   - `VCProjectVersion`
   - `WindowsTargetPlatformVersion`
   - `ProjectName`
2. Add explicit output/intermediate paths aligned with solution conventions.
3. Add Debug/Release compile settings parity where appropriate:
   - warning level
   - conformance mode
   - C++ language standard
4. Keep `PrecompiledHeader=NotUsing` for minimal shim portability.

### Phase 2 — Public API Contract
1. Add a dedicated public header (for example `self_evolving_shim.h`) declaring:
   - `extern "C" void blazeclaw_self_evolving_reminder();`
2. Include the header in `self_evolving_shim.cpp`.
3. Ensure symbol naming and calling contract are stable for MFC and other native consumers.

### Phase 3 — Consumer Integration
1. Verify `BlazeClawMfc.vcxproj` has a valid `ProjectReference` to `BlazeClawShim.vcxproj`.
2. If needed, add explicit linker dependency handling for the static library output.
3. Add a minimal runtime call site in BlazeClaw bootstrap path (if not already present) to invoke `blazeclaw_self_evolving_reminder()`.

### Phase 4 — Build Reliability
1. Run solution validation:
   - `msbuild blazeclaw\BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
2. Run Release build validation:
   - `msbuild blazeclaw\BlazeClaw.sln /m /p:Configuration=Release /p:Platform=x64`
3. Resolve any warnings/errors introduced by shim project settings.

### Phase 5 — Documentation and Maintenance
1. Update `BlazeClawShim/README.md` with:
   - API usage snippet
   - expected output behavior
   - consumer integration notes
2. Add troubleshooting section:
   - unresolved external symbol guidance
   - platform toolset mismatch guidance
3. Keep `.vcxproj.filters` synchronized with file additions.

## Deliverables
- Hardened `blazeclaw/BlazeClawShim/BlazeClawShim.vcxproj`
- Public API header for shim
- Updated integration in consumer project(s)
- Updated `blazeclaw/BlazeClawShim/README.md`
- Successful Debug/Release msbuild validation logs

## Acceptance Criteria
- `BlazeClaw.sln` builds successfully in Debug and Release x64 via `msbuild`.
- `BlazeClawShim.lib` is produced in both configurations.
- Consumer project links without unresolved symbols.
- Shim reminder API is callable through a stable header contract.
- Documentation reflects final integration and troubleshooting steps.

## Risks and Mitigations
- Risk: Toolset/platform mismatch across projects.
  - Mitigation: Align platform toolset and target platform version with solution defaults.
- Risk: Missing explicit call site causes shim to compile but never execute.
  - Mitigation: Add and verify one bootstrap invocation path.
- Risk: Header/source drift for exported function signature.
  - Mitigation: Enforce declaration in a single public header used by both producer and consumer.

## Suggested Execution Order
1. Baseline hardening
2. Public header/API contract
3. Consumer integration
4. Debug + Release validation with msbuild
5. Documentation finalization
