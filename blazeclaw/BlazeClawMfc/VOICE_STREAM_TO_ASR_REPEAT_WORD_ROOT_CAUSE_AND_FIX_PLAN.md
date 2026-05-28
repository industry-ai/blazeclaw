# Voice Stream to ASR Sherpa Repeat-Word Root-Cause Analysis and Fix Plan

## Problem statement

After speaking `写一首诗用它来描述春天的色彩`, the recognized text becomes:

`写一首诗用它来描述描述描述描述描述春天的色素色素色素色素色素色素色素色素色素色素色素色素色素色素色素色素色素彩`

The recognition result contains repeated words/phrases (`描述`, `色素`) before the
text is sent into chat.

## Corrected scope and active path

The active speech recognition path is Sherpa Zipformer, not Qwen3 ASR.

The current used config file is `BlazeClawMfc/blazeclaw.conf`, which sets:

- `speech.storageRoot=BlazeClawMfc/models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en`
- `speech.activeModelId=speech/sherpa-onnx-streaming-zipformer-bilingual-zh-en`
- `speech.streaming.enabled=true`
- `speech.streaming.chunk_ms=1500`
- `speech.streaming.lookback_ms=320`
- `speech.chunk_ms=1500`
- `speech.overlap_ms=320`

Therefore this repeat-word issue belongs to the native Sherpa streaming path in:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `web/chat/chat-controller.js`

The earlier Qwen3 ASR diagnosis is obsolete for the currently selected model.

## Relationship to the existing Sherpa no-output plan

`VOICE_STREAM_TO_ASR_SHERPA_NO_OUTPUT_ROOT_CAUSE_PLAN.md` documents the previous
Sherpa all-blank/no-output regression and its follow-up fixes.

That plan is important because the current repeat-word failure happens after the
Sherpa path has moved past the all-blank failure mode:

- audio reaches the realtime `pcm_stream` path,
- online fbank/encoder/decoder/joiner run,
- non-blank token IDs are emitted,
- BPE/token decoding produces user-visible text,
- but the RNN-T decode path can still emit degenerate repeated tokens or repeated
  short phrases.

So the new failure is not "no output". It is a Sherpa-native non-blank output
quality regression.

## Historical repeat fix to reuse

The shorter utterance `请讲一个笑话` previously produced the repeated Sherpa
output `请讲讲讲一个笑笑笑`, but it is now recognized correctly. Git history shows
the useful pattern:

- Commit `5034cf4d` (`SherpaZipformer: encoder cache, fbank, and RNN-T fixes`)
  changed the RNN-T immediate-repeat handling from diagnostics-only to an active
  suppression guard. When the best joiner token is identical to the last emitted
  token, the runtime now increments `rnntRepeatedTokenCount`, advances the
  encoder frame, and does **not** append the duplicate token or feed it back into
  `decoderContext`.
- The same commit and the existing no-output plan also record related successful
  Sherpa stability fixes: normalized-float fbank as the default, final-tail
  padding avoidance for tiny finite-stream tails, complete encoder cache input
  materialization, and retained structured diagnostics.

The key lesson is that the successful short-utterance fix stopped repetition
**before** it entered `emittedTokenIds` and **before** it was fed back into the
decoder context. The current longer utterance should reuse that pattern, but
generalize it from immediate same-token repetition to repeated multi-token
n-grams and decoded CJK phrase units.

Because `写一首诗用它来描述春天的色彩` is longer than `请讲一个笑话`, it likely
exercises more encoder chunks/frames and gives the greedy RNN-T loop more chances
to enter an alternating-token or phrase-level loop that the immediate-token guard
cannot catch.

## Code evidence

### Evidence 1: Sherpa path owns persistent streaming decode state

`SherpaZipformerStreamingEngine::StreamState` stores the active RNN-T state:

- `decoderContext`
- `emittedTokenIds`
- `baselineTokenIds`
- `onlineFbank`
- `pendingFeatureFrames`
- `decodedTokenCount`
- `joinerCallCount`
- `blankTokenCount`
- `rnntRepeatedTokenCount`
- `rnntMaxSymbolsHitCount`
- `rnntMultiSymbolFrameCount`
- `partialText`

This confirms the repeat words are generated inside the native Sherpa token path,
not by a downstream UI concatenation layer.

### Evidence 2: the RNN-T greedy loop can still generate repeated phrase patterns

In `SherpaZipformerStreamingEngine.cpp`, each valid encoder frame runs an RNN-T
inner loop up to `kSherpaMaxSymbolsPerFrame`:

- `kSherpaMaxSymbolsPerFrame = 8`
- the joiner logits are decoded by greedy `argmax`,
- non-blank/non-EOS tokens are appended to `streamState.emittedTokenIds`,
- the token is fed back into `decoderContext`,
- `updateDecoderFromContext(...)` is called again before continuing the same
  frame.

The current immediate-repeat guard, added by the historical short-utterance fix,
only suppresses the exact same token ID when it is identical to the most recently
emitted token.

Impact:

- A degenerate sequence such as `色素色素色素...` can be represented by an
  alternating multi-token pattern rather than one repeated token ID.
- Immediate token-ID suppression does not catch repeated 2-token, 3-token, or
  decoded-substring loops.
- The loop can still emit several non-blank symbols per encoder frame before
  hitting blank/EOS or `kSherpaMaxSymbolsPerFrame`.

### Evidence 3: partial text is rebuilt from all emitted Sherpa token IDs

After each inference pass, the engine does:

- `streamState.partialText = DecodeTokenIdsToText(streamState.emittedTokenIds)`
- `result.text = streamState.partialText`

Final segments use the same `partialText`.

Impact:

- Once repeated tokens enter `emittedTokenIds`, the final transcript naturally
  contains the repeated words.
- The coordinator/gateway/UI are exposing the native Sherpa output rather than
  inventing the repeated content.

### Evidence 4: downstream transport forwards one normalized transcript

The downstream layers do not appear to create the repeated words:

- `SpeechTranscriptionCoordinator.cpp` selects `result.sessionState.transcriptText`
  or `result.text`.
- `GatewayHost.Handlers.Runtime.SpeechRecognition.cpp` computes `normalizedText`
  once and exposes it through `speechSession`, `executionState`, and
  `speechArtifact`.
- `web/chat/chat-controller.js` sends the accepted transcript once through
  `sendPayload(...)`.

Impact:

- The repeated words are already in the native recognition result before
  `chat.send`.
- The fix must focus on Sherpa decoding/state handling first, then add frontend
  containment only as a safety net.

### Evidence 5: frontend quality gate does not block repeated CJK phrases

`web/chat/chat-controller.js::assessTranscriptQuality(...)` detects:

- long runs of the same character,
- compact single-character repetition,
- punctuation-heavy patterns,
- mojibake-heavy patterns,
- placeholder/control-token patterns.

It does not detect repeated CJK word/phrase units such as:

- `描述描述描述描述描述`
- `色素色素色素色素色素...`

Impact:

- Even when Sherpa emits a visibly degenerate transcript, the WebView gate may
  accept and forward it to chat.

## Root cause

The primary root cause is an incomplete Sherpa RNN-T repetition guard. The engine
now emits non-blank tokens, but its greedy joiner loop only suppresses immediate
identical token IDs. It does not detect repeated decoded subwords, repeated
multi-token n-grams, or repeated CJK word/phrase units.

The previous `请讲讲讲一个笑笑笑` issue was fixed by suppressing repeated tokens
before they were appended or fed back to the decoder. The current longer utterance
appears to be the same class of failure at a larger granularity: repeated decoded
phrases such as `描述` and `色素` can be formed by more than one token or across
more than one encoder frame, so the existing one-token guard is not sufficient.

The latest Phase 1 baseline run narrows this further: the current longer
utterance produced `写一首是用他来描述春天的色彩色彩色彩色彩色彩`, and the
classifier identified a repeated two-token unit `1251 768` decoded as `色彩`
with count 5 and coverage 0.455. This confirms the immediate same-token guard is
working, but an alternating/multi-token RNN-T loop still escapes it.

The likely contributing causes are:

1. **RNN-T inner-loop over-emission per encoder frame.**
   `kSherpaMaxSymbolsPerFrame=8` allows multiple non-blank symbols to be emitted
   for a single encoder frame. If the decoder/joiner enters a local loop, several
   repeated units can be appended before the frame advances.
2. **Only same-token repeats are suppressed.**
   The existing guard catches `A A` token repetition, but not `A B A B A B` or a
   decoded unit such as `色素` repeated many times.
3. **Decoder context feedback can reinforce the loop.**
   Each accepted non-blank token is pushed into `decoderContext`, and the decoder
   is rerun from that context. A bad token can bias the next joiner call toward a
   repeated token sequence.
4. **No decoded-text stability check exists before segment finalization.**
   `partialText` is accepted once token decoding succeeds. There is no runtime
   quality gate for repeated decoded units.
5. **Frontend containment is too weak for CJK phrase repetition.**
   The WebView quality gate catches character-level or mojibake failures, but not
   repeated words/phrases.

## Non-root causes ruled out

- **Qwen3 ASR chunk stitching:** obsolete for the current active model; current
  config selects Sherpa Zipformer.
- **Gateway response construction:** gateway selects and returns one normalized
  transcript; it does not append the same transcript repeatedly.
- **WebView event handling:** speech lifecycle updates replace speech state and
  do not build the final transcript by repeated concatenation.
- **Chat send pipeline:** bad text exists before the transcript reaches
  `chat.send`.
- **No-output/all-blank failure:** this report has the opposite signature: tokens
  are emitted, decoded, and user-visible, but token quality is degenerate.

## Fix plan

### Phase 1: Freeze a Sherpa repeat-word baseline

Status: implemented.

Implementation notes:

- `tools/compare_sherpa_baseline.py` now classifies repeat patterns in every
  baseline comparison output under `repeatClassification`.
- The classifier detects:
  - repeated token n-grams from `tokenIds`,
  - repeated decoded CJK phrase units of 2-6 characters,
  - long repeated CJK character runs,
  - existing RNN-T counters such as `rnntRepeatedTokenCount`,
    `rnntMaxSymbolsHitCount`, and `rnntMultiSymbolFrameCount`.
- The tool supports `--require-no-repeat`, which returns a non-zero exit code if
  a degenerate token or decoded CJK repeat pattern is detected.
- Sherpa baseline JSON now persists RNN-T repeat counters and decoded repeat
  fields:
  - `rnntInnerLoopCount`,
  - `rnntMaxSymbolsHitCount`,
  - `rnntRepeatedTokenCount`,
  - `rnntMultiSymbolFrameCount`,
  - `rnntMaxSymbolsPerFrame`,
  - `decodedRepeatDegenerate`,
  - `decodedRepeatUnit`,
  - `decodedRepeatUnitLength`,
  - `decodedRepeatUnitCount`,
  - `decodedRepeatUnitCoverage`,
  - `decodedLongestRepeatedChar`,
  - `decodedLongestRepeatedCharRun`.
- `SherpaStep8BaselineToolTests.cpp` now covers both a clean CJK baseline and a
  repeated CJK phrase baseline through the `--require-no-repeat` gate.

Latest captured baseline result:

- Historical control baseline:
  - file: `bin/Debug/BlazeClawRecordings/web-1779957233191-1144.sherpa-baseline.json`
  - expected text: `请讲一个笑话`
  - decoded text: `请讲一个笑话`
  - token count: 6
  - fbank frames: 64
  - final flush: `true`
  - final outcome: `final_transcript`
  - repeat classification: `ok`
- Current longer utterance baseline:
  - file: `bin/Debug/BlazeClawRecordings/web-1779957261620-9748.sherpa-baseline.json`
  - expected text: `写一首诗用它来描述春天的色彩`
  - decoded text: `写一首是用他来描述春天的色彩色彩色彩色彩色彩`
  - token count: 22
  - fbank frames: 128
  - final flush: `true`
  - final outcome: `final_transcript`
  - repeat classification: `degenerate_repeat_detected`
  - token repeat: length 2, count 5, unit `['1251', '768']`
  - decoded CJK repeat: unit `色彩`, count 5, coverage 0.455

Interpretation:

- The historical control proves the previous immediate-token repeat fix still
  works for `请讲一个笑话`.
- The longer utterance proves this is no longer a no-output or transport issue:
  the Sherpa path emits a native final transcript, but the final transcript is
  quality-degenerate.
- The repeated unit is now measured precisely: the failing run repeats token
  n-gram `1251 768`, decoded as `色彩`, five times.
- The next implementation phase should focus on suppressing repeated token
  n-grams before they are appended to `emittedTokenIds` or fed back into
  `decoderContext`.

Usage:

1. Start BlazeClaw with the active Sherpa config in `BlazeClawMfc/blazeclaw.conf`.
2. Keep `BLAZECLAW_SHERPA_BASELINE_DIR=BlazeClawRecordings` enabled.
3. Capture the historical control utterance `请讲一个笑话` and the current longer
   utterance `写一首诗用它来描述春天的色彩` through the normal realtime
   `pcm_stream` path.
4. Run the classifier on each persisted baseline:

   `python tools/compare_sherpa_baseline.py --baseline <runId>.sherpa-baseline.json --require-no-repeat --output <comparison.json>`

5. Compare each output's `repeatClassification` section to verify where the
   longer utterance diverges from the corrected historical control.

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `tools/compare_sherpa_baseline.py`

Plan:

1. Use the active Sherpa config from `BlazeClawMfc/blazeclaw.conf`.
2. Keep `BLAZECLAW_SHERPA_BASELINE_DIR=BlazeClawRecordings` enabled.
3. Capture both utterances through the normal realtime `pcm_stream` path:
   - historical control: `请讲一个笑话`,
   - current failing case: `写一首诗用它来描述春天的色彩`.
4. Use the historical control to confirm the existing immediate-token guard still
   keeps the short utterance correct.
5. Capture the current utterance `写一首诗用它来描述春天的色彩` through the normal
   realtime `pcm_stream` path.
6. Persist the baseline JSON with:
   - decoded text,
   - token IDs,
   - token pieces,
   - raw repeated token diagnostics,
   - `rnntRepeatedTokenCount`,
   - `rnntMaxSymbolsHitCount`,
   - `rnntMultiSymbolFrameCount`,
   - top joiner token snapshots,
   - final drain/fbank/segment state.
7. Add a repeat-word classification mode to `compare_sherpa_baseline.py` or a new
   helper script that detects repeated decoded CJK units.
8. Compare the control and failing baselines, especially:
   - whether the short utterance only triggers immediate-token repeats,
   - whether the long utterance triggers multi-token or decoded-unit repeats,
   - whether the long utterance has more `rnntMaxSymbolsHitCount`,
     `rnntMultiSymbolFrameCount`, or final-tail padding.

Exit gate:

- Persisted Sherpa baselines prove why the old short utterance is now correct and
  where the longer utterance first diverges: token IDs, token pieces, decoded
  text, or final text normalization.
- `--require-no-repeat` passes for the corrected historical control and fails for
  a baseline containing repeated CJK phrase output like the reported transcript.

Result: satisfied by the captured baselines above. Phase 1 confirms the current
failure signature is a repeated two-token Sherpa RNN-T loop (`1251 768`) decoded
as repeated `色彩`, not a Qwen fallback, no-output regression, or UI append bug.

### Phase 2: Add runtime repeated-unit diagnostics

Status: implemented.

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h`
- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Plan:

1. Extend `StreamState` / debug info with repeated-unit diagnostics:
   - repeated token n-gram length,
   - repeated token n-gram count,
   - repeated token n-gram unit,
   - repeated decoded UTF-8 unit,
   - repeated decoded unit length,
   - repeated decoded unit count,
   - repeated decoded unit coverage,
   - repeat guard action.
2. Analyze repetition after every accepted non-blank token by checking recent
   token history for repeated n-grams of length 2-8.
3. Reuse the decoded partial transcript for UTF-8/CJK phrase-repeat diagnostics
   after each inference pass, avoiding an extra full-token decode inside the
   inner RNN-T loop.
4. Include these fields in `gateway.speech.debug.snapshot` and persisted baseline
   JSON.

Implementation notes:

- `StreamState` now tracks:
  - `repeatedTokenNgramLength`,
  - `repeatedTokenNgramCount`,
  - `repeatedTokenNgramUnit`,
  - `repeatedDecodedUnit`,
  - `repeatedDecodedUnitLength`,
  - `repeatedDecodedUnitCount`,
  - `repeatedDecodedUnitCoverage`,
  - `repeatGuardAction`.
- `SpeechRecognitionDebugInfo` exposes the same fields with `sherpa...` names.
- After each accepted non-blank token, the runtime checks the recent emitted-token
  tail for repeated n-grams of length 2-8 and records the strongest trailing
  repeat found.
- After each partial transcript decode, the runtime records decoded CJK repeated
  unit diagnostics using the existing decoded-repeat classifier.
- `repeatGuardAction` is currently `diagnostic_only`; Phase 2 does not suppress,
  rewrite, or hold transcript output.
- Persisted Sherpa baseline JSON and `gateway.speech.debug.snapshot` now include
  these diagnostics so a failing run can identify both the repeated token unit and
  the repeated decoded unit. Gateway telemetry exposes decoded repeat coverage as
  `sherpaRepeatedDecodedUnitCoveragePermille` because the existing gateway JSON
  number helper is integer-only; persisted baseline JSON keeps the decimal
  `repeatedDecodedUnitCoverage` field.

Exit gate:

- A failing run reports exactly which repeated token/decoded unit caused the
  transcript to degenerate.

Result: implemented. A repeat run should now surface the escaped pattern through
runtime telemetry and baseline JSON, for example token n-gram `1251 768` and
decoded unit `色彩`, while leaving the transcript acceptance behavior unchanged
until Phase 3 and Phase 4 add guards.

### Phase 3: Generalize the historical RNN-T repeat guard to n-grams

Status: implemented.

Target file:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`

Plan:

1. Preserve the existing immediate same-token guard from commit `5034cf4d`.
2. Apply the same successful suppression pattern to n-grams: when a repeat is
   detected, suppress before appending the token and before feeding it back into
   `decoderContext`.
3. Add a generic repeated token n-gram guard for recent history:
   - detect repeated n-grams of length 2-8,
   - require at least 3 consecutive repeats before suppressing,
   - keep thresholds conservative to avoid blocking legitimate short repeated
     speech.
4. When the guard fires:
   - increment a diagnostic counter,
   - record the repeated unit,
   - do not append the candidate token,
   - advance the current encoder frame or force a blank-equivalent transition.
5. Do not clear the whole utterance state unless diagnostics show the loop
   persists across many frames.
6. Make thresholds constants first; only expose config after baseline validation.

Implementation notes:

- The immediate same-token guard remains active and now records
  `repeatGuardAction="immediate_token_suppressed"` when it fires.
- Before accepting a non-blank/non-EOS candidate token, the runtime now builds a
  bounded candidate token tail and checks for repeated token n-grams.
- The Phase 3 guard uses constants:
  - minimum n-gram length: 2,
  - maximum n-gram length: 8,
  - minimum consecutive repeat count before suppression: 3.
- When a suppressible n-gram is detected, the runtime:
  - increments `rnntRepeatedTokenCount`,
  - records repeated token n-gram length/count/unit,
  - records `repeatGuardAction="ngram_suppressed"`,
  - does not append the candidate token to `emittedTokenIds`,
  - does not append the candidate token to baseline token IDs,
  - does not feed the candidate token into `decoderContext`,
  - advances the current encoder frame.
- The guard is token-structural and does not hard-code `描述`, `色素`, `色彩`, or
  the reported utterance.
- Decoded CJK phrase holding/rejection remains Phase 4; Phase 3 only prevents
  repeated token n-gram loops from growing inside the RNN-T decoder state.

Exit gate:

- The engine cannot emit unbounded alternating token patterns that decode to
  repeated words such as `色素色素色素...`.

Result: implemented. Candidate repeated token n-grams such as the measured
`1251 768` loop are suppressed before they enter emitted-token or decoder-context
state once they reach the conservative repeat threshold.

### Phase 4: Add decoded CJK phrase-repeat guard before final segment publication

Status: implemented.

Target files:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- optionally `src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h`

Plan:

1. After `DecodeTokenIdsToText(...)`, scan `partialText` for repeated UTF-8 CJK
   units.
2. Detect repeated units of 2-6 CJK characters.
3. Treat the guard as a runtime quality failure or hold condition when:
   - a 2-character unit repeats 5 or more times, or
   - a 3-6 character unit repeats 4 or more times, or
   - repeated-unit coverage exceeds a conservative ratio.
4. Prefer failing/holding the transcript with explicit diagnostics over silently
   rewriting user text.
5. Keep this logic generic and structural. Do not hard-code `描述`, `色素`, or the
   reported utterance.

Implementation notes:

- The runtime reuses `ClassifyDecodedRepeats(...)` after decoding Sherpa token IDs
  into `partialText`.
- The guard is evaluated when finite-stream input is final, before assigning
  `sessionState.transcriptText` or publishing a final `SpeechTranscriptSegment`.
- A decoded transcript is rejected when the existing structural repeat classifier
  marks it degenerate:
  - 2-character CJK unit repeated 5 or more times,
  - 3-6 character CJK unit repeated 4 or more times,
  - or repeated-unit coverage above the conservative threshold.
- When the guard fires, the runtime:
  - records `repeatGuardAction="decoded_repeat_final_rejected"`,
  - sets `decodedRepeatFinalRejected=true`,
  - preserves the repeated decoded unit diagnostics,
  - sets `sherpaFinalOutcome="decoded_repeat_rejected"`,
  - does not assign the degenerate text as final transcript text,
  - does not emit it as a final valid segment,
  - does not rewrite the user text.
- `decodedRepeatFinalRejected` is persisted in Sherpa baseline JSON, exposed in
  `SpeechRecognitionDebugInfo` as `sherpaDecodedRepeatFinalRejected`, and surfaced
  through `gateway.speech.debug.snapshot`.
- The logic remains generic and structural; it does not hard-code `描述`, `色素`,
  `色彩`, or any full reported utterance.

Exit gate:

- A repeated CJK phrase transcript is not emitted as a final valid segment.

Result: implemented. Repeated decoded CJK phrase output is now held/rejected at
final publication time with explicit diagnostics instead of being forwarded as a
valid final segment.

### Phase 5: Review RNN-T per-frame emission limits

Target file:

- `src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`

Plan:

1. Compare `kSherpaMaxSymbolsPerFrame=8` with Sherpa reference behavior for this
   Zipformer model.
2. Compare the historical short utterance and current longer utterance baseline
   diagnostics to see whether length/chunk count changes the repeat signature.
3. Use baseline diagnostics to see whether repeated phrases correlate with:
   - `rnntMaxSymbolsHitCount > 0`,
   - high `rnntMultiSymbolFrameCount`,
   - many non-blank symbols emitted from the same encoder frame,
   - final partial chunks with high padded-frame counts.
4. If confirmed, reduce the default max symbols per frame or make it adaptive:
   - stop earlier when score margin is weak,
   - stop when a repeated n-gram candidate appears,
   - preserve normal multi-symbol frames when confidence remains stable.

Exit gate:

- Normal recognition quality is preserved while runaway per-frame repeated output
  is stopped.

### Phase 6: Strengthen frontend containment for repeated CJK phrases

Target file:

- `web/chat/chat-controller.js`

Plan:

1. Extend `assessTranscriptQuality(...)` to detect repeated multi-character
   substrings after sanitization.
2. Include CJK-specific logic for repeated 2-6 character units in compact text.
3. Reject or hold transcripts when a repeated unit exceeds a conservative
   threshold, for example:
   - 2-character unit repeated 5 or more times,
   - 3-6 character unit repeated 4 or more times,
   - repeated-unit coverage above a configured ratio.
4. Surface `transcript_rejected` with reason
   `repetitive phrase transcript pattern detected`.
5. Keep this as containment only; do not silently rewrite the transcript for chat.

Exit gate:

- If Sherpa decoding regresses again, the repeated transcript is blocked before
  `chat.send`.

### Phase 7: Add regression coverage

Target files:

- `tests/SpeechRecognitionRealtimeStreamingTests.cpp`
- `tests/SherpaStep8BaselineToolTests.cpp`
- `tools/compare_sherpa_baseline.py`
- `web/chat/chat-controller.js` test section if in-file browser tests are used

Plan:

1. Add a runtime unit/helper test for repeated token n-gram detection.
2. Add a baseline-tool test that rejects a fixture whose decoded text contains:
   - `描述描述描述描述描述`, or
   - `色素` repeated many times.
3. Add a frontend quality-assessment test:
   - accept `写一首诗用它来描述春天的色彩`,
   - reject the reported repeated transcript.
4. If a stable PCM fixture is available, add a replay baseline test for the full
   Sherpa path.

Exit gate:

- Tests fail on the current repeated-word behavior and pass after runtime and
  containment fixes.

## Validation plan

1. Build with the project-required command:

   `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

2. Run BlazeClaw with the active Sherpa config from `BlazeClawMfc/blazeclaw.conf`.

3. Speak the sample utterance:

   `写一首诗用它来描述春天的色彩`

   Also re-run the historical control utterance:

   `请讲一个笑话`

4. Expected transcript:

   - historical control remains correct,
   - no repeated `描述` run,
   - no repeated `色素` run,
   - final word should be close to `色彩`,
   - no mojibake/control markers,
   - no fallback transcript path.

5. Validate Sherpa diagnostics:

   - `sherpaDecodedTokenCount > 0`,
   - `sherpaEmittedTokenCount > 0`,
   - `sherpaFinalOutcome="final_transcript"` for a clean run,
   - repeat guard diagnostics are zero for clean output,
   - repeat guard diagnostics are non-zero for the old failing baseline.

6. Validate downstream behavior:

   - gateway emits a final segment only for accepted transcript text,
   - WebView does not send a rejected repeated transcript to `chat.send`,
   - clean transcripts still flow to chat normally.

## Acceptance criteria

- The active Sherpa Zipformer path no longer produces repeated word/phrase runs
  for the reported utterance.
- The historical control utterance `请讲一个笑话` remains correct; the new fix does
  not regress the previous successful repeat-word repair.
- The fix is Sherpa-native and does not use Qwen fallback, synthetic transcript
  rescue, or phrase-specific hard-coded replacements.
- Runtime diagnostics identify repeated token n-grams or decoded phrase units when
  a repeat guard fires.
- Frontend containment blocks visibly repetitive CJK transcripts if the runtime
  regresses.
- Build validation passes with the project-required MSBuild command.
