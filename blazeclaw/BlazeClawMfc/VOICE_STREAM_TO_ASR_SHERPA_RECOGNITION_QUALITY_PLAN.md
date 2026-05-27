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
- Preserved the existing `pendingSamples` remainder mechanism and passed the
  finite-stream final condition into `OnlineFbank::InputFinished()`.
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

Status: planned

Target files:

- local diagnostic scripts/tools to be selected during implementation
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

Exit criteria:

- BlazeClaw output is close to the reference decoder for the baseline clip.
- Any remaining difference is documented with evidence.
- The user-facing realtime transcript is stable enough for normal use.

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
