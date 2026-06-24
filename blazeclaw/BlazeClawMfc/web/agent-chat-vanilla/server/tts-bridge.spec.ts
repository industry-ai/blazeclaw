import { afterEach, describe, expect, it, vi } from 'vitest'
import { resolveTtsAudioUrl } from './tts-bridge'

describe('tts-bridge audio URL resolution', () => {
  afterEach(() => {
    vi.unstubAllEnvs()
  })

  it('uses public audio URLs returned by moss tts', () => {
    expect(resolveTtsAudioUrl({
      audio_url: 'https://cdn.example.com/audio/voice.wav',
      audio_path: 'outputs/local.wav',
    })).toBe('https://cdn.example.com/audio/voice.wav')
  })

  it('maps audio_path through the configured public base URL', () => {
    vi.stubEnv('TTS_AUDIO_PUBLIC_BASE_URL', 'https://cdn.example.com/tts/')

    expect(resolveTtsAudioUrl({
      audio_path: 'C:\\tts\\outputs\\voice file.wav',
    })).toBe('https://cdn.example.com/tts/voice%20file.wav')
  })
})
