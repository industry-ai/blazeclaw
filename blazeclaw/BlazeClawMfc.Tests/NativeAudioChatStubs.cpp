#include "pch.h"

#include "../BlazeClawMfc/src/app/VoiceRecorder.h"
#include "../BlazeClawMfc/src/app/ChatView.h"
#include "../BlazeClawMfc/src/app/BlazeClawMfcApp.h"

CVoiceRecorder::CVoiceRecorder() = default;
CVoiceRecorder::~CVoiceRecorder() = default;

BOOL CVoiceRecorder::Initialize(HWND, const VoiceRecorderConfig&) {
	return TRUE;
}

void CVoiceRecorder::Shutdown() {
}

BOOL CVoiceRecorder::StartRecording(const wchar_t*) {
	return TRUE;
}

BOOL CVoiceRecorder::StopRecording() {
	return TRUE;
}

BOOL CVoiceRecorder::PauseRecording() {
	return TRUE;
}

BOOL CVoiceRecorder::ResumeRecording() {
	return TRUE;
}

void CALLBACK CVoiceRecorder::WaveInCallback(HWAVEIN, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
}

void CVoiceRecorder::HandleWaveInMessage(UINT, WPARAM, LPARAM) {
}

void CVoiceRecorder::AllocateBuffers() {
}

void CVoiceRecorder::FreeBuffers() {
}

void CVoiceRecorder::PrepareBuffers() {
}

void CVoiceRecorder::UnprepareBuffers() {
}

BOOL CVoiceRecorder::WriteWavToFile() {
	return TRUE;
}

void CVoiceRecorder::UpdateSessionState(
	blazeclaw::core::speechrecognition::SpeechSessionStage) {
}

void CVoiceRecorder::NotifySessionState(
	blazeclaw::core::speechrecognition::SpeechSessionStage) {
}

int CVoiceRecorder::GetInputDeviceCount() {
	return 1;
}

BOOL CVoiceRecorder::GetInputDeviceName(int, wchar_t*, int) {
	return FALSE;
}

BOOL CVoiceRecorder::SetInputDevice(int) {
	return TRUE;
}

const blazeclaw::config::AppConfig& CBlazeClawMFCApp::Config() const noexcept {
	static blazeclaw::config::AppConfig config;
	return config;
}

VoiceRecorderConfig BuildVoiceRecorderConfigFromSpeechConfig(
	const blazeclaw::config::SpeechRecognitionConfig& speechConfig) {
	VoiceRecorderConfig config;
	config.nChannels = (std::max)(1U, speechConfig.recorderChannels);
	config.nSamplesPerSec = (std::max)(1U, speechConfig.sampleRate);
	config.ringCaptureChannelIndex = speechConfig.recorderCaptureChannelIndex;
	config.ringCaptureChannelFixedOverride = speechConfig.recorderCaptureChannelFixedOverride;
	config.adaptiveRingCaptureChannelEnabled = speechConfig.recorderAdaptiveCaptureChannelEnabled;
	config.adaptiveRingCaptureDecisionFrames = (std::max)(1U, speechConfig.recorderAdaptiveCaptureDecisionFrames);
	config.adaptiveRingCaptureMinStableChunks = (std::max)(1U, speechConfig.recorderAdaptiveCaptureMinStableChunks);
	config.adaptiveRingCaptureRelockFloorPermille = speechConfig.recorderAdaptiveCaptureRelockFloorPermille;
	config.adaptiveRingCaptureRelockWindowFrames = (std::max)(1U, speechConfig.recorderAdaptiveCaptureRelockWindowFrames);
	return config;
}

bool CVoiceRecorder::ReadLatestSamples(std::vector<float>& out, size_t sampleCount) const {
	if (!m_audioRingBuffer) {
		out.clear();
		return false;
	}
	return m_audioRingBuffer->PeekLatest(out, sampleCount);
}

bool CVoiceRecorder::ReadSamplesBySequence(
	std::vector<float>& out,
	uint64_t startSequence,
	size_t sampleCount) {
	if (!m_audioRingBuffer) {
		out.clear();
		return false;
	}
	return m_audioRingBuffer->ReadWindowBySequence(out, startSequence, sampleCount);
}

uint64_t CVoiceRecorder::GetRingLatestSequence() const {
	return m_audioRingBuffer ? m_audioRingBuffer->GetLatestSequence() : 0;
}

uint64_t CVoiceRecorder::GetRingOldestAvailableSequence() const {
	return m_audioRingBuffer ? m_audioRingBuffer->GetOldestAvailableSequence() : 0;
}

std::optional<blazeclaw::core::speechrecognition::SpeechAudioArtifact>
CVoiceRecorder::BuildStreamingAudioArtifact() const {
	if (!m_audioRingBuffer) {
		return std::nullopt;
	}

	blazeclaw::core::speechrecognition::SpeechAudioArtifact artifact;
	artifact.handoffMode = blazeclaw::core::speechrecognition::SpeechAudioHandoffMode::PcmStream;
	artifact.sampleRate = m_config.nSamplesPerSec;
	artifact.channels = 1;
	artifact.bitsPerSample = m_config.nBitsPerSample;
	artifact.sequenceStart = m_audioRingBuffer->GetOldestAvailableSequence();
	artifact.sequenceEnd = m_audioRingBuffer->GetLatestSequence();
	if (artifact.sequenceEnd <= artifact.sequenceStart) {
		return std::nullopt;
	}
	artifact.durationMs = static_cast<std::uint32_t>(
		((artifact.sequenceEnd - artifact.sequenceStart) * 1000ULL) /
		(static_cast<std::uint64_t>(artifact.sampleRate == 0 ? 1 : artifact.sampleRate)));
	return artifact;
}

VoiceRecorderTelemetry CVoiceRecorder::GetTelemetrySnapshot() const {
	return m_telemetry;
}

void CVoiceRecorder::PushPcm16ChunkForTest(
	const int16_t* data,
	size_t frameCount,
	size_t channelCount,
	uint64_t enqueueLatencyUs) {
	if (!data || frameCount == 0 || channelCount == 0) {
		return;
	}
	if (!m_audioRingBuffer) {
		m_audioRingBuffer = std::make_unique<AudioRingBuffer>(m_config.GetRingCapacitySamples());
	}
	m_audioRingBuffer->PushInterleavedPcm16(data, frameCount, channelCount, 0);
	m_telemetry.chunkEnqueueLatencyUs = enqueueLatencyUs;
	const auto cap = static_cast<uint64_t>(m_audioRingBuffer->GetCapacitySamples());
	const auto avail = m_audioRingBuffer->GetLatestSequence() - m_audioRingBuffer->GetOldestAvailableSequence();
	m_telemetry.ringOccupancyPercent = cap == 0 ? 0 : (avail * 100ULL) / cap;
	m_telemetry.ringDroppedSamples = m_audioRingBuffer->GetDroppedSamples();
}

void CVoiceRecorder::ResetVadState() {}
void CVoiceRecorder::ProcessVadFromRing() {}
void CVoiceRecorder::ProcessVadFromRingWithLatency(uint64_t) {}
void CVoiceRecorder::EmitBoundarySignal(VoiceBoundarySignalType, uint64_t, uint64_t, uint64_t) {}

std::unique_ptr<IVoiceVadProvider> CVoiceRecorder::CreateVadProvider(VoiceVadProviderType) const {
	return {};
}

bool CChatView::StartRecordingToPath(const CStringW&) {
	return true;
}

CStringW CChatView::StopRecordingAndGetPath() {
	return CStringW();
}

std::optional<blazeclaw::core::speechrecognition::SpeechAudioArtifact>
CChatView::GetLastRecordingAudioArtifact() const {
	return std::nullopt;
}
