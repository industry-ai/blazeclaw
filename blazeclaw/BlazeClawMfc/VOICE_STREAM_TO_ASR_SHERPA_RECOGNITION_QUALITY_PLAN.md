# Sherpa Zipformer recognition-quality improvement plan

## Goal

Improve recognition quality for
`sherpa-onnx-streaming-zipformer-bilingual-zh-en` in BlazeClaw realtime STT by
making the native Sherpa path match the model contract used by Sherpa/FunASR
streaming runtimes.

The current pipeline already reaches the model and emits tokens, but the text is
low quality and repetitive. This plan targets the remaining contract mismatch,
not transport, UI, or lifecycle plumbing.

## Current evidence

A spoken phrase such as `请讲一个笑话` can currently decode as unrelated repeated
pieces such as `个笑笑笑笑傲江湖火花华海黄瓜明日 N`. This indicates:

- audio reaches the streaming engine,
- encoder/decoder/joiner ONNX sessions run,
- the joiner emits non-blank tokens,
- the remaining failure is quality caused by model-contract mismatch.

The existing implementation still differs from Sherpa-equivalent streaming in
four critical areas:

1. Feature extraction is only an approximation of Kaldi/Sherpa online fbank.
2. Encoder streaming state/cache binding is inferred heuristically from ONNX
   names and shapes.
3. Greedy decoding emits at most one non-blank token per encoder frame.
4. Token reconstruction concatenates `tokens.txt` pieces directly instead of
   decoding with the model's BPE/SentencePiece artifact.

## FunASR references to use

Use the local `FunASR/` repo as behavioral reference, especially for online
frontend and stream-state discipline:

- `FunASR/runtime/onnxruntime/src/paraformer-online.cpp`
  - `FbankKaldi(...)` merges `input_cache_` into the current waveform chunk.
  - It computes complete frames, saves remaining waveform samples back into
	`input_cache_`, scales samples by `32768`, calls
	`knf::OnlineFbank::AcceptWaveform(...)`, then reads
	`NumFramesReady()` / `GetFrame(...)`.
  - `ExtractFeats(...)` carries `reserve_waveforms_` and
	`lfr_splice_cache_` across calls and handles `input_finished` flushing.
- `FunASR/runtime/onnxruntime/src/fsmn-vad-online.cpp`
  - Uses the same online fbank/cache pattern for VAD streaming.
- `FunASR/runtime/onnxruntime/third_party/kaldi-native-fbank/kaldi-native-fbank/csrc/online-feature.h`
  - Defines `OnlineFbank`, `AcceptWaveform(...)`, `InputFinished()`,
	`NumFramesReady()`, `IsLastFrame(...)`, and `GetFrame(...)` semantics.
- `FunASR/runtime/onnxruntime/third_party/kaldi-native-fbank/kaldi-native-fbank/csrc/feature-window.h`
  - Confirms Kaldi-style frame extraction defaults: 16 kHz sample rate,
	25 ms frame length, 10 ms shift, Povey window, 0.97 preemphasis,
	`snip_edges=true`, and online frame-ready behavior.
- `FunASR/runtime/onnxruntime/third_party/kaldi-native-fbank/kaldi-native-fbank/csrc/feature-fbank.h`
  - Confirms `FbankOptions`, `FrameExtractionOptions`, `MelBanksOptions`, and
	fbank dimension configuration.
- `FunASR/runtime/websocket/bin/websocket-server.cpp`
  - Shows per-connection decoder/session lifecycle, default 16 kHz PCM
	assumptions, `is_eof` handling, and final cleanup responsibilities.

These files are not a direct drop-in for BlazeClaw because the model is a
Sherpa transducer, but they document the operational rules BlazeClaw should
match: exact online frontend behavior, explicit stream state, final flush, and
stable decoder lifecycle.

## Target architecture

BlazeClaw should keep its current native architecture:

- `AudioRingBuffer` remains the in-memory realtime audio source.
- `StreamingAudioSourceRegistry` remains the source lookup mechanism.
- `SherpaZipformerStreamingEngine` remains the Sherpa-specific runtime engine.
- Gateway/WebView telemetry remains the observation layer.

The internal Sherpa engine should change from a hand-approximated decoder into a
contract-driven streaming runtime:

1. `AudioRingBuffer` chunks are fed into an online fbank frontend that preserves
   waveform remainder and flushes on final input.
2. Encoder inputs, cache inputs, cache outputs, length outputs, decoder inputs,
   and joiner inputs are mapped from the exact model metadata contract.
3. RNN-T greedy search follows the standard per-frame inner loop.
4. Output token IDs are decoded through `bpe.model` / SentencePiece-compatible
   logic and only fall back to `tokens.txt` for diagnostics.

## Phase 1: Establish a quality baseline and reproducible test clip

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- developer-selected 16 kHz mono PCM test clip captured through the normal
  `pcm_stream` recording path

Plan:

1. Capture one or more short 16 kHz mono PCM clips with known transcripts, for
   example `请讲一个笑话`.
2. Add a repeatable debug path that runs the same clip through the Sherpa engine
   without relying on microphone timing.
3. Persist per-run diagnostics:
   - sample rate,
   - chunk sizes,
   - fbank frame count,
   - encoder frame count,
   - emitted token IDs,
   - emitted token pieces,
   - decoded text,
   - final flush state.
4. Keep the current bad output as the regression baseline.

Outcome:

- Added Phase 1 baseline fields to `SpeechRecognitionDebugInfo` and the
  `gateway.speech.debug.snapshot` telemetry event.
- The Sherpa streaming engine now records sample rate, chunk size, stream range,
  cursor position, final-flush state, emitted token IDs, emitted token pieces,
  decoded text, and optional expected text for every transcribe result.
- Setting `BLAZECLAW_SHERPA_BASELINE_DIR` persists a deterministic JSON baseline
  file named `<runId>.sherpa-baseline.json` when a finite `pcm_stream` request is
  drained to final input.
- Setting `BLAZECLAW_SHERPA_BASELINE_EXPECTED_TEXT` stores the known transcript
  beside the decoded output, for example `请讲一个笑话`.
- The baseline path does not change the Sherpa decoding algorithm; it only makes
  the current quality problem reproducible and measurable for later phases.

Usage:

1. Set `BLAZECLAW_SHERPA_BASELINE_DIR` to a writable diagnostics directory.
2. Optionally set `BLAZECLAW_SHERPA_BASELINE_EXPECTED_TEXT` to the known clip
   transcript.
3. Record or replay the same short 16 kHz mono PCM phrase through the existing
   Sherpa `pcm_stream` path.
4. Save the emitted JSON file as the regression baseline before Phase 2 changes.

Exit criteria:

- The bad recognition is reproducible from a fixed clip.
- Each later phase can be validated against the same input.
- The persisted baseline contains sample rate, chunk sizes, fbank/encoder frame
  count, token IDs, token pieces, decoded text, and final-flush state.

## Phase 2: Replace hand-written fbank with kaldi-native-fbank online frontend

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `BlazeClawMfc.vcxproj`
- local reference:
  `FunASR/runtime/onnxruntime/third_party/kaldi-native-fbank/kaldi-native-fbank/csrc/*`

Plan:

1. Integrate or vendor the same `kaldi-native-fbank` implementation already
   present under `FunASR/`.
2. Use `knf::OnlineFbank` instead of `BuildLogMel(...)` for Sherpa streaming.
3. Configure fbank from the model contract and FunASR/Sherpa defaults:
   - 16 kHz sample rate,
   - 25 ms frame length,
   - 10 ms frame shift,
   - Povey window,
   - 0.97 preemphasis,
   - `snip_edges=true`,
   - expected mel-bin dimension for the loaded encoder.
4. Preserve online waveform remainder like FunASR:
   - merge previous incomplete samples before extraction,
   - only process complete frames,
   - save remainder samples for the next chunk,
   - call final flush behavior when input is complete.
5. Confirm whether Sherpa expects raw float samples in `[-1, 1]` or
   Kaldi-style scaled samples as used by FunASR (`sample * 32768`), then use one
   convention consistently and document it.

Outcome:

- Added the local FunASR `kaldi-native-fbank` include root to the MFC Debug and
  Release x64 project settings.
- Added the required `kaldi-native-fbank` source files to `BlazeClawMfc.vcxproj`
  with precompiled headers disabled:
  - `feature-fbank.cc`,
  - `feature-functions.cc`,
  - `feature-window.cc`,
  - `fftsg.c`,
  - `mel-computations.cc`,
  - `online-feature.cc`,
  - `rfft.cc`.
- Replaced the active Sherpa `BuildLogMel(...)` path with
  `knf::OnlineFbank`.
- Configured the frontend with the intended Sherpa/FunASR-compatible settings:
  - 16 kHz input sample rate from the stream contract,
  - 25 ms frame length,
  - 10 ms frame shift,
  - Povey window,
  - 0.97 preemphasis,
  - DC offset removal,
  - power-of-two padded FFT,
  - `snip_edges=true`,
  - log-power fbank output,
  - 80 mel bins for the current encoder path.
- Step 3 of the no-output root-cause plan superseded the temporary
  `pendingSamples` reconstruction path: each active Sherpa stream now owns a
  persistent `SherpaOnlineFbankFrontend`, feeds only newly read samples into
  `AcceptWaveform(...)`, tracks consumed `NumFramesReady()` frames, and calls
  `InputFinished()` once on finite final input.
- New online fbank frames are buffered as feature frames until the encoder path
  consumes them; no code now maps consumed feature frames back to
  `featureFrames * 160` retained waveform samples.
- Step 4 of the no-output root-cause plan now enforces deterministic encoder
  chunk assembly: fixed encoder time dimensions from ONNX metadata are treated
  as required chunk sizes, streaming inference waits for full chunks, pending
  full chunks are drained before more audio is read, and final finite input pads
  only one remaining partial chunk while preserving the real frame count in the
  feature-length tensor.
- Step 5 of the no-output root-cause plan now adds a diagnostic-only frontend
  amplitude switch. Set `BLAZECLAW_SHERPA_FBANK_SAMPLE_SCALING=normalized_float`
  or `normalized` to feed normalized BlazeClaw samples directly into
  `OnlineFbank`; leave it unset or set `reference`/`default` to keep the
  FunASR-observed `sample * 32768` path. Debug snapshots, `[SherpaContract]`
  traces, and persisted baseline JSON report the selected scaling mode plus
  feature stats and joiner top-token diagnostics for A/B comparison.
- Step 6 of the no-output root-cause plan now makes encoder state-cache
  initialization model-contract driven. Each mapped state input/output pair
  records input/output shapes and element counts; dynamic input shapes resolve
  from the mapped output contract or existing cache size; updates validate actual
  output element counts; and contract failures now surface as explicit Sherpa
  inference diagnostics instead of silent cache-skip behavior.
- Step 7 of the no-output root-cause plan now verifies decoder and joiner tensor
  contracts at runtime. Decoder token input shapes are derived from decoder ONNX
  metadata and context size, decoder output slicing records the selected vector
  window, joiner encoder/decoder inputs preserve the exported model rank, and
  joiner logits slicing records the vocabulary axis and top-token scores for
  reference comparison.
- Adopted the FunASR-observed sample convention for this phase: BlazeClaw ring
  samples are treated as normalized floats and scaled by `32768` before
  `OnlineFbank::AcceptWaveform(...)`.

Exit criteria:

- Feature frame counts match Kaldi/Sherpa expectations for the same waveform.
- No hand-written FFT/mel/window code remains on the Sherpa path except behind a
  temporary diagnostic switch.
- Recognition output improves or changes in a direction consistent with the
  reference decoder.
- Build validation passes with `kaldi-native-fbank` compiled into the MFC app.

## Phase 3: Make streaming cache/state binding model-contract driven

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Plan:

1. Read and persist exact ONNX input/output metadata at model load:
   - names,
   - element types,
   - ranks,
   - static dimensions,
   - dynamic dimensions,
   - input/output ordering.
2. Classify tensors by the Sherpa streaming transducer contract, not by broad
   heuristics:
   - feature input,
   - feature length input,
   - encoder states/caches,
   - processed length/state tensors,
   - decoder token context input,
   - decoder output embedding,
   - joiner encoder projection,
   - joiner decoder projection,
   - logits output.
3. Build a deterministic state table from encoder cache inputs to matching cache
   outputs.
4. Initialize every cache with the exact expected shape and type.
5. Update cache tensors only from the corresponding output after a successful
   encoder invocation.
6. Treat length-like outputs as frame-validity metadata and never decode padded
   encoder frames.

Outcome:

- Extended Sherpa tensor metadata to retain exact model-order `ordinal`, element
  type, shape, dynamic-shape flag, normalized state name, state/cache flag, and
  length-like flag.
- Built explicit encoder output bindings at model load instead of keeping only
  output names.
- Classified tensors into the Sherpa streaming contract categories used by the
  current engine:
  - feature input,
  - feature length input,
  - encoder output,
  - encoder output lengths,
  - encoder state/cache tensors,
  - processed length/state tensors,
  - decoder token context input,
  - decoder output,
  - joiner logits.
- Added deterministic encoder cache mapping from each state input to its matching
  state output by normalized name and element type.
- Removed positional fallback cache refresh: encoder state caches are now updated
  only when a mapped output tensor exists and its element count is compatible
  with the mapped input tensor shape.
- Made length-like encoder outputs authoritative frame-validity metadata. If a
  model exposes a length output but no valid positive length is available, padded
  encoder frames are not decoded.
- Added diagnostics in both native TRACE output and
  `gateway.speech.debug.snapshot`:
  - `sherpaEncoderStateCacheBindingCount`,
  - `sherpaEncoderStateCacheUpdateCount`,
  - `sherpaEncoderLengthOutputCount`,
  - `sherpaEncoderLengthOutputUsed`.

Exit criteria:

- Runtime diagnostics show a stable, explicit cache mapping.
- No shape-incompatible cache update is attempted.
- Encoder valid-frame count is derived from the model outputs/metadata instead
  of guessed from tensor dimensions.
- Build validation passes with deterministic encoder cache mapping enabled.

## Phase 4: Implement standard RNN-T greedy search inner loop

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Plan:

1. Replace the current one-token-per-frame decode with the standard streaming
   RNN-T greedy loop:
   - for each encoder frame,
   - run joiner with the current decoder state,
   - if blank is emitted, advance to the next frame,
   - if non-blank is emitted, append token, update decoder context, rerun
	 decoder, and retry the same encoder frame,
   - stop after blank or `max_symbols_per_frame`.
2. Use blank ID, context size, SOS/EOS handling, and unk handling from the model
   token contract.
3. Add guardrails:
   - `max_symbols_per_frame`,
   - max total tokens per utterance,
   - repeated-token diagnostics,
   - confidence/top-k telemetry for debugging.
4. Validate that decoder state/context updates happen only after non-blank token
   emission.

Outcome:

- Replaced the active one-token-per-frame joiner path with a bounded RNN-T greedy
  inner loop.
- Each encoder frame now runs joiner repeatedly until:
  - blank is emitted,
  - `<sos/eos>` is emitted,
  - `max_symbols_per_frame` is reached,
  - the utterance token cap is reached,
  - or the joiner output is unusable.
- Non-blank tokens are appended before decoder context is updated, and the
  decoder is rerun before retrying the same encoder frame.
- Blank and `<sos/eos>` advance to the next encoder frame without mutating the
  decoder context.
- Added guardrails:
  - `max_symbols_per_frame = 8`,
  - max total emitted tokens per utterance = 512,
  - repeated-token counter,
  - multi-symbol-frame counter,
  - max-symbol-hit counter.
- Added debug telemetry in `gateway.speech.debug.snapshot`:
  - `sherpaRnntInnerLoopCount`,
  - `sherpaRnntMaxSymbolsHitCount`,
  - `sherpaRnntRepeatedTokenCount`,
  - `sherpaRnntMultiSymbolFrameCount`,
  - `sherpaRnntMaxSymbolsPerFrame`.

Exit criteria:

- Multiple symbols can be emitted from one encoder frame when the model requires
  it.
- Repetition is bounded by an explicit max-symbol guard, not by accidental
  one-token-per-frame behavior.
- Token sequences are closer to Sherpa/FunASR reference output on the baseline
  clip.
- Build validation passes with the RNN-T inner loop enabled.

## Phase 5: Decode token IDs with BPE/SentencePiece instead of direct piece concatenation

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- model artifacts:
  `models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en/bpe.model`
  `models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en/bpe.vocab`
  `models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en/tokens.txt`

Plan:

1. Load the model's `bpe.model` when available.
2. Convert emitted token IDs to token pieces using the correct vocabulary order.
3. Decode pieces through a SentencePiece/BPE-compatible decoder.
4. Preserve `tokens.txt` direct rendering only as diagnostic output.
5. Normalize final text consistently for Chinese/English mixed output:
   - remove BPE boundary markers,
   - handle whitespace around English words,
   - keep Chinese characters unspaced unless the decoder inserts valid spaces.

Outcome:

- Added `bpe.model` and `bpe.vocab` discovery beside `tokens.txt` and exposed
  their presence through debug telemetry.
- Replaced the user-visible `TokenIdsToText(...)` path with
  `DecodeTokenIdsToText(...)`, which decodes emitted token IDs through the
  vocabulary pieces rather than directly concatenating raw `tokens.txt` text.
- The decoder now:
  - filters special tokens (`<blk>`, `<blank>`, `<sos/eos>`, `<eos>`),
  - maps `<unk>` to `?`,
  - decodes byte-fallback pieces such as `<0xE4>`,
  - normalizes SentencePiece word-boundary markers (`▁`) to spaces,
  - removes common BPE continuation markers,
  - compacts whitespace for mixed Chinese/English output.
- Raw `tokens.txt` pieces are preserved only for diagnostics through
  `JoinTokenPieces(...)` and `sherpaRawTokenPieces` /
  `sherpaBaselineTokenPieces` telemetry.
- Added debug telemetry:
  - `sherpaBpeModelPresent`,
  - `sherpaBpeVocabPresent`,
  - `sherpaDecodedText`,
  - `sherpaRawTokenPieces`.

Exit criteria:

- `TokenIdsToText(...)` no longer concatenates BPE pieces directly for final user
  text.
- Debug telemetry can show both raw token pieces and decoded text.
- Mixed Chinese/English output renders naturally.
- Build validation passes with BPE-aware decoding enabled.

## Phase 6: Align stream lifecycle with FunASR final-flush behavior

Status: completed

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/app/VoiceRecorder.*`
- `src/core/runtime/SpeechRecognition/StreamingAudioSourceRegistry.*`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Plan:

1. Treat final bounded `pcm_stream` requests as finite input, not live polling.
2. Propagate an explicit final flag into the Sherpa engine when the recorder has
   no more audio for the stream.
3. Flush the online fbank frontend on final input, equivalent to
   `OnlineFbank::InputFinished()` semantics.
4. Run any remaining encoder frames through the decoder before committing the
   final segment.
5. Emit final telemetry that distinguishes:
   - no speech detected,
   - no tokens emitted,
   - tokens emitted but empty decoded text,
   - successful final transcript.

Outcome:

- Confirmed `CVoiceRecorder::BuildStreamingAudioArtifact()` already publishes a
  bounded finite `pcm_stream` range using `sequenceStart`, `sequenceEnd`, and
  `durationMs` from `AudioRingBuffer`.
- Confirmed `StreamingAudioSourceRegistry` already supports sequence-aware reads
  plus latest/oldest sequence callbacks, so bounded requests can drain to the
  recorder-provided `sequenceEnd` without registry changes.
- Hardened `SherpaZipformerStreamingEngine` final lifecycle diagnostics by
  recording whether a request is a final stream request, whether it is still a
  live `pcm_stream`, whether the finite stream drained to `sequenceEnd`, whether
  the final fbank flush path ran, the final cursor, and any remaining samples.
- Added explicit final outcome classification in debug snapshots:
  `no_speech_detected`, `no_tokens_emitted`,
  `tokens_emitted_empty_decoded_text`, `final_transcript`,
  `live_stream_not_final`, and `finite_stream_not_drained`.
- The final fbank flush flag is now tied to the same finite-input condition that
  drives `OnlineFbank::InputFinished()` behavior in `BuildOnlineFbank(...)`, and
  final transcript telemetry uses the last decoded model output.

Exit criteria:

- Stopping recording drains finite stream requests to `sequenceEnd`; debug
  snapshots expose `sherpaFinalDrainComplete` and `sherpaFinalRemainingSamples`
  to prove this per run.
- Final fbank frames are not lost; `sherpaFinalFbankFlush` identifies the final
  `InputFinished()`-equivalent path.
- Final segment text matches the last decoded model output; debug snapshots carry
  both `sherpaDecodedText` and `sherpaFinalOutcome` for final-run validation.

## Phase 7: Compare against a reference decoder before removing diagnostics

Status: completed

Target files:

- `tools/compare_sherpa_baseline.py`
- `VOICE_STREAM_TO_ASR_SHERPA_ZIPFORMER_ENABLEMENT_PLAN.md`
- this plan file

Plan:

1. Run the same baseline clip through a known-good Sherpa/FunASR-compatible
   decoder if available locally.
2. Compare:
   - fbank frame counts,
   - token IDs,
   - token pieces,
   - decoded text,
   - final timing.
3. Keep BlazeClaw telemetry until the deltas are understood.
4. Update the enablement plan with measured results and remaining gaps.
5. Remove or reduce noisy diagnostics only after quality is validated.

Outcome:

- Added `tools/compare_sherpa_baseline.py`, a local diagnostic utility that reads
  BlazeClaw `*.sherpa-baseline.json` files and optionally compares them with a
  known-good Sherpa/FunASR reference decoder JSON file.
- The comparison extracts and reports the Phase 1-6 evidence fields needed for
  quality validation: fbank frame count, encoder frame count, decoded token
  count, token IDs, token pieces, decoded text, final flush state, final outcome,
  final drain completion, final remaining samples, latency, loop counts, and
  stream cursor/final sequence data.
- The tool accepts common reference field names such as `text`, `transcript`,
  `token_ids`, `token_pieces`, `fbank_frames`, and `encoder_frames`, so output
  from different reference runners can be normalized without changing BlazeClaw
  runtime code.
- If no local reference decoder output is available, the tool exits successfully
  with `status: reference_missing` and still extracts BlazeClaw baseline metrics;
  this keeps telemetry in place until an external reference result can be
  attached.
- Validation covered matching and different BlazeClaw/reference JSON pairs, final
  timing/drain-state comparison output, and the no-reference path.

Usage:

1. Capture a finite baseline run with `BLAZECLAW_SHERPA_BASELINE_DIR` enabled so
   BlazeClaw writes `<runId>.sherpa-baseline.json`.
2. Run the same audio through a known-good Sherpa/FunASR-compatible decoder and
   save a reference JSON file containing at least decoded text and, where
   available, token IDs/pieces and frame counts.
3. Compare the two outputs:
   `python tools/compare_sherpa_baseline.py --baseline <runId>.sherpa-baseline.json --reference reference.json --output comparison.json`
4. If the reference decoder is not available yet, run the tool without
   `--reference` to verify and archive the BlazeClaw metrics that must be
   compared later.

Exit criteria:

- BlazeClaw output can now be compared directly against a reference decoder for
  the same baseline clip with structured deltas for frame counts, token IDs,
  token pieces, and decoded text.
- Any remaining difference can be documented with the generated comparison JSON;
  diagnostics should remain enabled until those deltas are measured on a real
  reference run.
- The user-facing realtime transcript stability gate remains evidence-based: do
  not remove noisy diagnostics until the comparison report shows acceptable
	decoded-text, token/frame-count, final timing, and drain-state deltas for
  representative clips.

## Post-Phase 7 regression: all-blank output after cache-state corruption

Status: baseline freeze implemented; root-cause fix pending

Evidence from the finite `pcm_stream` utterance `请讲一个笑话` showed the stream
was not blocked in recording, lifecycle, or final flush:

- `runtimeCursorNextSequence` reached `requestSequenceEnd`.
- `sherpaFinalDrainComplete=true` and `sherpaFinalFbankFlush=true`.
- `sherpaSpeechActive=true`, so the input was not classified as silence.
- The encoder/joiner ran, but all best tokens were blank:
  `sherpaDecodedTokenCount=0`, `sherpaBlankTokenCount=216`, and
  `sherpaJoinerCallCount=216`.

Load-time binding traces then exposed the root cause: encoder state inputs such
as `cached*` and `processed_lens` were classified as state tensors but never
received unique `bufferIndex` values. As a result, every encoder state-cache
mapping used `cacheIndex=0`, so independent Zipformer cache tensors overwrote
the same per-stream cache slot.

Fix:

- `BuildTensorBindings(...)` now assigns unique cache buffer indexes to every
  encoder state tensor by element type before deterministic input/output cache
  mapping is built.
- This is a model-contract fix, not a transcript fallback or final rescue path.
- Later runtime traces confirmed cache mappings no longer all report
  `cacheIndex=0`, but the all-blank no-output signature still persists, so the
  unique-cache-index fix is necessary but not sufficient.

Step 1 baseline freeze:

- Added `tools/freeze_sherpa_no_output_baseline.py` to validate and archive the
  current all-blank regression baseline before further fixes.
- The utility checks final drain, final fbank flush, speech-active state,
  encoder/joiner activity, zero decoded tokens, and blank-token count matching
  joiner-call count.
- The frozen baseline is the evidence gate for the remaining root-cause plan in
  `VOICE_STREAM_TO_ASR_SHERPA_NO_OUTPUT_ROOT_CAUSE_PLAN.md`.

Step 2 contract diagnostics:

- Added bounded Sherpa contract diagnostics to expose feature input shape,
  feature length, real/padded frame counts, first/last feature-frame stats,
  encoder output shape and valid frame count, decoder context/output shape,
  joiner input/output shapes, and top-5 joiner token scores.
- The diagnostics are surfaced through `gateway.speech.debug.snapshot` and
  sampled `[SherpaContract]` TRACE lines so the next failing run can identify
  whether blanks originate before encoder, after encoder, or in decoder/joiner
  contract handling.

Validation target:

- Use `python tools/freeze_sherpa_no_output_baseline.py --baseline <runId>.sherpa-baseline.json --archive-dir <archive-dir>`
  to freeze the current all-blank evidence before changing the frontend,
  encoder chunking, cache shape, or decoder/joiner contracts.
- The final regression fix remains pending until runtime telemetry no longer
  ends as `no_tokens_emitted` with
  `sherpaBlankTokenCount == sherpaJoinerCallCount`.

Step 8 non-blank restoration gate:

- `tools/compare_sherpa_baseline.py` now accepts `--require-step8-pass` and an
  optional `--debug-log` argument to validate the restored non-blank behavior.
- The gate requires decoded tokens, blank-token count below joiner-call count,
  `sherpaFinalOutcome="final_transcript"`, non-empty `sherpaDecodedText`,
  gateway `hasSegment=true`, and no fallback handoff.
- Sherpa baseline JSON now persists `finalOutcome`, `hasSegment`, and
  `fallbackUsed` after native segment creation, enabling a captured
  `请讲一个笑话` rerun to prove user-visible transcript restoration without
  synthetic rescue text.
- Tool-level regression coverage accepts a native non-blank final transcript
  fixture and rejects the historical all-blank no-output signature.

Step 9 reference comparison gate:

- `tools/compare_sherpa_baseline.py` now emits structured `finalTiming` and
  `finalDrainState` sections when a known-good reference JSON is supplied.
- Reference aliases cover `sequence_start`, `sequence_end`, `cursor_next`,
  `final_remaining_samples`, `final_flush`, `final_drain_complete`,
  `final_outcome`, `latency_ms`, `loop_count`, and `max_loop_count`.
- BlazeClaw baseline JSON now persists `finalRemainingSamples` and
  `finalDrainComplete`, so a comparison report can separate remaining text/token
  quality deltas from lifecycle or finite-drain regressions.
- Step 9 tests cover both matched reference output and measurable differences in
  decoded text, token IDs/pieces, timing, and drain state.

Step 10 diagnostic retirement gate:

- High-volume Sherpa runtime TRACE output is now opt-in through
  `BLAZECLAW_SHERPA_VERBOSE_TRACE=1`.
- Gated traces include periodic chunk-energy logs, online fbank pending-frame
  logs, sampled joiner top-token contract dumps, and per-emitted-token lines.
- Retained always-on bounded diagnostics include `gateway.speech.debug.snapshot`,
  baseline JSON, final outcome classification, cache binding/update counts,
  contract failure counts, and feature/encoder/decoder/joiner shape summaries.
- Load-time binding/cache-map TRACE output remains available because it is
  bounded by model metadata and is useful for future contract regressions.
- Step 10 tests verify the retained debug fields and the verbose trace gate.

## Recommended implementation order

1. Build the reproducible baseline first.
2. Replace the frontend with `kaldi-native-fbank` online extraction.
3. Make ONNX cache/state binding deterministic.
4. Fix RNN-T greedy search.
5. Add BPE/SentencePiece text decoding.
6. Tighten final flush and lifecycle behavior.
7. Compare with a reference decoder and retire excessive diagnostics.

## Risks and mitigations

- Risk: `FunASR/` online frontend behavior may not exactly match the Sherpa
  Zipformer model.
  - Mitigation: use FunASR as a Kaldi-native online frontend reference, then
	verify settings against Sherpa model metadata and reference output.
- Risk: Adding `kaldi-native-fbank` or SentencePiece-compatible decoding changes
  build complexity.
  - Mitigation: isolate both behind small wrapper classes and keep existing
	diagnostics during migration.
- Risk: ONNX metadata alone may not expose every Sherpa cache semantic.
  - Mitigation: derive mapping from exact names/order and validate against
	generated diagnostics; prefer Sherpa metadata/config files if present.
- Risk: RNN-T inner-loop changes can increase CPU usage.
  - Mitigation: enforce `max_symbols_per_frame`, measure joiner call counts, and
	tune chunk size only after correctness is restored.

## Definition of done

The quality-improvement work is complete when:

- the same fixed Mandarin test clip no longer produces unrelated repeated text,
- fbank extraction uses the online Kaldi/Sherpa-compatible implementation,
- encoder cache/state tensors are mapped deterministically,
- greedy search supports multiple labels per encoder frame,
- final text is decoded through BPE/SentencePiece logic,
- stopped recordings flush all remaining frames,
- telemetry proves that the output path is model-derived and not a fallback.
