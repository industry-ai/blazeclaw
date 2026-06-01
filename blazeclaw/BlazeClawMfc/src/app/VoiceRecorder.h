#pragma once

#include "../core/runtime/SpeechRecognition/SpeechRecognitionContracts.h"
#include "AudioRingBuffer.h"

#include <mmsystem.h>
#include <mmreg.h>
#include <memory>
#include <optional>
#include <vector>

#pragma comment(lib, "winmm.lib")

// Recording state
enum class VoiceRecorderState
{
    Idle,       // Idle
    Recording,  // Recording in progress
    Paused      // Paused
};

enum class VoiceBoundarySignalType
{
    SpeechStartCandidate,
    SpeechEndCandidate,
    MaxUtteranceTimeout,
};

struct VoiceBoundarySignal
{
    VoiceBoundarySignalType type = VoiceBoundarySignalType::SpeechStartCandidate;
    uint64_t startSequence = 0;
    uint64_t endSequence = 0;
    uint32_t durationMs = 0;
    uint64_t chunkLatencyUs = 0;
};

struct VoiceRecorderTelemetry
{
    uint64_t ringOccupancyPercent = 0;
    uint64_t ringDroppedSamples = 0;
    uint64_t chunkEnqueueLatencyUs = 0;
    uint64_t silenceSegmentationLatencyUs = 0;
    uint64_t boundarySignalCount = 0;
    uint64_t captureChannelIndex = 0;
    uint64_t captureChannelEnergyPermille = 0;
    bool captureAdaptiveEnabled = false;
    bool captureAdaptiveLocked = false;
    uint64_t captureAdaptiveObservedFrames = 0;
};

class IVoiceVadProvider
{
public:
    virtual ~IVoiceVadProvider() = default;
    virtual bool IsSpeech(
        const float* samples,
        size_t sampleCount,
        uint32_t sampleRate) = 0;
};

enum class VoiceVadProviderType
{
    NoOp,
    Nvidia,
};

// Recording configuration
struct VoiceRecorderConfig
{
    UINT nChannels = 1;           // Channels: 1 = mono
    UINT nSamplesPerSec = 16000; // Sample rate: 16000Hz
    UINT nBitsPerSample = 16;     // Bits per sample: 16bit
    UINT ringBufferDurationSeconds = 30; // Ring retention window
    UINT ringCaptureChannelIndex = 0; // Interleaved channel index captured into ring
    bool ringCaptureChannelFixedOverride = false;
    bool adaptiveRingCaptureChannelEnabled = true;
    UINT adaptiveRingCaptureDecisionFrames = 2400;
    UINT adaptiveRingCaptureMinStableChunks = 3;
    UINT adaptiveRingCaptureRelockFloorPermille = 1;
    UINT adaptiveRingCaptureRelockWindowFrames = 1600;
    bool vadEnabled = true;
    VoiceVadProviderType vadProviderType = VoiceVadProviderType::NoOp;
    UINT vadFrameDurationMs = 20;
    UINT vadSilenceDurationMs = 500;
    UINT vadMaxUtteranceMs = 15000;

    DWORD GetAvgBytesPerSec() const
    {
        return nSamplesPerSec * nChannels * nBitsPerSample / 8;
    }

    DWORD GetBlockAlign() const
    {
        return nChannels * nBitsPerSample / 8;
    }

    size_t GetRingCapacitySamples() const
    {
        return static_cast<size_t>(nSamplesPerSec) *
               static_cast<size_t>(ringBufferDurationSeconds);
    }

    size_t GetVadFrameSamples() const
    {
        return static_cast<size_t>(nSamplesPerSec) *
               static_cast<size_t>(vadFrameDurationMs) / 1000;
    }
};

// Recording data callback interface
class IVoiceRecorderCallback
{
public:
    virtual ~IVoiceRecorderCallback() = default;
    virtual void OnVoiceDataAvailable(const BYTE* pData, DWORD dwLength) = 0;
    virtual void OnVoiceStateChanged(VoiceRecorderState state) = 0;
    virtual void OnVoiceSessionChanged(
        const blazeclaw::core::speechrecognition::SpeechSessionState& sessionState) = 0;
    virtual void OnVoiceError(long nError, const wchar_t* pszDescription) = 0;
    virtual void OnVoiceBoundarySignal(const VoiceBoundarySignal& signal)
    {
        UNREFERENCED_PARAMETER(signal);
    }
};

// Voice recorder wrapper class
class CVoiceRecorder
{
public:
    CVoiceRecorder();
    ~CVoiceRecorder();

    // Disable copy
    CVoiceRecorder(const CVoiceRecorder&) = delete;
    CVoiceRecorder& operator=(const CVoiceRecorder&) = delete;

    // Initialize / shutdown
    BOOL Initialize(HWND hWnd, const VoiceRecorderConfig& config = VoiceRecorderConfig());
    void Shutdown();

    // Recording control
    BOOL StartRecording(const wchar_t* pszFilePath);
    BOOL StopRecording();
    BOOL PauseRecording();
    BOOL ResumeRecording();

    // State query
    VoiceRecorderState GetState() const { return m_state; }
    const VoiceRecorderConfig& GetConfig() const { return m_config; }
    DWORD GetRecordedDataSize() const { return m_dwRecordedDataSize; }

    // Raw audio data access (PCM bytes)
    const std::vector<BYTE>& GetRecordedData() const { return m_recordedData; }

    // Ring-backed sample access for downstream STT pipeline
    bool ReadLatestSamples(std::vector<float>& out, size_t sampleCount) const;
    bool ReadSamplesBySequence(
        std::vector<float>& out,
        uint64_t startSequence,
        size_t sampleCount) const;
    uint64_t GetRingLatestSequence() const;
    uint64_t GetRingOldestAvailableSequence() const;
    std::optional<blazeclaw::core::speechrecognition::SpeechAudioArtifact>
        BuildStreamingAudioArtifact() const;
    VoiceRecorderTelemetry GetTelemetrySnapshot() const;

    // Test-oriented deterministic ingest helper
    void PushPcm16ChunkForTest(
        const int16_t* data,
        size_t frameCount,
        size_t channelCount,
        uint64_t enqueueLatencyUs = 0);

    // Callback setting
    void SetCallback(IVoiceRecorderCallback* pCallback) { m_pCallback = pCallback; }

    // Device-related
    static int GetInputDeviceCount();
    static BOOL GetInputDeviceName(int nDeviceIndex, wchar_t* pszName, int nNameLength);
    BOOL SetInputDevice(int nDeviceIndex);

protected:
    static void CALLBACK WaveInCallback(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
        DWORD_PTR dwParam1, DWORD_PTR dwParam2);

    void HandleWaveInMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void AllocateBuffers();
    void FreeBuffers();
    void PrepareBuffers();
    void UnprepareBuffers();
    BOOL WriteWavToFile();
    void UpdateSessionState(
        blazeclaw::core::speechrecognition::SpeechSessionStage stage);
    void NotifySessionState(
        blazeclaw::core::speechrecognition::SpeechSessionStage stage);
    void ResetVadState();
    void ProcessVadFromRing();
    void ProcessVadFromRingWithLatency(uint64_t enqueueLatencyUs);
    void EmitBoundarySignal(
        VoiceBoundarySignalType type,
        uint64_t startSequence,
        uint64_t endSequence,
        uint64_t chunkLatencyUs = 0);
    std::unique_ptr<IVoiceVadProvider> CreateVadProvider(
        VoiceVadProviderType providerType) const;
    size_t ResolveCaptureChannelIndex(
        const int16_t* data,
        size_t frameCount,
        size_t channelCount);
    void ResetCaptureChannelSelection();

private:
    HWND           m_hNotifyWnd;
    HWAVEIN        m_hWaveIn;
    VoiceRecorderConfig m_config;
    VoiceRecorderState m_state;
    IVoiceRecorderCallback* m_pCallback;
    blazeclaw::core::speechrecognition::SpeechSessionState m_sessionState;

    int            m_nDeviceID;
    UINT           m_nBufferCount;
    WAVEHDR*       m_pWaveHeaders;
    BYTE**         m_pBuffers;

    // File path for saving on stop
    wchar_t        m_szFilePath[MAX_PATH];
    // In-memory audio buffer (PCM)
    std::vector<BYTE> m_recordedData;
    DWORD          m_dwRecordedDataSize;
    std::unique_ptr<AudioRingBuffer> m_audioRingBuffer;
    uint64_t       m_recordingStartSequence;
    std::unique_ptr<IVoiceVadProvider> m_vadProvider;
    uint64_t       m_vadNextSequence;
    uint64_t       m_vadSpeechStartSequence;
    uint64_t       m_vadLastSpeechSequence;
    uint64_t       m_vadSilenceSamples;
    uint32_t       m_vadBoundarySignalSequence;
    bool           m_vadSpeechActive;
    std::vector<float> m_vadFrameBuffer;
    VoiceRecorderTelemetry m_telemetry;
    size_t         m_selectedCaptureChannelIndex;
    bool           m_captureChannelLocked;
    size_t         m_captureAdaptiveLastBestChannel;
    uint32_t       m_captureAdaptiveStableChunks;
    uint64_t       m_captureAdaptiveObservedFrames;
    uint64_t       m_captureAdaptiveObservedFramesSinceLock;
    std::vector<double> m_captureAdaptiveEnergyByChannel;

    BOOL           m_bInitialized;
};
