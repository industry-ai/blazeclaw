#include "pch.h"

#include "../BlazeClawMfc/src/app/VoiceRecorder.h"
#include "../BlazeClawMfc/src/app/ChatView.h"

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

bool CChatView::StartRecordingToPath(const CStringW&) {
	return true;
}

CStringW CChatView::StopRecordingAndGetPath() {
	return CStringW();
}
