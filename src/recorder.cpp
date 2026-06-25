/// @file recorder.cpp
/// @brief Implementation of the Recorder class for audio capture via miniaudio.
/// @details This is the ONLY translation unit that defines MINIAUDIO_IMPLEMENTATION.
///          The miniaudio library is header-only; defining the implementation macro
///          here causes the library's function bodies to be compiled into this unit.

#include "recorder.h"

#include <vector>
#include <cstdint>
#include <mutex>
#include <cstring>

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>

// miniaudio implementation — MUST be defined in exactly ONE .cpp file
#define MINIAUDIO_IMPLEMENTATION
#include "../include/miniaudio.h"

#include "wav_utils.h"

// ---------------------------------------------------------------------------
// PIMPL Implementation Struct
// ---------------------------------------------------------------------------

/// @brief Hidden implementation details containing miniaudio objects.
/// @note This struct is only visible within this translation unit, keeping
///       miniaudio types out of the public header.
struct Recorder::Impl {
    ma_device        device;     ///< miniaudio capture device handle
    ma_device_config config;     ///< Device configuration (format, rate, channels)
    bool             deviceInit; ///< Whether ma_device_init succeeded

    Impl() : device{}, config{}, deviceInit(false) {}
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

/// @brief Constructs the Recorder and allocates PIMPL internals.
Recorder::Recorder()
    : m_impl(std::make_unique<Impl>())
    , m_isRecording(false) {
    OutputDebugStringW(L"[Recorder] Created.\n");
}

/// @brief Destructs the Recorder, ensuring any active recording is stopped.
Recorder::~Recorder() {
    if (m_isRecording) {
        StopRecording();
    }
    OutputDebugStringW(L"[Recorder] Destroyed.\n");
}

// ---------------------------------------------------------------------------
// Public Methods
// ---------------------------------------------------------------------------

/// @brief Starts capturing audio from the default input device.
/// @return true if recording started successfully, false on error.
/// @note Must be called from the main thread.
bool Recorder::StartRecording() {
    // If already recording, stop first
    if (m_isRecording) {
        OutputDebugStringW(L"[Recorder] Already recording — stopping first.\n");
        StopRecording();
    }

    // Clear any leftover PCM data from a previous recording
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_pcmBuffer.clear();
        m_pcmBuffer.reserve(AudioConfig::SAMPLE_RATE * 10);  // Pre-allocate ~10 seconds
    }

    // Configure the capture device
    m_impl->config = ma_device_config_init(ma_device_type_capture);
    m_impl->config.capture.format   = ma_format_s16;           // 16-bit signed integer
    m_impl->config.capture.channels = AudioConfig::CHANNELS;   // Mono
    m_impl->config.sampleRate       = AudioConfig::SAMPLE_RATE; // 16 kHz
    m_impl->config.dataCallback     = reinterpret_cast<ma_device_data_proc>(AudioCallback);
    m_impl->config.pUserData        = this;  // Pass Recorder* to callback

    // Initialize the device
    ma_result result = ma_device_init(NULL, &m_impl->config, &m_impl->device);
    if (result != MA_SUCCESS) {
        OutputDebugStringW(L"[Recorder] ERROR: ma_device_init failed.\n");
        m_impl->deviceInit = false;
        return false;
    }
    m_impl->deviceInit = true;

    // Start capturing
    result = ma_device_start(&m_impl->device);
    if (result != MA_SUCCESS) {
        OutputDebugStringW(L"[Recorder] ERROR: ma_device_start failed.\n");
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInit = false;
        return false;
    }

    m_isRecording = true;
    OutputDebugStringW(L"[Recorder] Recording started (16kHz, 16-bit, mono).\n");
    return true;
}

/// @brief Stops the current recording and releases the audio device.
/// @note Safe to call even if not currently recording (no-op).
///       Must be called from the main thread.
void Recorder::StopRecording() {
    if (!m_isRecording) {
        return;
    }

    m_isRecording = false;

    if (m_impl->deviceInit) {
        ma_device_stop(&m_impl->device);
        ma_device_uninit(&m_impl->device);
        m_impl->deviceInit = false;
    }

    // Log the recording duration
    float duration = GetDurationSeconds();
    wchar_t msg[128];
    swprintf_s(msg, L"[Recorder] Recording stopped. Duration: %.2f seconds.\n", duration);
    OutputDebugStringW(msg);
}

/// @brief Retrieves the captured PCM data and clears the internal buffer.
/// @return Vector of 16-bit signed PCM samples (moved out for efficiency).
/// @note Thread-safe via mutex. After calling this, the internal buffer is empty.
std::vector<int16_t> Recorder::GetPCMData() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);

    // Move the buffer out — caller takes ownership, internal buffer is cleared
    std::vector<int16_t> data = std::move(m_pcmBuffer);
    m_pcmBuffer.clear();  // Ensure consistent empty state after move

    return data;
}

/// @brief Checks if the recorder is currently capturing audio.
/// @return true if recording is in progress.
bool Recorder::IsRecording() const {
    return m_isRecording;
}

/// @brief Gets the duration of the current recording in seconds.
/// @return Duration in seconds, based on the number of buffered samples.
float Recorder::GetDurationSeconds() const {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    return WavUtils::GetDurationSeconds(m_pcmBuffer.size(), AudioConfig::SAMPLE_RATE);
}

// ---------------------------------------------------------------------------
// Static Audio Callback
// ---------------------------------------------------------------------------

/// @brief miniaudio data callback — invoked on the OS audio capture thread.
/// @param pDevice Pointer to the ma_device. pDevice->pUserData points to the Recorder.
/// @param pOutput Unused for capture-only devices.
/// @param pInput Pointer to captured audio frames (int16_t* for ma_format_s16).
/// @param frameCount Number of audio frames captured in this callback invocation.
///
/// @warning This function runs on a real-time audio thread managed by the OS.
///          Keep processing minimal. The mutex lock here is acceptable because
///          the main thread only briefly holds it during GetPCMData() calls.
void Recorder::AudioCallback(
    void* pDevice,
    void* pOutput,
    const void* pInput,
    uint32_t frameCount
) {
    // Suppress unused parameter warning for output (capture mode has no output)
    (void)pOutput;

    // Retrieve the Recorder instance from the device's user data
    ma_device* device = static_cast<ma_device*>(pDevice);
    Recorder* recorder = static_cast<Recorder*>(device->pUserData);

    if (recorder == nullptr || pInput == nullptr || frameCount == 0) {
        return;
    }

    const int16_t* samples = static_cast<const int16_t*>(pInput);

    // Lock the buffer mutex and append captured frames
    {
        std::lock_guard<std::mutex> lock(recorder->m_bufferMutex);

        // Enforce maximum recording duration to stay within API limits
        size_t currentSamples = recorder->m_pcmBuffer.size();
        size_t samplesToAdd = static_cast<size_t>(frameCount) * AudioConfig::CHANNELS;

        if (currentSamples >= WavUtils::MAX_SAMPLES) {
            // Maximum recording duration reached — discard new samples
            return;
        }

        // Clamp if adding all frames would exceed the limit
        size_t remaining = WavUtils::MAX_SAMPLES - currentSamples;
        if (samplesToAdd > remaining) {
            samplesToAdd = remaining;
        }

        recorder->m_pcmBuffer.insert(
            recorder->m_pcmBuffer.end(),
            samples,
            samples + samplesToAdd
        );
    }
}
