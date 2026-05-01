# Copilot Instructions

## Project Guidelines
- Prefer Way 3 (full modular runtime) for ServiceManager refactoring over other refactor options.
- Preserve the skill tool manifest mechanism when it provides better performance; design migration plans to maintain manifest-based loading rather than remove manifests.
- Account for runtime working directory differences (repo root vs BlazeClawMfc project root) when performing root-cause investigations; behavior can change depending on launch location.