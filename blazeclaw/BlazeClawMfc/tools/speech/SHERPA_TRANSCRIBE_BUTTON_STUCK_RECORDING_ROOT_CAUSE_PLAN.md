# SHERPA Transcribe Button Stuck in "Recording..." Root Cause Plan

## Problem Statement
After clicking **Transcribe** and speaking (for example `请讲一个笑话`), interim preview text appears in **Recognizing stream ...**, but the final recognition flow does not settle correctly:
- The transcribe button can remain stuck at **Recording... (click to stop)**.
- Repeated clicks keep issuing stop/final requests.
- Preview text can leak into the final lifecycle path instead of cleanly transitioning to a final run state.

## Root Cause Analysis (Codebase Evidence)

### 1) Final request can be rejected as "busy" by an in-flight preview request in the same session
- In [`SpeechTranscriptionCoordinator::Accept`](../../src/core/SpeechTranscriptionCoordinator.cpp), session-level in-flight gating rejects any second run when a run is still active (`accept.rejected_busy_session`, lines ~172-190).
- Preview runs (`speech-preview-*`) and final runs (`speech-final-*`) are treated identically because `SpeechExecutionRequest` has no `livePreviewOnly`/request kind field ([`ISpeechRecognitionRuntime.h`](../../src/core/runtime/SpeechRecognition/ISpeechRecognitionRuntime.h), lines ~83-91).
- Result: the stop-triggered final run can collide with the last preview poll request and get rejected while preview is still active.

### 2) Busy rejection payload returns active preview execution state, which reactivates UI recording semantics
- In [`GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`](../../src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp), the `!accepted.accepted` branch returns `speechSession/runId/stage/text` from the existing busy run (lines ~790-841), often the preview run (`streaming`, `speech-preview-*`).
- Frontend final path (`transcribeSpeech`) normalizes and applies this returned state ([`chat-controller.js`](../../web/chat/chat-controller.js), lines ~2663-2719), so the session can remain/revert to streaming preview semantics.
- Button rendering logic marks streaming/preview stages as recording-active ([`index.js`](../../web/chat/index.js), lines ~159-168 and ~2606-2608), causing the persistent "Recording..." caption.

### 3) Final-run authority is not guaranteed when final request receives non-final/busy state
- Frontend has preview-stale guards (`isPreviewUpdateBlockedByFinalAuthority`, etc.), but they rely on final authority having been established in state.
- If the final request itself resolves to busy preview state (instead of a finalized final run), authority is never latched robustly enough to prevent visual regression.

## Fix Strategy

### A. Make final transcription preempt preview for the same session (primary fix)
1. Extend execution request context to encode request type (`preview` vs `final`) in coordinator-facing contracts.
2. In coordinator accept logic, allow a **final** request to supersede an in-flight **preview** request for the same session.
3. On takeover, atomically cancel/retire preview execution state before admitting final execution state.

### B. Harden gateway response semantics for rejected/fallback paths
4. If final request is not admitted, return explicit terminal/finalization-safe semantics for the caller (do not reassert active preview `streaming` as authoritative final-session state).
5. Ensure returned run identifiers preserve final intent (`requested runId` vs `executionRunId`) without causing preview state rollback.

### C. Add frontend guard rails so UI never sticks in recording on finalization race
6. In `transcribeSpeech` final flow, treat busy-preview responses as non-authoritative for final-state replacement; keep `stopped/transcribing/final` progression.
7. Strengthen lifecycle filtering so preview updates cannot re-open recording after stop/final has started for the same session.
8. Preserve preview text display, but separate it from recording state ownership.

## Step-by-Step Implementation Plan
1. **Introduce request-kind metadata into speech execution contracts**
   - Update speech execution request structs and binding layers to carry `livePreviewOnly` or equivalent request kind through coordinator entry points.
2. **Implement preview-to-final handoff in coordinator accept path**
   - In `SpeechTranscriptionCoordinator::Accept`, detect same-session in-flight preview when incoming request is final.
   - Cancel/retire preview state and accept final run atomically.
3. **Update gateway busy/rejection response shaping**
   - In `speech.transcribe` handler, prevent busy-preview state from being emitted as authoritative final response for final requests.
   - Return explicit, deterministic fields for caller-side state machine handling.
4. **Harden frontend final-run authority in `chat-controller.js`**
   - Ignore/contain busy-preview payloads during final flow.
   - Keep finalizing state progression and prevent fallback to preview-streaming ownership.
5. **Adjust UI state transitions in `index.js`**
   - Ensure recording caption is strictly tied to active recording/finalizable preview ownership, not stale preview echoes after finalization begins.
6. **Add regression tests**
   - Coordinator-level unit test: preview in-flight + final request same session must admit final.
   - Web controller test: final flow cannot be overwritten back to recording by preview update.
7. **Validate end-to-end behavior**
   - Manual: Transcribe -> speak Chinese utterance -> stop once -> verify final recognition and button returns to `Transcribe`.
   - Build validation command:
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

## Expected Outcome
- Preview remains responsive during recording.
- Stop action deterministically transitions to final transcription.
- No stuck "Recording..." button after stop.
- No preview-state takeover after finalization has started/completed.

## Implementation Status (Completed)

### Code changes applied
- Added request-kind metadata propagation with `livePreviewOnly` across speech runtime execution contracts and coordinator bindings.
- Implemented preview-to-final preemption in `SpeechTranscriptionCoordinator::Accept`:
  - Final requests can supersede in-flight preview requests for the same session.
  - Preempted preview state is marked cancelled and lifecycle callback is emitted before final run proceeds.
- Hardened `speech.transcribe` busy response shaping to avoid returning preview-stream authority payloads for final requests:
  - Finalization-safe `runId/stage` are returned.
  - Preview transcript/segment text is suppressed in this conflict path.
- Hardened WebView speech state authority in `chat-controller.js`:
  - Preserved `finalRunId/previousRunId` in normalized speech state.
  - Added guard to block preview-run payloads from replacing final-owned state.
- Updated `index.js` transcribe button ownership logic:
  - Added final-authority-aware guard so stale preview updates cannot keep the button in `Recording...`.

### Regression coverage updates
- Extended `BlazeClawMfc/tests/GatewaySpeechPhase56ParityTests.cpp` with a new handoff/final-authority regression case covering:
  - contract propagation markers,
  - coordinator preemption markers,
  - gateway busy conflict shaping marker,
  - frontend final-authority guards.

### Validation checklist
- [x] Build command attempted:
  - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001` (solution file missing in current workspace)
- [x] Fallback build command attempted:
  - `msbuild "BlazeClawMfc/BlazeClawMfc.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
- [ ] Full build passed (blocked by external dependency):
  - `C1083: Cannot open include file: 'kaldi-native-fbank/csrc/online-feature.h'` in `SherpaZipformerStreamingEngine.cpp`
- [x] Test discovery/run attempted in Test Explorer.
- [ ] Tests completed (blocked because test run aborts on the same build dependency failure).
