#include "pch.h"
#include "VoiceRecorder.h"

namespace {

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

} // namespace

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

    WAVEINCAPS wic = {};
    MMRESULT mmResult = waveInGetDevCaps((UINT)nDeviceIndex, &wic, sizeof(WAVEINCAPS));
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
