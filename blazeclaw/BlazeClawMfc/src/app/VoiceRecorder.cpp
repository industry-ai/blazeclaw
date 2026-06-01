#include "pch.h"
#include "VoiceRecorder.h"
#include "../core/runtime/SpeechRecognition/StreamingAudioSourceRegistry.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>

namespace {

class NoOpVoiceVadProvider final : public IVoiceVadProvider
{
public:
    bool IsSpeech(
        const float* samples,
        size_t sampleCount,
        uint32_t sampleRate) override
    {
        UNREFERENCED_PARAMETER(samples);
        UNREFERENCED_PARAMETER(sampleCount);
        UNREFERENCED_PARAMETER(sampleRate);
        return true;
    }
};

class NvidiaVoiceVadProvider final : public IVoiceVadProvider
{
public:
    bool IsSpeech(
        const float* samples,
        size_t sampleCount,
        uint32_t sampleRate) override
    {
        UNREFERENCED_PARAMETER(samples);
        UNREFERENCED_PARAMETER(sampleCount);
        UNREFERENCED_PARAMETER(sampleRate);
        return true;
    }
};

std::string ToNarrow(const wchar_t* value)
{
    if (value == nullptr)
    {
        return {};
    }

    std::string output;
    while (*value != L'\0')
    {
        output.push_back(static_cast<char>(*value <= 0x7F ? *value : '?'));
        ++value;
    }
    return output;
}

size_t ResolveRingCapacitySamples(const VoiceRecorderConfig& config)
{
    const size_t requested = config.GetRingCapacitySamples();
    if (requested > 0) {
        return requested;
    }

    return static_cast<size_t>(config.nSamplesPerSec);
}

uint64_t DurationMsToSamples(
    const VoiceRecorderConfig& config,
    const uint32_t durationMs)
{
    if (config.nSamplesPerSec == 0) {
        return 0;
    }

    return (static_cast<uint64_t>(config.nSamplesPerSec) * durationMs) / 1000ULL;
}

const char* kVoiceRecorderStreamId = "voice_recorder";

VoiceRecorderConfig BuildVoiceRecorderConfigFromSpeechConfigImpl(
    const blazeclaw::config::SpeechRecognitionConfig& speechConfig)
{
    VoiceRecorderConfig recorderConfig;
    recorderConfig.nChannels = (std::max)(1U, speechConfig.recorderChannels);
    recorderConfig.nSamplesPerSec =
        (std::max)(1U, speechConfig.sampleRate);
    recorderConfig.ringCaptureChannelIndex =
        speechConfig.recorderCaptureChannelIndex;
    recorderConfig.ringCaptureChannelFixedOverride =
        speechConfig.recorderCaptureChannelFixedOverride;
    recorderConfig.adaptiveRingCaptureChannelEnabled =
        speechConfig.recorderAdaptiveCaptureChannelEnabled;
    recorderConfig.adaptiveRingCaptureDecisionFrames =
        (std::max)(1U, speechConfig.recorderAdaptiveCaptureDecisionFrames);
    recorderConfig.adaptiveRingCaptureMinStableChunks =
        (std::max)(1U, speechConfig.recorderAdaptiveCaptureMinStableChunks);
    recorderConfig.adaptiveRingCaptureRelockFloorPermille =
        speechConfig.recorderAdaptiveCaptureRelockFloorPermille;
    recorderConfig.adaptiveRingCaptureRelockWindowFrames =
        (std::max)(1U, speechConfig.recorderAdaptiveCaptureRelockWindowFrames);
    return recorderConfig;
}

} // namespace

VoiceRecorderConfig BuildVoiceRecorderConfigFromSpeechConfig(
    const blazeclaw::config::SpeechRecognitionConfig& speechConfig)
{
    return BuildVoiceRecorderConfigFromSpeechConfigImpl(speechConfig);
}

CVoiceRecorder::CVoiceRecorder()
    : m_hNotifyWnd(nullptr)
    , m_hWaveIn(nullptr)
    , m_state(VoiceRecorderState::Idle)
    , m_pCallback(nullptr)
    , m_nDeviceID(WAVE_MAPPER)
    , m_nBufferCount(3)
    , m_pWaveHeaders(nullptr)
    , m_pBuffers(nullptr)
    , m_dwRecordedDataSize(0)
    , m_recordingStartSequence(0)
    , m_vadNextSequence(0)
    , m_vadSpeechStartSequence(0)
    , m_vadLastSpeechSequence(0)
    , m_vadSilenceSamples(0)
    , m_vadBoundarySignalSequence(0)
    , m_vadSpeechActive(false)
    , m_selectedCaptureChannelIndex(0)
    , m_captureChannelLocked(false)
    , m_captureAdaptiveLateReselectionUsed(false)
    , m_captureAdaptiveLastBestChannel(0)
    , m_captureAdaptiveStableChunks(0)
    , m_captureAdaptiveObservedFrames(0)
    , m_captureAdaptiveObservedFramesSinceLock(0)
    , m_bInitialized(FALSE)
{
    m_szFilePath[0] = L'\0';
    m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Idle;
}

CVoiceRecorder::~CVoiceRecorder()
{
    Shutdown();
}

BOOL CVoiceRecorder::Initialize(HWND hWnd, const VoiceRecorderConfig& config)
{
    if (m_bInitialized)
    {
        return FALSE;
    }

    int nDevices = waveInGetNumDevs();
    if (nDevices == 0)
    {
        TRACE(L"CVoiceRecorder::Initialize: no recording device found!\n");
        return FALSE;
    }

    m_hNotifyWnd = hWnd;
    m_config = config;
    m_nBufferCount = 3;
    m_sessionState = {};
    m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Idle;
    m_audioRingBuffer =
        std::make_unique<AudioRingBuffer>(ResolveRingCapacitySamples(m_config));
    m_recordingStartSequence = m_audioRingBuffer != nullptr
        ? m_audioRingBuffer->GetLatestSequence()
        : 0;
    m_vadProvider = CreateVadProvider(m_config.vadProviderType);
    ResetVadState();
    ResetCaptureChannelSelection();

    m_bInitialized = TRUE;
    return TRUE;
}

void CVoiceRecorder::Shutdown()
{
    if (m_state != VoiceRecorderState::Idle)
    {
        StopRecording();
    }

    if (m_hWaveIn != nullptr)
    {
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
    }

    FreeBuffers();
    blazeclaw::core::speechrecognition::UnregisterStreamingAudioSource(
        kVoiceRecorderStreamId);
    m_bInitialized = FALSE;
}

BOOL CVoiceRecorder::StartRecording(const wchar_t* pszFilePath)
{
    if (!m_bInitialized)
    {
        return FALSE;
    }

    if (m_state == VoiceRecorderState::Recording)
    {
        return FALSE;
    }

    if (m_state != VoiceRecorderState::Idle)
    {
        StopRecording();
    }

    // Save file path (file is written after recording stops)
    if (pszFilePath != nullptr && pszFilePath[0] != L'\0')
    {
        wcscpy_s(m_szFilePath, pszFilePath);
    }
    else
    {
        m_szFilePath[0] = L'\0';
    }

    // Clear buffer
    m_recordedData.clear();
    m_dwRecordedDataSize = 0;
    m_audioRingBuffer =
        std::make_unique<AudioRingBuffer>(ResolveRingCapacitySamples(m_config));
    m_vadProvider = CreateVadProvider(m_config.vadProviderType);
    ResetVadState();
    ResetCaptureChannelSelection();
    m_sessionState = {};
    m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Recording;
    m_sessionState.audioPath = ToNarrow(m_szFilePath);
    NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Recording);

    // Open waveform input device
    WAVEFORMATEX wfex = {};
    wfex.wFormatTag = WAVE_FORMAT_PCM;
    wfex.nChannels = (WORD)m_config.nChannels;
    wfex.nSamplesPerSec = m_config.nSamplesPerSec;
    wfex.wBitsPerSample = (WORD)m_config.nBitsPerSample;
    wfex.nBlockAlign = (WORD)m_config.GetBlockAlign();
    wfex.nAvgBytesPerSec = wfex.nBlockAlign * m_config.nSamplesPerSec;
    wfex.cbSize = 0;

    MMRESULT mmResult = waveInOpen(&m_hWaveIn, m_nDeviceID, &wfex,
        (DWORD_PTR)WaveInCallback, (DWORD_PTR)this, CALLBACK_FUNCTION);
    if (mmResult != MMSYSERR_NOERROR)
    {
        TRACE(L"CVoiceRecorder::StartRecording failed: waveInOpen error=%d\n", mmResult);
        m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Failed;
        m_sessionState.error = blazeclaw::core::speechrecognition::SpeechRecognitionError{
            .code = blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode::RuntimeUnavailable,
            .message = "failed to open recording device",
        };
        NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Failed);
        if (m_pCallback)
        {
            m_pCallback->OnVoiceError(mmResult, L"Failed to open recording device");
        }
        return FALSE;
    }

    AllocateBuffers();
    PrepareBuffers();

    for (UINT i = 0; i < m_nBufferCount; i++)
    {
        mmResult = waveInAddBuffer(m_hWaveIn, &m_pWaveHeaders[i], sizeof(WAVEHDR));
        if (mmResult != MMSYSERR_NOERROR)
        {
            UnprepareBuffers();
            FreeBuffers();
            waveInClose(m_hWaveIn);
            m_hWaveIn = nullptr;
            m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Failed;
            m_sessionState.error = blazeclaw::core::speechrecognition::SpeechRecognitionError{
                .code = blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode::RuntimeUnavailable,
                .message = "failed to add capture buffer",
            };
            NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Failed);
            if (m_pCallback)
            {
                m_pCallback->OnVoiceError(mmResult, L"Failed to add buffer");
            }
            return FALSE;
        }
    }

    mmResult = waveInStart(m_hWaveIn);
    if (mmResult != MMSYSERR_NOERROR)
    {
        TRACE(L"CVoiceRecorder::StartRecording failed: waveInStart error=%d\n", mmResult);
        UnprepareBuffers();
        FreeBuffers();
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
        m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Failed;
        m_sessionState.error = blazeclaw::core::speechrecognition::SpeechRecognitionError{
            .code = blazeclaw::core::speechrecognition::SpeechRecognitionErrorCode::RuntimeUnavailable,
            .message = "failed to start capture",
        };
        NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Failed);
        if (m_pCallback)
        {
            m_pCallback->OnVoiceError(mmResult, L"Failed to start recording");
        }
        return FALSE;
    }

    m_state = VoiceRecorderState::Recording;

    blazeclaw::core::speechrecognition::RegisterStreamingAudioSource(
        kVoiceRecorderStreamId,
        blazeclaw::core::speechrecognition::StreamingAudioSourceReader{
            .readBySequence = [this](
                const std::uint64_t startSequence,
                const std::size_t sampleCount,
                std::vector<float>& outSamples) {
                return ReadSamplesBySequence(outSamples, startSequence, sampleCount);
            },
            .latestSequence = [this]() {
                return GetRingLatestSequence();
            },
            .oldestSequence = [this]() {
                return GetRingOldestAvailableSequence();
            },
        });

    if (m_pCallback)
    {
        m_pCallback->OnVoiceStateChanged(m_state);
    }

    return TRUE;
}

BOOL CVoiceRecorder::StopRecording()
{
    if (m_state == VoiceRecorderState::Idle)
    {
        return TRUE;
    }

    VoiceRecorderState prevState = m_state;
    m_state = VoiceRecorderState::Idle;
    m_vadSpeechActive = false;
    HWAVEIN hWaveInToClose = m_hWaveIn;
    m_hWaveIn = nullptr;

    if (hWaveInToClose != nullptr)
    {
        waveInStop(hWaveInToClose);
        waveInReset(hWaveInToClose);
    }

    UnprepareBuffers();

    if (hWaveInToClose != nullptr)
    {
        waveInClose(hWaveInToClose);
    }

    // Write WAV file from memory buffer
    if (m_szFilePath[0] != L'\0' && !m_recordedData.empty())
    {
        WriteWavToFile();
    }

    FreeBuffers();
    //blazeclaw::core::speechrecognition::UnregisterStreamingAudioSource(
    //    kVoiceRecorderStreamId);

    m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Stopped;
    NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Stopped);

    if (m_pCallback && prevState != VoiceRecorderState::Idle)
    {
        m_pCallback->OnVoiceStateChanged(VoiceRecorderState::Idle);
    }

    return TRUE;
}

BOOL CVoiceRecorder::PauseRecording()
{
    if (m_state != VoiceRecorderState::Recording)
    {
        return FALSE;
    }

    MMRESULT mmResult = waveInStop(m_hWaveIn);
    if (mmResult != MMSYSERR_NOERROR)
    {
        return FALSE;
    }

    m_state = VoiceRecorderState::Paused;
    m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Paused;
    NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Paused);

    if (m_pCallback)
    {
        m_pCallback->OnVoiceStateChanged(m_state);
    }

    return TRUE;
}

BOOL CVoiceRecorder::ResumeRecording()
{
    if (m_state != VoiceRecorderState::Paused)
    {
        return FALSE;
    }

    MMRESULT mmResult = waveInStart(m_hWaveIn);
    if (mmResult != MMSYSERR_NOERROR)
    {
        return FALSE;
    }

    m_state = VoiceRecorderState::Recording;
    m_sessionState.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Recording;
    NotifySessionState(blazeclaw::core::speechrecognition::SpeechSessionStage::Recording);

    if (m_pCallback)
    {
        m_pCallback->OnVoiceStateChanged(m_state);
    }

    return TRUE;
}

void CALLBACK CVoiceRecorder::WaveInCallback(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
    DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    UNREFERENCED_PARAMETER(hwi);
    UNREFERENCED_PARAMETER(dwParam2);

    CVoiceRecorder* pThis = (CVoiceRecorder*)dwInstance;
    if (pThis != nullptr)
    {
        pThis->HandleWaveInMessage(uMsg, (WPARAM)dwParam2, (LPARAM)dwParam1);
    }
}

void CVoiceRecorder::HandleWaveInMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(wParam);

    if (uMsg == WIM_DATA)
    {
        WAVEHDR* pWaveHdr = (WAVEHDR*)lParam;

        if (pWaveHdr != nullptr && pWaveHdr->dwBytesRecorded > 0)
        {
            // Append audio data to in-memory buffer
            const BYTE* pData = (const BYTE*)pWaveHdr->lpData;
            DWORD dwLen = pWaveHdr->dwBytesRecorded;

            if (m_audioRingBuffer != nullptr &&
                m_config.nBitsPerSample == 16 &&
                m_config.nChannels > 0) {
                const size_t blockAlign = static_cast<size_t>(m_config.GetBlockAlign());
                if (blockAlign > 0) {
                    const size_t frameCount = static_cast<size_t>(dwLen) / blockAlign;
                    const size_t selectedChannel = ResolveCaptureChannelIndex(
                        reinterpret_cast<const int16_t*>(pData),
                        frameCount,
                        static_cast<size_t>(m_config.nChannels));
                    m_audioRingBuffer->PushInterleavedPcm16(
                        reinterpret_cast<const int16_t*>(pData),
                        frameCount,
                        static_cast<size_t>(m_config.nChannels),
                        selectedChannel);

                    ProcessVadFromRing();
                }
            }

            m_recordedData.insert(m_recordedData.end(), pData, pData + dwLen);
            m_dwRecordedDataSize += dwLen;

            // Callback notification
            if (m_pCallback != nullptr)
            {
                m_pCallback->OnVoiceDataAvailable(pData, dwLen);
            }

            // Re-add buffer to continue recording
            if (m_hWaveIn != nullptr)
            {
                waveInAddBuffer(m_hWaveIn, pWaveHdr, sizeof(WAVEHDR));
            }
        }
        else
        {
            if (m_hWaveIn != nullptr)
            {
                waveInAddBuffer(m_hWaveIn, pWaveHdr, sizeof(WAVEHDR));
            }
        }
    }
}

void CVoiceRecorder::AllocateBuffers()
{
    DWORD dwBufferSize = m_config.GetAvgBytesPerSec();

    m_pWaveHeaders = new WAVEHDR[m_nBufferCount];
    m_pBuffers = new BYTE * [m_nBufferCount];

    for (UINT i = 0; i < m_nBufferCount; i++)
    {
        m_pBuffers[i] = new BYTE[dwBufferSize];
        ZeroMemory(&m_pWaveHeaders[i], sizeof(WAVEHDR));
        m_pWaveHeaders[i].lpData = (LPSTR)m_pBuffers[i];
        m_pWaveHeaders[i].dwBufferLength = dwBufferSize;
    }
}

void CVoiceRecorder::FreeBuffers()
{
    if (m_pWaveHeaders != nullptr)
    {
        delete[] m_pWaveHeaders;
        m_pWaveHeaders = nullptr;
    }

    if (m_pBuffers != nullptr)
    {
        for (UINT i = 0; i < m_nBufferCount; i++)
        {
            if (m_pBuffers[i] != nullptr)
            {
                delete[] m_pBuffers[i];
                m_pBuffers[i] = nullptr;
            }
        }
        delete[] m_pBuffers;
        m_pBuffers = nullptr;
    }
}

void CVoiceRecorder::PrepareBuffers()
{
    if (m_hWaveIn == nullptr || m_pWaveHeaders == nullptr)
    {
        return;
    }

    for (UINT i = 0; i < m_nBufferCount; i++)
    {
        waveInPrepareHeader(m_hWaveIn, &m_pWaveHeaders[i], sizeof(WAVEHDR));
    }
}

void CVoiceRecorder::UnprepareBuffers()
{
    if (m_hWaveIn == nullptr || m_pWaveHeaders == nullptr)
    {
        return;
    }

    for (UINT i = 0; i < m_nBufferCount; i++)
    {
        if (m_pWaveHeaders[i].dwFlags & WHDR_PREPARED)
        {
            waveInUnprepareHeader(m_hWaveIn, &m_pWaveHeaders[i], sizeof(WAVEHDR));
        }
    }
}

BOOL CVoiceRecorder::WriteWavToFile()
{
    if (m_szFilePath[0] == L'\0' || m_recordedData.empty())
    {
        return FALSE;
    }

    HANDLE hFile = CreateFileW(m_szFilePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        TRACE(L"CVoiceRecorder::WriteWavToFile: failed to create file\n");
        return FALSE;
    }

    DWORD dwWritten;
    const BYTE* pData = m_recordedData.data();
    DWORD dwDataSize = static_cast<DWORD>(m_recordedData.size());

    // RIFF header
    WriteFile(hFile, "RIFF", 4, &dwWritten, nullptr);
    DWORD nRiffSize = 36 + dwDataSize;
    WriteFile(hFile, &nRiffSize, 4, &dwWritten, nullptr);
    WriteFile(hFile, "WAVE", 4, &dwWritten, nullptr);

    // fmt chunk
    WriteFile(hFile, "fmt ", 4, &dwWritten, nullptr);
    DWORD nFmtSize = 16;
    WriteFile(hFile, &nFmtSize, 4, &dwWritten, nullptr);

    WAVEFORMATEX wfex = {};
    wfex.wFormatTag = WAVE_FORMAT_PCM;
    wfex.nChannels = (WORD)m_config.nChannels;
    wfex.nSamplesPerSec = m_config.nSamplesPerSec;
    wfex.wBitsPerSample = (WORD)m_config.nBitsPerSample;
    wfex.nBlockAlign = (WORD)m_config.GetBlockAlign();
    wfex.nAvgBytesPerSec = wfex.nBlockAlign * m_config.nSamplesPerSec;
    wfex.cbSize = 0;
    WriteFile(hFile, &wfex, 16, &dwWritten, nullptr);

    // data chunk
    WriteFile(hFile, "data", 4, &dwWritten, nullptr);
    WriteFile(hFile, &dwDataSize, 4, &dwWritten, nullptr);
    WriteFile(hFile, pData, dwDataSize, &dwWritten, nullptr);

    CloseHandle(hFile);
    TRACE(L"CVoiceRecorder::WriteWavToFile: saved %lu bytes to %s\n", dwDataSize, m_szFilePath);
    return TRUE;
}

int CVoiceRecorder::GetInputDeviceCount()
{
    return waveInGetNumDevs();
}

BOOL CVoiceRecorder::GetInputDeviceName(int nDeviceIndex, wchar_t* pszName, int nNameLength)
{
    if (pszName == nullptr || nNameLength <= 0)
    {
        return FALSE;
    }

    WAVEINCAPSW wic = {};
    MMRESULT mmResult = waveInGetDevCapsW((UINT)nDeviceIndex, &wic, sizeof(WAVEINCAPSW));
    if (mmResult != MMSYSERR_NOERROR)
    {
        return FALSE;
    }

    wcsncpy_s(pszName, nNameLength, wic.szPname, nNameLength - 1);
    pszName[nNameLength - 1] = L'\0';
    return TRUE;
}

BOOL CVoiceRecorder::SetInputDevice(int nDeviceIndex)
{
    m_nDeviceID = (UINT)nDeviceIndex;
    return TRUE;
}

bool CVoiceRecorder::ReadLatestSamples(
    std::vector<float>& out,
    size_t sampleCount) const
{
    if (m_audioRingBuffer == nullptr) {
        out.clear();
        return false;
    }

    return m_audioRingBuffer->PeekLatest(
        out,
        sampleCount,
        AudioRingBuffer::kDefaultReadSpinCount);
}

bool CVoiceRecorder::ReadSamplesBySequence(
    std::vector<float>& out,
    uint64_t startSequence,
    size_t sampleCount)
{
    if (m_audioRingBuffer == nullptr) {
        out.clear();
        return false;
    }

    bool ok = m_audioRingBuffer->ReadWindowBySequence(
        out,
        startSequence,
        sampleCount,
        AudioRingBuffer::kDefaultReadSpinCount);
    if (ok && !out.empty()) {
        std::uint64_t nearZeroCount = 0;
        double sumSquares = 0.0;
        for (const float sample : out) {
            if (std::fabs(sample) <= 1.0e-6f) {
                ++nearZeroCount;
            }
            sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
        }
        const std::uint64_t sampleCountLocal = static_cast<std::uint64_t>(out.size());
        const double rms = std::sqrt(sumSquares / static_cast<double>(out.size()));
        const std::uint64_t rmsPermille = static_cast<std::uint64_t>((std::min)(
            rms * 1000.0,
            1000000.0));
        const std::uint64_t nearZeroPermille = sampleCountLocal > 0
            ? (nearZeroCount * 1000ULL) / sampleCountLocal
            : 0ULL;
        m_telemetry.captureProbeSampleCount = sampleCountLocal;
        m_telemetry.captureProbeNearZeroSamplePermille = nearZeroPermille;
        m_telemetry.captureProbeRmsPermille = rmsPermille;
        m_telemetry.captureProbeWeakSignal =
            nearZeroPermille >= 980ULL || rmsPermille <= 1ULL;
    }
    return ok;
}

uint64_t CVoiceRecorder::GetRingLatestSequence() const
{
    if (m_audioRingBuffer == nullptr) {
        return 0;
    }

    return m_audioRingBuffer->GetLatestSequence();
}

uint64_t CVoiceRecorder::GetRingOldestAvailableSequence() const
{
    if (m_audioRingBuffer == nullptr) {
        return 0;
    }

    return m_audioRingBuffer->GetOldestAvailableSequence();
}

std::optional<blazeclaw::core::speechrecognition::SpeechAudioArtifact>
CVoiceRecorder::BuildStreamingAudioArtifact() const
{
    if (m_audioRingBuffer == nullptr || m_config.nSamplesPerSec == 0) {
        return std::nullopt;
    }

    const uint64_t oldestAvailable = m_audioRingBuffer->GetOldestAvailableSequence();
    const uint64_t sequenceStart = m_recordingStartSequence > 0
        ? m_recordingStartSequence
        : oldestAvailable;
    const uint64_t latestSequence = m_audioRingBuffer->GetLatestSequence();
    const bool liveRecording = m_state == VoiceRecorderState::Recording;
    if (!liveRecording && latestSequence <= sequenceStart) {
        TRACE(
            "[VoiceRecorder][artifact.final.invalid] reason=empty_or_reversed_range start=%llu latest=%llu oldestAvailable=%llu\n",
            static_cast<unsigned long long>(sequenceStart),
            static_cast<unsigned long long>(latestSequence),
            static_cast<unsigned long long>(oldestAvailable));
        return std::nullopt;
    }

    const bool startBeforeOldest = sequenceStart < oldestAvailable;

    blazeclaw::core::speechrecognition::SpeechAudioArtifact artifact;
    artifact.handoffMode =
        blazeclaw::core::speechrecognition::SpeechAudioHandoffMode::PcmStream;
    artifact.path = ToNarrow(m_szFilePath);
    artifact.streamId = kVoiceRecorderStreamId;
    artifact.mimeType = "audio/pcm";
    artifact.container = "pcm_s16le";
    artifact.sampleRate = m_config.nSamplesPerSec;
    artifact.channels = 1;
    artifact.bitsPerSample = m_config.nBitsPerSample;
    artifact.frameSamples = static_cast<std::uint32_t>(m_config.nSamplesPerSec / 100);
    artifact.sequenceStart = sequenceStart;
    artifact.sequenceEnd = liveRecording ? 0ULL : latestSequence;
    artifact.captureChannelIndex = static_cast<std::uint32_t>(m_selectedCaptureChannelIndex);
    artifact.captureChannelEnergyPermille = m_telemetry.captureChannelEnergyPermille;

    const uint64_t availableSamples = latestSequence > sequenceStart
        ? latestSequence - sequenceStart
        : 0ULL;
    const uint64_t durationMsRaw =
        (availableSamples * 1000ULL) / static_cast<uint64_t>(m_config.nSamplesPerSec);
    artifact.durationMs = static_cast<std::uint32_t>((std::min)(
        durationMsRaw,
        static_cast<uint64_t>((std::numeric_limits<std::uint32_t>::max)())));
    TRACE(
        "[VoiceRecorder][artifact.%S] streamId=%S start=%llu end=%llu oldestAvailable=%llu latest=%llu startBeforeOldest=%d sampleRate=%u channels=%u durationMs=%u\n",
        liveRecording ? "preview" : "final",
        artifact.streamId.c_str(),
        static_cast<unsigned long long>(artifact.sequenceStart),
        static_cast<unsigned long long>(artifact.sequenceEnd),
        static_cast<unsigned long long>(oldestAvailable),
        static_cast<unsigned long long>(latestSequence),
        startBeforeOldest ? 1 : 0,
        artifact.sampleRate,
        artifact.channels,
        artifact.durationMs);
    return artifact;
}

void CVoiceRecorder::UpdateSessionState(
    blazeclaw::core::speechrecognition::SpeechSessionStage stage)
{
    m_sessionState.stage = stage;
    m_sessionState.segment.reset();
    if (stage == blazeclaw::core::speechrecognition::SpeechSessionStage::Stopped)
    {
        m_sessionState.audioPath = ToNarrow(m_szFilePath);
    }
}

void CVoiceRecorder::NotifySessionState(
    blazeclaw::core::speechrecognition::SpeechSessionStage stage)
{
    UpdateSessionState(stage);
    if (m_pCallback != nullptr)
    {
        m_pCallback->OnVoiceSessionChanged(m_sessionState);
    }
}

void CVoiceRecorder::ResetVadState()
{
    m_vadNextSequence = 0;
    m_vadSpeechStartSequence = 0;
    m_vadLastSpeechSequence = 0;
    m_vadSilenceSamples = 0;
    m_vadBoundarySignalSequence = 0;
    m_vadSpeechActive = false;
    m_vadFrameBuffer.clear();
    m_telemetry = {};
}

void CVoiceRecorder::ProcessVadFromRing()
{
    ProcessVadFromRingWithLatency(0);
}

void CVoiceRecorder::ProcessVadFromRingWithLatency(uint64_t enqueueLatencyUs)
{
    if (!m_config.vadEnabled ||
        m_audioRingBuffer == nullptr ||
        m_vadProvider == nullptr ||
        m_config.nSamplesPerSec == 0) {
        return;
    }

    const size_t frameSamples = m_config.GetVadFrameSamples();
    if (frameSamples == 0) {
        return;
    }

    const uint64_t oldestAvailable = m_audioRingBuffer->GetOldestAvailableSequence();
    const uint64_t latestAvailable = m_audioRingBuffer->GetLatestSequence();

    if (m_vadNextSequence < oldestAvailable) {
        m_vadNextSequence = oldestAvailable;
    }

    const uint64_t silenceThresholdSamples =
        DurationMsToSamples(m_config, static_cast<uint32_t>(m_config.vadSilenceDurationMs));
    const uint64_t maxUtteranceSamples =
        DurationMsToSamples(m_config, static_cast<uint32_t>(m_config.vadMaxUtteranceMs));

    while (m_vadNextSequence + static_cast<uint64_t>(frameSamples) <= latestAvailable) {
        if (!m_audioRingBuffer->ReadWindowBySequence(
            m_vadFrameBuffer,
            m_vadNextSequence,
            frameSamples,
            AudioRingBuffer::kDefaultReadSpinCount)) {
            break;
        }

        const bool frameIsSpeech = m_vadProvider->IsSpeech(
            m_vadFrameBuffer.data(),
            m_vadFrameBuffer.size(),
            m_config.nSamplesPerSec);

        const uint64_t frameStart = m_vadNextSequence;
        const uint64_t frameEnd = m_vadNextSequence + static_cast<uint64_t>(frameSamples);

        if (frameIsSpeech) {
            if (!m_vadSpeechActive) {
                m_vadSpeechActive = true;
                m_vadSpeechStartSequence = frameStart;
                m_vadLastSpeechSequence = frameEnd;
                m_vadSilenceSamples = 0;
                EmitBoundarySignal(
                    VoiceBoundarySignalType::SpeechStartCandidate,
                    frameStart,
                    frameEnd,
                    enqueueLatencyUs);
            }
            else {
                m_vadLastSpeechSequence = frameEnd;
                m_vadSilenceSamples = 0;
            }
        }
        else if (m_vadSpeechActive) {
            m_vadSilenceSamples += static_cast<uint64_t>(frameSamples);
            if (silenceThresholdSamples > 0 &&
                m_vadSilenceSamples >= silenceThresholdSamples) {
                EmitBoundarySignal(
                    VoiceBoundarySignalType::SpeechEndCandidate,
                    m_vadSpeechStartSequence,
                    m_vadLastSpeechSequence,
                    enqueueLatencyUs);
                m_vadSpeechActive = false;
                m_vadSilenceSamples = 0;
            }
        }

        if (m_vadSpeechActive && maxUtteranceSamples > 0 &&
            frameEnd - m_vadSpeechStartSequence >= maxUtteranceSamples) {
            EmitBoundarySignal(
                VoiceBoundarySignalType::MaxUtteranceTimeout,
                m_vadSpeechStartSequence,
                frameEnd,
                enqueueLatencyUs);
            m_vadSpeechActive = false;
            m_vadSilenceSamples = 0;
        }

        m_vadNextSequence = frameEnd;
    }
}

void CVoiceRecorder::EmitBoundarySignal(
    VoiceBoundarySignalType type,
    uint64_t startSequence,
    uint64_t endSequence,
    uint64_t chunkLatencyUs)
{
    if (endSequence <= startSequence || m_config.nSamplesPerSec == 0) {
        return;
    }

    VoiceBoundarySignal signal;
    signal.type = type;
    signal.startSequence = startSequence;
    signal.endSequence = endSequence;
    signal.chunkLatencyUs = chunkLatencyUs;
    signal.durationMs = static_cast<uint32_t>(
        ((endSequence - startSequence) * 1000ULL) /
        static_cast<uint64_t>(m_config.nSamplesPerSec));

    m_telemetry.boundarySignalCount += 1;
    m_telemetry.silenceSegmentationLatencyUs = chunkLatencyUs;

    m_sessionState.segment = blazeclaw::core::speechrecognition::SpeechTranscriptSegment{
        .text = type == VoiceBoundarySignalType::SpeechStartCandidate
            ? "speech_start_candidate"
            : (type == VoiceBoundarySignalType::SpeechEndCandidate
                ? "speech_end_candidate"
                : "max_utterance_timeout"),
        .final = type != VoiceBoundarySignalType::SpeechStartCandidate,
        .sequence = ++m_vadBoundarySignalSequence,
    };

    if (m_pCallback != nullptr) {
        m_pCallback->OnVoiceBoundarySignal(signal);
        m_pCallback->OnVoiceSessionChanged(m_sessionState);
    }
}

VoiceRecorderTelemetry CVoiceRecorder::GetTelemetrySnapshot() const
{
    return m_telemetry;
}

void CVoiceRecorder::PushPcm16ChunkForTest(
    const int16_t* data,
    size_t frameCount,
    size_t channelCount,
    uint64_t enqueueLatencyUs)
{
    if (data == nullptr || frameCount == 0 || channelCount == 0) {
        return;
    }

    if (m_audioRingBuffer == nullptr) {
        m_audioRingBuffer =
            std::make_unique<AudioRingBuffer>(ResolveRingCapacitySamples(m_config));
    }

    if (m_vadProvider == nullptr) {
        m_vadProvider = CreateVadProvider(m_config.vadProviderType);
    }

    const size_t selectedChannel = ResolveCaptureChannelIndex(
        data,
        frameCount,
        channelCount);

    m_audioRingBuffer->PushInterleavedPcm16(
        data,
        frameCount,
        channelCount,
        selectedChannel);

    m_telemetry.chunkEnqueueLatencyUs = enqueueLatencyUs;
    const uint64_t ringCapacity =
        static_cast<uint64_t>(m_audioRingBuffer->GetCapacitySamples());
    const uint64_t available =
        m_audioRingBuffer->GetLatestSequence() -
        m_audioRingBuffer->GetOldestAvailableSequence();
    m_telemetry.ringOccupancyPercent = ringCapacity == 0
        ? 0
        : (available * 100ULL) / ringCapacity;
    m_telemetry.ringDroppedSamples = m_audioRingBuffer->GetDroppedSamples();

    ProcessVadFromRingWithLatency(enqueueLatencyUs);
}

std::unique_ptr<IVoiceVadProvider> CVoiceRecorder::CreateVadProvider(
    VoiceVadProviderType providerType) const
{
    switch (providerType) {
    case VoiceVadProviderType::Nvidia:
        return std::make_unique<NvidiaVoiceVadProvider>();
    case VoiceVadProviderType::NoOp:
    default:
        return std::make_unique<NoOpVoiceVadProvider>();
    }
}

void CVoiceRecorder::ResetCaptureChannelSelection()
{
    m_captureAdaptiveObservedFrames = 0;
    m_captureAdaptiveObservedFramesSinceLock = 0;
    m_captureAdaptiveStableChunks = 0;
    m_captureAdaptiveLateReselectionUsed = false;
    m_captureAdaptiveLastBestChannel = 0;
    m_captureAdaptiveEnergyByChannel.clear();

    const size_t configuredChannel = static_cast<size_t>(m_config.ringCaptureChannelIndex);
    const bool fixedOverride = m_config.ringCaptureChannelFixedOverride;
    const bool adaptiveEnabled = m_config.adaptiveRingCaptureChannelEnabled && !fixedOverride;

    m_selectedCaptureChannelIndex = configuredChannel;
    m_captureChannelLocked = fixedOverride || !adaptiveEnabled;

    m_telemetry.captureChannelIndex = static_cast<uint64_t>(m_selectedCaptureChannelIndex);
    m_telemetry.captureChannelEnergyPermille = 0;
    m_telemetry.captureAdaptiveEnabled = adaptiveEnabled;
    m_telemetry.captureAdaptiveLocked = m_captureChannelLocked;
    m_telemetry.captureAdaptiveObservedFrames = 0;
}

size_t CVoiceRecorder::ResolveCaptureChannelIndex(
    const int16_t* data,
    size_t frameCount,
    size_t channelCount)
{
    if (channelCount == 0) {
        return 0;
    }

    const size_t maxChannelIndex = channelCount - 1;
    const size_t configuredChannel = (std::min)(
        static_cast<size_t>(m_config.ringCaptureChannelIndex),
        maxChannelIndex);

    if (m_config.ringCaptureChannelFixedOverride || !m_config.adaptiveRingCaptureChannelEnabled) {
        m_selectedCaptureChannelIndex = configuredChannel;
        m_captureChannelLocked = true;
        m_telemetry.captureChannelIndex = static_cast<uint64_t>(m_selectedCaptureChannelIndex);
        m_telemetry.captureAdaptiveEnabled = false;
        m_telemetry.captureAdaptiveLocked = true;
        return m_selectedCaptureChannelIndex;
    }

    if (data == nullptr || frameCount == 0 || channelCount == 1) {
        m_selectedCaptureChannelIndex = configuredChannel;
        m_telemetry.captureChannelIndex = static_cast<uint64_t>(m_selectedCaptureChannelIndex);
        return m_selectedCaptureChannelIndex;
    }

    if (m_captureAdaptiveEnergyByChannel.size() != channelCount) {
        m_captureAdaptiveEnergyByChannel.assign(channelCount, 0.0);
    }

    std::vector<double> chunkEnergyByChannel(channelCount, 0.0);
    for (size_t frame = 0; frame < frameCount; ++frame) {
        const size_t frameOffset = frame * channelCount;
        for (size_t channel = 0; channel < channelCount; ++channel) {
            const float sample = static_cast<float>(data[frameOffset + channel]) / 32768.0f;
            const double energy = static_cast<double>(sample) * static_cast<double>(sample);
            chunkEnergyByChannel[channel] += energy;
            m_captureAdaptiveEnergyByChannel[channel] += energy;
        }
    }

    size_t bestChannel = 0;
    double bestChunkEnergy = chunkEnergyByChannel[0];
    for (size_t channel = 1; channel < channelCount; ++channel) {
        if (chunkEnergyByChannel[channel] > bestChunkEnergy) {
            bestChunkEnergy = chunkEnergyByChannel[channel];
            bestChannel = channel;
        }
    }

    if (bestChannel == m_captureAdaptiveLastBestChannel) {
        ++m_captureAdaptiveStableChunks;
    }
    else {
        m_captureAdaptiveStableChunks = 1;
        m_captureAdaptiveLastBestChannel = bestChannel;
    }

    m_captureAdaptiveObservedFrames += static_cast<uint64_t>(frameCount);
    if (m_captureChannelLocked) {
        m_captureAdaptiveObservedFramesSinceLock += static_cast<uint64_t>(frameCount);
    }
    const uint64_t decisionFrames = (std::max)(
        static_cast<uint64_t>(1),
        static_cast<uint64_t>(m_config.adaptiveRingCaptureDecisionFrames));
    const uint64_t minStableChunks = (std::max)(
        static_cast<uint64_t>(1),
        static_cast<uint64_t>(m_config.adaptiveRingCaptureMinStableChunks));
    const uint64_t relockWindowFrames = (std::max)(
        static_cast<uint64_t>(1),
        static_cast<uint64_t>(m_config.adaptiveRingCaptureRelockWindowFrames));
    const uint64_t shortRunDecisionFrames = (std::max)(
        static_cast<uint64_t>(1),
        decisionFrames / 2);
    const bool allowShortRunLock =
        m_captureAdaptiveObservedFrames >= shortRunDecisionFrames &&
        m_captureAdaptiveStableChunks >= (std::max)(
            static_cast<uint64_t>(1),
            minStableChunks / 2);
    if (!m_captureChannelLocked &&
        ((m_captureAdaptiveObservedFrames >= decisionFrames &&
            m_captureAdaptiveStableChunks >= minStableChunks) ||
            allowShortRunLock)) {
        m_selectedCaptureChannelIndex = bestChannel;
        m_captureChannelLocked = true;
        m_captureAdaptiveObservedFramesSinceLock = 0;
    }

    if (m_captureChannelLocked) {
        const size_t lockedChannel = (std::min)(m_selectedCaptureChannelIndex, maxChannelIndex);
        const double lockedChunkEnergy = chunkEnergyByChannel[lockedChannel];
        const std::uint64_t lockedEnergyPermille = static_cast<std::uint64_t>(
            (std::min)(
                frameCount > 0
                    ? (lockedChunkEnergy / static_cast<double>(frameCount)) * 1000.0
                    : 0.0,
                1000000.0));
        const bool relockAllowed = m_captureAdaptiveObservedFramesSinceLock >= relockWindowFrames;
        const bool lateReselectAllowed =
            !m_captureAdaptiveLateReselectionUsed &&
            m_captureAdaptiveObservedFramesSinceLock >= shortRunDecisionFrames;
        const bool lockedEnergyCollapsed =
            lockedEnergyPermille <= static_cast<std::uint64_t>(m_config.adaptiveRingCaptureRelockFloorPermille);
        if ((relockAllowed || lateReselectAllowed) &&
            lockedEnergyCollapsed &&
            bestChannel != lockedChannel) {
            m_captureChannelLocked = false;
            m_captureAdaptiveStableChunks = 1;
            m_captureAdaptiveLastBestChannel = bestChannel;
            m_captureAdaptiveObservedFramesSinceLock = 0;
            m_captureAdaptiveLateReselectionUsed = true;
        }
    }

    if (!m_captureChannelLocked) {
        m_selectedCaptureChannelIndex = bestChannel;
    }

    const size_t selectedChannel = (std::min)(m_selectedCaptureChannelIndex, maxChannelIndex);
    const double selectedEnergy = chunkEnergyByChannel[selectedChannel];
    const double averageSelectedEnergy = frameCount > 0
        ? selectedEnergy / static_cast<double>(frameCount)
        : 0.0;
    const uint64_t selectedEnergyPermille = static_cast<uint64_t>(
        (std::min)(averageSelectedEnergy * 1000.0, 1000000.0));

    m_telemetry.captureChannelIndex = static_cast<uint64_t>(selectedChannel);
    m_telemetry.captureChannelEnergyPermille = selectedEnergyPermille;
    m_telemetry.captureAdaptiveEnabled = true;
    m_telemetry.captureAdaptiveLocked = m_captureChannelLocked;
    m_telemetry.captureAdaptiveObservedFrames = m_captureAdaptiveObservedFrames;

    return selectedChannel;
}
