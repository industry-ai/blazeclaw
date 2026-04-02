# DeepSeek Extension (BlazeClaw)

This directory contains the BlazeClaw-native extension assets for the DeepSeek provider.

## Included artifacts
- `blazeclaw.extension.json`: extension/provider manifest metadata
- `model-catalog.json`: DeepSeek model catalog metadata used by discovery and onboarding surfaces

## Notes
- These assets establish Phase B (directory + manifest + registration) of the DeepSeek porting plan.
- Runtime model routing and auth application logic are implemented in later phases.
- Chat runtime now executes embedded tool orchestration before provider fallback.
- DeepSeek is used as model backend transport in fallback path with injected skills prompt context.
