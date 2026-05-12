#pragma once

#include <mmsystem.h>
#include <mmreg.h>
#include <vector>

#pragma comment(lib, "winmm.lib")

// Recording state
enum class VoiceRecorderState
{
    Idle,       // Idle
    Recording,  // Recording in progress
    Paused      // Paused
};

// Recording configuration
struct VoiceRecorderConfig
{
    UINT nChannels = 1;           // Channels: 1 = mono
    UINT nSamplesPerSec = 16000; // Sample rate: 16000Hz
    UINT nBitsPerSample = 16;     // Bits per sample: 16bit

    DWORD GetAvgBytesPerSec() const
    {
        return nSamplesPerSec * nChannels * nBitsPerSample / 8;
    }

    DWORD GetBlockAlign() const
    {
        return nChannels * nBitsPerSample / 8;
    }
};

// Recording data callback interface
class IVoiceRecorderCallback
{
public:
    virtual ~IVoiceRecorderCallback() = default;
    virtual void OnVoiceDataAvailable(const BYTE* pData, DWORD dwLength) = 0;
    virtual void OnVoiceStateChanged(VoiceRecorderState state) = 0;
    virtual void OnVoiceError(long nError, const wchar_t* pszDescription) = 0;
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

private:
    HWND           m_hNotifyWnd;
    HWAVEIN        m_hWaveIn;
    VoiceRecorderConfig m_config;
    VoiceRecorderState m_state;
    IVoiceRecorderCallback* m_pCallback;

    int            m_nDeviceID;
    UINT           m_nBufferCount;
    WAVEHDR*       m_pWaveHeaders;
    BYTE**         m_pBuffers;

    // File path for saving on stop
    wchar_t        m_szFilePath[MAX_PATH];
    // In-memory audio buffer (PCM)
    std::vector<BYTE> m_recordedData;
    DWORD          m_dwRecordedDataSize;

    BOOL           m_bInitialized;
};
