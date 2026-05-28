# Sherpa Zipformer no-output regression root-cause fix plan

## Problem statement

Before the Sherpa Zipformer recognition-quality improvement work, BlazeClaw could
produce a speech-recognition result, even if the decoded text quality was poor.
After the quality-plan implementation, the same finite `pcm_stream` path for an
utterance such as `请讲一个笑话` completes without a user-visible transcript or
segment.

The current failure is not that audio cannot be recorded, not that the finite
stream cannot be drained, and not that the gateway cannot return a response. The
runtime now reaches the Sherpa encoder and joiner, but the joiner selects blank
for every processed encoder frame, so no token text exists for the gateway to
publish.

## Evidence from the latest run

Latest debug/build evidence shows:

- The Debug x64 build succeeds.
- The recorder saves WAV artifacts, proving audio capture occurred.
- The finite `pcm_stream` reaches `sequenceEnd`:
  - `runtimeCursorNextSequence == requestSequenceEnd`.
  - `sherpaFinalDrainComplete=true`.
- The final fbank flush path runs:
  - `sherpaFinalFbankFlush=true`.
- The audio is not being treated as silence:
  - `sherpaSpeechActive=true`.
- The Sherpa model path runs:
  - `sherpaEncoderFrameCount > 0`.
  - `sherpaJoinerCallCount > 0`.
- The active failure is all-blank model output:
  - `sherpaDecodedTokenCount=0`.
  - `sherpaEmittedTokenCount=0`.
  - `sherpaLastBestTokenId=0`.
  - `sherpaBlankTokenCount == sherpaJoinerCallCount`.
  - `sherpaFinalOutcome="no_tokens_emitted"`.
- The previous state-cache collision bug is no longer the active root cause:
  - encoder state-cache mapping now reports distinct `cacheIndex` values,
  - `sherpaEncoderStateCacheBindingCount=35`,
  - `sherpaEncoderStateCacheUpdateCount > 0`.
- Gateway "no response" is downstream of empty model output:
  - `hasSegment` is only true when the native runtime provides a segment or
	completed text is non-empty,
  - all-blank Sherpa output leaves `normalizedText` empty,
  - gateway emits a completed lifecycle without `gateway.speech.segment`.

## Root cause

The root cause is a post-quality-plan Sherpa model-contract mismatch that makes
the encoder/joiner path blank-dominant. The runtime feeds the model and receives
valid tensors, but the tensors no longer represent the streaming Zipformer input
contract closely enough for non-blank token emission.

The most likely contract mismatches are, in priority order:

1. **Online frontend lifetime and chunking are wrong.**
   `BuildOnlineFbank(...)` constructs a new `knf::OnlineFbank` object for every
   loop over `pendingSamples`. This approximates online extraction by rebuilding
   features from retained samples, but it is not the same as Sherpa/FunASR's
   persistent online frontend state. It can duplicate, skip, or re-window frames
   differently from the reference runtime.
2. **Encoder chunk contract is not honored exactly.**
   The encoder input metadata shows a fixed chunk dimension such as
   `x shape=[-1,39,80]`, but runtime feature logs show variable frame batches
   such as 1, 3, 5, 7, 9, 11, 13, and 15 frames. The current code pads/copies
   these into an effective 39-frame input and sends `featureLength=copiedFrames`.
   If the Zipformer export expects exact chunk-size progression, correct left
   context, or exact processed-length semantics, this can produce valid but
   blank-only logits.
3. **Feature amplitude convention may be wrong for this Sherpa model.**
   Phase 2 adopted FunASR-style `sample * 32768` scaling before
   `OnlineFbank::AcceptWaveform(...)`. If this Sherpa ONNX export expects
   normalized float input or a different fbank normalization convention, the
   encoder receives feature magnitudes outside the distribution used by the
   reference decoder.
4. **State tensors are mapped, but dynamic cache shapes may still be initialized
   incorrectly.**
   The fixed `cacheIndex` bug prevents all state tensors from overwriting the
   same slot, but dimensions with `-1` are still converted to 1 when allocating
   caches. Some Zipformer caches may require model-specific static dimensions
   derived from the matching output shape or Sherpa config, not generic dynamic
   fallback sizing.
5. **Decoder/joiner shape handling still needs reference verification.**
   The RNN-T loop runs, but decoder and joiner tensors are shaped from generic
   metadata heuristics. If the joiner expects `[N,512]` while the runtime feeds
   `[1,1,512]`, or if decoder context uses the wrong blank/SOS initialization,
   logits can remain blank-dominant without throwing an ONNX error.

## Non-root causes ruled out

- **Audio capture:** WAV artifacts are saved and speech energy is detected.
- **Stream lifecycle:** finite requests drain to `sequenceEnd` and final flush is
  visible in telemetry.
- **Gateway/WebView transport:** gateway returns `ok=true` and emits lifecycle
  telemetry; it suppresses only the transcript segment because text is empty.
- **Original cache-index collision:** fixed and verified by distinct cache-index
  traces. It was a real bug, but not sufficient to restore recognition.
- **BPE/SentencePiece decoding:** no token IDs are emitted, so text decoding is
  not reached for user-visible text.

## Fix strategy

Fix the native Sherpa path by making the input tensors and stream state match the
reference Sherpa streaming runtime. Do not add fallback transcript generation or
rescue text. The success criterion is restored non-blank token emission from the
Sherpa model itself.

## Step-by-step implementation plan

### Step 1: Freeze the failing baseline

Status: implemented

Tool:

- `tools/freeze_sherpa_no_output_baseline.py`

Purpose:

- Validate that a persisted Sherpa baseline JSON still has the all-blank
  no-output regression signature.
- Archive the baseline JSON and optional exported Visual Studio debug trace in a
  stable folder for later comparison.
- Write a `freeze-summary.json` file so later implementation steps can compare
  against exactly the same failing run.

1. Keep `BLAZECLAW_SHERPA_BASELINE_DIR` enabled.
2. Capture the same `请讲一个笑话` finite `pcm_stream` run.
3. Archive the baseline JSON and Visual Studio debug trace.
4. Confirm the failure signature remains:
   - final drain complete,
   - final fbank flush true,
   - speech active true,
   - encoder frames greater than zero,
   - joiner calls greater than zero,
   - decoded token count zero,
   - blank token count equal to joiner call count.

Usage:

1. Start BlazeClaw with baseline persistence enabled:
   `BLAZECLAW_SHERPA_BASELINE_DIR=<diagnostics-dir>`.
2. Optionally set the expected transcript:
   `BLAZECLAW_SHERPA_BASELINE_EXPECTED_TEXT=请讲一个笑话`.
3. Capture the finite `pcm_stream` utterance through the normal app path.
4. Export the relevant Visual Studio Debug output to a text file if available.
5. Freeze the failing run:
   `python tools/freeze_sherpa_no_output_baseline.py --baseline <diagnostics-dir>/<runId>.sherpa-baseline.json --debug-log <debug-log.txt> --archive-dir diagnostics/sherpa-no-output-freeze`

Exit gate: one reproducible all-blank baseline is available for comparison.

### Step 2: Add contract-level diagnostics before changing behavior

Status: implemented

Implemented fields in `gateway.speech.debug.snapshot`:

- `sherpaContractFeatureInputShape`,
  `sherpaContractFeatureLengthValue`,
  `sherpaContractFeatureFrameCount`,
  `sherpaContractFeatureRealFrameCount`, and
  `sherpaContractFeaturePaddedFrameCount`.
- `sherpaContractFeatureFirstFrameStats` and
  `sherpaContractFeatureLastFrameStats`.
- `sherpaContractEncoderOutputShape` and
  `sherpaContractEncoderValidFrameCount`.
- `sherpaContractDecoderInputContext` and
  `sherpaContractDecoderOutputShape`.
- `sherpaContractJoinerEncoderInputShape`,
  `sherpaContractJoinerDecoderInputShape`,
  `sherpaContractJoinerOutputShape`, and
  `sherpaContractJoinerTopTokens`.

The native TRACE stream also emits sampled `[SherpaContract]` lines for the
first few joiner calls and then periodically, so Visual Studio Debug output can
show the same contract evidence without flooding every frame.

Add temporary diagnostics to `SherpaZipformerStreamingEngine` for each encoder,
decoder, and joiner invocation:

1. encoder feature input shape and length value,
2. number of real feature frames versus padded frames,
3. first/last feature-frame mean and range,
4. encoder output shape and valid-frame count,
5. decoder input token context and decoder output shape,
6. joiner input shapes,
7. top-5 joiner token IDs and scores for selected frames.

Exit gate: the failing run can show whether blanks are caused before encoder,
after encoder, or at decoder/joiner combination time.

### Step 3: Replace per-call fbank reconstruction with persistent online fbank state

Status: implemented

Implementation notes:

- `StreamState` now owns a persistent `SherpaOnlineFbankFrontend` wrapper around
  `knf::OnlineFbank` for each active stream.
- The streaming loop feeds only newly read ring-buffer chunks into
  `AcceptWaveform(...)`.
- The frontend tracks how many frames were already extracted from
  `NumFramesReady()` and returns only newly available frames.
- The finite-stream final chunk calls `InputFinished()` through a one-shot guard,
  preserving final flush behavior without repeating EOF signaling.
- New fbank frames are buffered as pending feature frames, and encoder assembly
  consumes frames from that buffer directly.
- The old retained-sample path and `featureFrames * 160` sample-erasure heuristic
  have been removed from the Sherpa path.

Change `StreamState` so each stream owns persistent online frontend state instead
of rebuilding a new `OnlineFbank` from `pendingSamples` each loop.

Implementation direction:

1. Store a per-stream fbank frontend or a small wrapper around
   `knf::OnlineFbank`.
2. Feed only newly read audio samples into `AcceptWaveform(...)`.
3. Track the number of frames already consumed from `NumFramesReady()`.
4. On final finite input, call `InputFinished()` once.
5. Extract only newly available complete frames.
6. Remove sample-consumption logic that assumes `featureFrames * 160` precisely
   maps back to retained `pendingSamples`.

Exit gate: feature frame counts and final flush behavior match the reference
online frontend for the same waveform.

### Step 4: Enforce exact encoder chunk assembly

Status: implemented

Implementation notes:

- The encoder feature input metadata now determines whether the model has a
  fixed chunk size, for example `[1,39,80]`.
- For fixed-size encoders, normal streaming waits until at least one full chunk
  is available before invoking the encoder.
- The runtime drains all available full fixed chunks from the pending feature
  buffer before reading additional audio.
- On final finite input, exactly one remaining partial chunk may be padded.
- Feature frames are copied in deterministic left-to-right order and padded on
  the right for final partial chunks.
- The feature-length tensor continues to carry the real copied-frame count, so
  padded frames are not advertised as real input.

Use the encoder input metadata and Sherpa model config to define chunk behavior.

Implementation direction:

1. Treat fixed encoder chunk dimension, for example 39 frames, as a required
   chunk size unless the model explicitly accepts variable length.
2. Accumulate online fbank frames until a full encoder chunk is available for
   normal streaming inference.
3. Do not repeatedly run the encoder on underfilled chunks during live input.
4. For final input, pad only the final partial chunk and set the length tensor to
   the number of real frames.
5. Ensure frame ordering is left-aligned or right-aligned according to Sherpa
   reference behavior, not by heuristic copying.
6. Decode only encoder frames marked valid by the model length output.

Exit gate: encoder invocations use deterministic chunk sizes and only one final
partial chunk is padded.

### Step 5: Verify sample scaling and fbank options against Sherpa reference

Status: implemented.

Implementation notes:

- Added the diagnostic environment switch
  `BLAZECLAW_SHERPA_FBANK_SAMPLE_SCALING`.
- Default, `default`, and `reference` keep the current Sherpa/FunASR-style
  `sample * 32768` path and report `reference_kaldi_int16`.
- `normalized`, `normalized_float`, `float`, `none`, and `unscaled` feed the
  normalized BlazeClaw ring-buffer samples directly into `OnlineFbank` and
  report `normalized_float`.
- The switch only changes frontend amplitude for diagnostics; it does not add
  fallback transcripts, token rescue logic, or workflow-specific behavior.
- The selected mode is exposed in `SpeechRecognitionDebugInfo`,
  `gateway.speech.debug.snapshot`, sampled `[SherpaContract]` TRACE output, and
  persisted `*.sherpa-baseline.json` files.
- Baseline JSON now also records first/last feature-frame stats and top joiner
  tokens so A/B runs can compare frame count, feature mean/range, top tokens,
  decoded token count, and decoded text from the same finite clip.

Run an A/B diagnostic switch for frontend amplitude only; keep it diagnostic
until a clear winner is proven.

Compare:

1. current `sample * 32768` input into `OnlineFbank`,
2. normalized float samples without scaling,
3. any scaling required by the Sherpa reference implementation for this exact
   model.

For each mode, compare:

- fbank frame count,
- feature mean/range,
- top joiner tokens,
- decoded token count,
- decoded text.

Exit gate: the chosen scaling mode matches reference features or restores
non-blank token emission without fallback logic.

### Step 6: Correct state-cache initialization from model outputs/config

Status: implemented.

Implementation notes:

- Each mapped encoder state cache now records input shape, output shape,
  element type, static element counts, dynamic-shape flags, normalized cache
  name, and cache index in the load-time cache map trace.
- Dynamic cache input shapes are resolved from the mapped output shape when the
  output contract is static. If both sides remain dynamic, the existing runtime
  cache size is used only when it can resolve a single dynamic dimension.
- First-use cache buffers are initialized from the resolved model-contract shape,
  not by replacing every dynamic dimension with `1`.
- Encoder output cache updates validate the actual output element count against
  the model output shape before refreshing the cached tensor.
- Cache initialization/update contract failures now fail the Sherpa inference
  path with `InferenceFailed` diagnostics instead of silently skipping the bad
  cache update.
- Debug snapshots and persisted baseline JSON expose validated update counts,
  contract failure counts, the cache contract summary, and the last cache
  contract error.

Audit every encoder state input/output pair:

1. Record input shape, output shape, element type, and element count.
2. For dynamic input dimensions, derive initial cache allocation from the mapped
   output shape or Sherpa config instead of replacing `-1` with 1 blindly.
3. Validate each cache update has the expected element count.
4. Fail loudly in diagnostics when a cache tensor cannot be initialized or
   updated according to its mapped contract.

Exit gate: cache tensors are sized from the model contract and cache updates are
stable across the whole utterance.

### Step 7: Verify decoder and joiner tensor contracts

Status: implemented.

Implementation notes:

- Decoder token input shapes now resolve from decoder ONNX input metadata and
  the active decoder context size, with element-count validation before session
  execution.
- Decoder output slicing now records the selected vector offset, vector size,
  and total element count so the extracted context/time position is visible in
  diagnostics.
- Joiner encoder and decoder inputs now resolve from joiner ONNX input metadata
  instead of hard-coded rank assumptions, while preserving `[1,dim]` and
  `[1,1,dim]` model contracts as declared by the exported model.
- Joiner output handling validates the vocabulary axis from the output shape,
  records the logits slice used for argmax, and keeps top-token IDs/scores in
  debug snapshots for reference-runtime comparison.
- Decoder/joiner contract failures now fail the Sherpa inference path with
  explicit diagnostics instead of silently continuing with an ambiguous tensor
  shape.
- Debug snapshots and baseline JSON expose decoder input shape, decoder vector
  slice, joiner input/output shapes, logits slice, validated call count,
  contract failure count, contract summary, and last contract error.

Compare BlazeClaw decoder/joiner invocations against a known-good Sherpa runtime
or the exported ONNX model metadata.

Implementation direction:

1. Confirm decoder context initialization uses the correct blank/SOS IDs and
   context size.
2. Confirm decoder input shape is exactly what the decoder model expects.
3. Confirm decoder output vector slicing uses the correct time/context position.
4. Confirm joiner encoder input shape and decoder input shape match the joiner
   model, for example `[1,512]` versus `[1,1,512]`.
5. Confirm joiner output vocabulary axis and argmax offset are correct.

Exit gate: selected frames produce the same top token IDs/scores as the
reference decoder, or any remaining delta is documented.

### Step 8: Restore non-blank regression behavior

Status: implemented.

Implementation notes:

- `tools/compare_sherpa_baseline.py` now supports
  `--require-step8-pass` and optional `--debug-log` input. The gate evaluates
  the exact Step 8 pass conditions from baseline JSON plus gateway telemetry.
- Persisted Sherpa baseline JSON now records `finalOutcome`, `hasSegment`, and
  `fallbackUsed` after native segment finalization so a rerun can prove the
  transcript came from Sherpa tokens rather than a fallback handoff.
- Automated coverage in `SherpaStep8BaselineToolTests.cpp` accepts a native
  non-blank `final_transcript` fixture and rejects the all-blank
  `no_tokens_emitted` signature.
- The historical frozen artifact remains useful regression evidence: it already
  demonstrates non-blank token emission and `hasSegment=true`, but predates
  `finalOutcome` persistence, so rerun the baseline capture to satisfy the full
  Step 8 gate.

Run the fixed engine on the frozen `请讲一个笑话` baseline.

Pass conditions:

1. `sherpaDecodedTokenCount > 0`,
2. `sherpaBlankTokenCount < sherpaJoinerCallCount`,
3. `sherpaFinalOutcome="final_transcript"`,
4. `sherpaDecodedText` is non-empty,
5. gateway emits `hasSegment=true`,
6. user-visible transcript is produced without fallback.

Exit gate: the app produces a Sherpa-native transcript for the utterance and
`python tools/compare_sherpa_baseline.py --baseline <runId>.sherpa-baseline.json --debug-log <debug-log.txt> --require-step8-pass`
returns success.

### Step 9: Compare with reference output

Status: implemented.

Implementation notes:

- `tools/compare_sherpa_baseline.py` now normalizes reference aliases for final
  timing and drain-state fields, including sequence start/end, cursor next,
  final remaining samples, final fbank flush, final drain completion, final
  outcome, latency, and loop counts.
- Comparison JSON now includes structured `finalTiming` and `finalDrainState`
  sections in addition to frame, token, token-piece, and decoded-text deltas.
- Sherpa baseline JSON now persists `finalRemainingSamples` and
  `finalDrainComplete` so Step 9 reports can prove whether a finite clip drained
  to `sequenceEnd` before comparing text quality.
- Automated Step 9 coverage exercises both a matched BlazeClaw/reference fixture
  and a fixture with measurable text, token, timing, and drain-state deltas.

Use `tools/compare_sherpa_baseline.py` to compare BlazeClaw output against a
known-good reference result for the same clip.

Compare:

1. fbank frame count,
2. encoder frame count,
3. token IDs,
4. token pieces,
5. decoded text,
6. final timing and drain state.

Exit gate: `python tools/compare_sherpa_baseline.py --baseline <runId>.sherpa-baseline.json --reference <reference.json> --output <comparison.json>`
produces structured frame, token, text, final timing, and final drain-state
deltas, so remaining quality differences are measurable and no longer present as
a no-output regression.

### Step 10: Retire only noisy diagnostics

Status: implemented.

Implementation notes:

- High-volume runtime TRACE output for chunk energy, online fbank frame polling,
  sampled per-joiner contract top-token dumps, and per-emitted-token messages is
  now opt-in via `BLAZECLAW_SHERPA_VERBOSE_TRACE=1`.
- Structured diagnostics remain enabled: `gateway.speech.debug.snapshot`,
  baseline JSON, final outcome classification, cache binding/update counts,
  contract failure counts, and top-level feature/encoder/decoder/joiner contract
  shapes are retained.
- Load-time binding/cache-map TRACE output is retained because it is bounded by
  model metadata rather than per-frame runtime volume.
- Step 10 coverage verifies the retained bounded Sherpa debug fields and the
  verbose trace gate.

After non-blank output is restored and reference comparison is acceptable:

1. keep minimal failure telemetry for future regressions,
2. remove high-volume per-frame traces,
3. keep final outcome classification,
4. keep cache binding counts and top-level model-contract diagnostics.

Exit gate: the runtime remains diagnosable without flooding debug output; verbose
per-frame/token traces require `BLAZECLAW_SHERPA_VERBOSE_TRACE`.

## Validation checklist

- Build passes in Debug x64.
- Finite `pcm_stream` request drains to `sequenceEnd`.
- Final fbank flush runs exactly once for a finite request.
- `sherpaSpeechActive=true` for the test utterance.
- Encoder and joiner run without ONNX exceptions.
- At least one non-blank token is emitted.
- BPE decoded text is non-empty.
- Gateway emits a final segment.
- No fallback transcript path is used.

## Definition of done

This regression is fixed when the same `请讲一个笑话` utterance produces a
Sherpa-native non-empty transcript through the realtime `pcm_stream` path, and
telemetry shows the result came from non-blank model token emission rather than a
fallback, rescue path, or synthetic transcript.
