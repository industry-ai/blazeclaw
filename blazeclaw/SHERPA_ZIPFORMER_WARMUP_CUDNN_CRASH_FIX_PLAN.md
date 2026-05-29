# Sherpa Zipformer Warmup cuDNN Crash Fix Plan

## Purpose

Fix the fatal crash observed during Sherpa Zipformer streaming warmup:

- Exception: `0xC0000409`
- Native module: `cudnn_engines_precompiled64_9.dll`
- BlazeClaw frame: `SherpaZipformerStreamingEngine::TranscribeStreaming`
- Failing operation: encoder `Ort::Session::Run(...)` during `SpeechRecognitionRuntime::RunWarmupLocked()`

The goal is to prevent unsafe ONNX Runtime CUDA execution for the Sherpa streaming model before warmup reaches a native cuDNN fatal exit, while preserving native Sherpa streaming behavior and CPU fallback.

## Reference Findings

### FunASR reference behavior

Relevant files inspected:

- `FunASR/runtime/onnxruntime/CMakeLists.txt`
- `FunASR/runtime/onnxruntime/src/offline-stream.cpp`
- `FunASR/runtime/onnxruntime/src/paraformer-online.cpp`
- `FunASR/runtime/docs/benchmark_onnx_cpp.md`
- `FunASR/runtime/docs/SDK_advanced_guide_offline_gpu.md`
- `FunASR/runtime/docs/SDK_tutorial_online.md`

Important observations:

1. FunASR's ONNX Runtime C++ documentation is CPU-focused.
   - `benchmark_onnx_cpp.md` is explicitly titled `CPU Benchmark (ONNX-cpp)`.
   - The ONNX C++ build instructions download plain ONNX Runtime and do not document appending CUDA EP.

2. FunASR gates GPU support explicitly.
   - `CMakeLists.txt` defines `option(GPU "Whether to build with GPU" OFF)`.
   - GPU mode adds `-DUSE_GPU` and links Torch/TorchBlade paths, not ONNX Runtime CUDA EP.

3. FunASR falls back to CPU if GPU support is not compiled.
   - `offline-stream.cpp` checks `use_gpu` under `#ifdef USE_GPU`.
   - Without `USE_GPU`, it logs that GPU is unsupported and switches to CPU.

4. FunASR online ONNX inference uses CPU memory tensors.
   - `paraformer-online.cpp` creates inputs with `Ort::MemoryInfo::CreateCpu(...)`.
   - It runs `encoder_session_->Run(...)` with CPU tensors and no visible CUDA EP append in the online ONNX path.

Conclusion: FunASR keeps ONNX C++ streaming conservative and CPU-oriented unless GPU is explicitly supported by a separate build/runtime stack. BlazeClaw should not treat successful CUDA EP append as enough proof that Sherpa warmup inference is safe.

## BlazeClaw Findings

Relevant files:

- `BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`

Current behavior:

1. `SpeechRecognitionRuntime::EnsureLoadedLocked()` routes Sherpa models to `SherpaZipformerStreamingEngine::Load(...)`.
2. `SherpaZipformerStreamingEngine::Load(...)` appends ONNX Runtime CUDA EP directly when `speech.cudaEnabled` is true.
3. The Sherpa path does not use the existing `PassesSpeechCudaCompatibilityGuard(...)` or the process-level compatibility latch used by the non-Sherpa ONNX speech path.
4. `RunWarmupLocked()` immediately performs a real Sherpa streaming inference using generated warmup PCM.
5. The previous debugger session showed the crash occurs inside native cuDNN during the encoder `Run(...)`, before C++ exception handling can convert the failure into `warmup_failed`.

Debugger evidence:

- Encoder inputs were fully assembled: `input_count == 36`, `output_count == 36`.
- Feature tensor was internally consistent: `[1,39,80]`, with `3120` float elements.
- Crash happened before encoder output handling: `firstTokenEncoderEndOffsetMs == 0`.
- CUDA/cuDNN DLLs were loaded from mixed external locations, including CUDA 13 cuBLAS DLLs and cuDNN 9.20 binaries.

## Root Cause Hypothesis

The most likely root cause is unsafe CUDA/cuDNN runtime compatibility for ONNX Runtime CUDA EP in the Sherpa streaming path.

The app currently prevents this for the older non-Sherpa ONNX speech path by checking loaded CUDA/cuDNN DLL major versions and latching failures in-process. The Sherpa path bypasses that protection. As a result, warmup can enter CUDA EP execution and reach an unrecoverable cuDNN fatal abort.

A secondary risk is that Sherpa state-cache shape resolution may produce model-specific dynamic-cache shapes that should be contract-validated, but the current failure signature points first to native CUDA/cuDNN runtime safety rather than an ordinary ONNX shape validation error.

## Fix Strategy

### Phase 1: Share CUDA compatibility preflight with Sherpa

1. Move or duplicate the existing CUDA compatibility guard helpers so they are usable by both:
   - `SpeechRecognitionRuntime.cpp`
   - `SherpaZipformerStreamingEngine.cpp`

2. Preserve the current checks for loaded DLL major versions:
   - `cublas64_%d.dll` expected major `12`
   - `cublasLt64_%d.dll` expected major `12`
   - `cufft64_%d.dll` expected major `12`
   - `cudnn64_%d.dll` expected major `9`
   - `cudnn_graph64_%d.dll` expected major `9`
   - `cudnn_engines_precompiled64_%d.dll` expected major `9`
   - `cudnn_engines_runtime_compiled64_%d.dll` expected major `9`

3. Apply the guard before `TryAppendCudaExecutionProvider(...)` in `SherpaZipformerStreamingEngine::Load(...)`.

4. If the guard fails, do not append CUDA EP. Set:
   - `cudaExecutionProviderAvailable = false`
   - `cudaExecutionProviderEnabled = false`
   - `cudaExecutionProviderReason = <guard reason>`
   - `effectiveExecutionProvider = "cpu"`

5. Add trace output equivalent to the existing non-Sherpa path:
   - `runtime.execution_provider.compatibility_guard.blocked`
   - `runtime.execution_provider.compatibility_guard.latch`

### Phase 2: Add a process-level Sherpa CUDA latch

1. Add a latch in the owning runtime layer so a failed CUDA compatibility guard blocks subsequent Sherpa CUDA attempts in the same process.

2. Prefer reusing the existing `SpeechRecognitionRuntime` latch fields if possible:
   - `m_cudaCompatibilityGuardLatched`
   - `m_cudaCompatibilityGuardLatchedReason`

3. If direct reuse would make ownership awkward, extend `SherpaZipformerStreamingEngine::ExecutionProviderOptions` with:
   - `cudaCompatibilityGuardLatched`
   - `cudaCompatibilityGuardLatchedReason`

4. Ensure the runtime records the Sherpa provider status in the existing snapshot fields:
   - `cudaExecutionProviderAvailable`
   - `cudaExecutionProviderEnabled`
   - `cudaExecutionProviderReason`
   - `effectiveExecutionProvider`

### Phase 3: Make Sherpa warmup safe by construction

1. Do not let warmup be the first operation that discovers CUDA incompatibility.

2. After model load, but before warmup, ensure the selected provider is already final:
   - If guard blocked CUDA, warmup must run CPU.
   - If CUDA is enabled, it must have passed the guard and session creation.

3. Keep `RunWarmupLocked()` behavior as a real Sherpa streaming warmup, but only after provider selection is safe.

4. Add a configuration escape hatch if needed:
   - `speech.runtimeHotWarmupEnabled=false` remains a temporary mitigation.
   - `speech.cudaEnabled=false` remains the safest immediate workaround.

Do not add a fake transcription fallback. The project instruction is to use native Sherpa streaming behavior only.

### Phase 4: Improve diagnostics before native inference

Add structured trace fields before the first Sherpa encoder run:

- effective provider
- CUDA guard status/reason
- encoder input count/output count
- feature shape and element count
- state-cache binding count
- first few state-cache resolved shapes
- paths of already-loaded CUDA/cuDNN modules if available

The goal is to make future failures diagnosable without requiring a native debugger stop inside cuDNN.

### Phase 5: Optional Sherpa contract validation hardening

After the CUDA crash is fixed, add a CPU-only validation pass for the Sherpa encoder contract:

1. Verify initial state-cache shapes are accepted by the encoder on CPU.
2. Validate that each encoder output cache maps back to the next input cache.
3. Persist contract errors into `SpeechRecognitionDebugInfo` rather than relying only on traces.

This is lower priority than CUDA guard parity because the observed crash is native cuDNN fatal exit.

## Implementation Steps

Status: implemented in the BlazeClaw speech runtime and wired into the app/test projects.

1. Extract CUDA compatibility guard helpers into a shared speech runtime utility.
   - Candidate new files:
	 - `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechCudaCompatibilityGuard.h`
	 - `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechCudaCompatibilityGuard.cpp`

2. Replace the private guard implementation in `SpeechRecognitionRuntime.cpp` with the shared helper.

3. Add guard/latch data flow to `SherpaZipformerStreamingEngine::ExecutionProviderOptions` or call the shared guard from the owning runtime before invoking `Load(...)`.

4. Update `SherpaZipformerStreamingEngine::Load(...)` to skip CUDA EP append when the guard is blocked or latched.

5. Preserve CPU session creation fallback after CUDA session creation exceptions.

6. Add trace lines for Sherpa provider decisions and pre-encoder-run diagnostics.

7. Add or update tests covering:
   - guard blocks Sherpa CUDA when incompatible DLL majors are observed
   - Sherpa provider status reports CPU fallback reason
   - warmup uses CPU when CUDA is blocked
   - CUDA append is not attempted when latch is active

8. Validate with the required command:

   ```powershell
   msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
   ```

## Implementation Notes

- Added `SpeechCudaCompatibilityGuard.h/.cpp` as the shared CUDA/cuDNN major-version guard.
- `SpeechRecognitionRuntime.cpp` now uses the shared guard and passes its process latch state into the Sherpa engine before `Load(...)`.
- `SherpaZipformerStreamingEngine::ExecutionProviderOptions` and `ExecutionProviderStatus` now carry guard-latch state and reason strings.
- Sherpa load now skips CUDA EP append when the guard is already latched or when an incompatible loaded CUDA/cuDNN major is detected.
- CUDA session creation exceptions still fall back to CPU session creation.
- Sherpa provider decisions and pre-encoder-run diagnostics now trace the effective provider, CUDA reason, tensor counts, feature shape, element count, and state-cache summary.
- Added `SpeechCudaCompatibilityGuardTests.cpp` for guard formatting and Sherpa latch/provider-status behavior.

## Immediate Workarounds Until the Fix Lands

1. Disable Sherpa CUDA:
   - Set `speech.cudaEnabled=false` in `BlazeClawMfc/blazeclaw.conf`.

2. Or disable startup warmup temporarily:
   - Set `speech.runtimeHotWarmupEnabled=false` if available in the active config.

3. Prefer cleaning the runtime DLL path so ONNX Runtime CUDA EP does not load mismatched CUDA/cuDNN libraries.

## Acceptance Criteria

- BlazeClaw starts without crashing when mismatched CUDA/cuDNN DLLs are present.
- Sherpa model load reports `effectiveExecutionProvider=cpu` with a clear compatibility guard reason.
- Warmup completes on CPU or reports a recoverable `warmup_failed`; it must not terminate the process.
- When CUDA DLLs are compatible, Sherpa CUDA remains available and provider status reports `effectiveExecutionProvider=cuda`.
- The required `msbuild` command succeeds.
