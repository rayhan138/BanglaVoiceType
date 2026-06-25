/// @file recorder.h
/// @brief Audio recorder class for capturing microphone input as raw PCM data.
/// @details Uses the PIMPL idiom to hide miniaudio library internals from
///          consumers. Captures audio in 16kHz, 16-bit signed integer, mono
///          format — the optimal input format for Whisper-based STT APIs.
///
/// @par Thread Safety:
///     The audio callback runs on a dedicated OS audio thread. All access to
///     the internal PCM buffer is protected by a std::mutex. Public methods
///     are safe to call from the main (UI) thread.

#pragma once

#include <vector>
#include <cstdint>
#include <mutex>
#include <memory>

/// @brief Audio capture configuration constants.
namespace AudioConfig {
    constexpr uint32_t SAMPLE_RATE     = 16000;   ///< 16 kHz sample rate (optimal for Whisper)
    constexpr uint32_t CHANNELS        = 1;       ///< Mono audio
    constexpr uint32_t BITS_PER_SAMPLE = 16;      ///< 16-bit signed integer PCM
}

/// @class Recorder
/// @brief Captures microphone audio into a raw PCM buffer using miniaudio.
///
/// @par Typical Usage:
/// @code
///     Recorder recorder;
///     recorder.StartRecording();
///     // ... wait for user to stop ...
///     recorder.StopRecording();
///     auto pcmData = recorder.GetPCMData();
///     auto wav = WavUtils::Encode(pcmData);
/// @endcode
///
/// @note Only one Recorder instance should be active at a time. Creating
///       multiple simultaneous recorders may cause device conflicts.
class Recorder {
public:
    /// @brief Constructs the Recorder and initializes PIMPL internals.
    Recorder();

    /// @brief Destructs the Recorder, stopping any active recording.
    ~Recorder();

    // Non-copyable, non-movable (owns audio device handle)
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    /// @brief Starts capturing audio from the default input device.
    /// @return true if recording started successfully, false on error.
    /// @note Must be called from the main thread. If already recording,
    ///       this will stop the current recording first.
    bool StartRecording();

    /// @brief Stops the current recording.
    /// @note Safe to call even if not currently recording (no-op).
    ///       Must be called from the main thread.
    void StopRecording();

    /// @brief Retrieves the captured PCM data and clears the internal buffer.
    /// @return Vector of 16-bit signed PCM samples. The internal buffer is
    ///         moved out and cleared, so subsequent calls return empty vectors.
    /// @note Thread-safe. Can be called while recording (snapshot) or after.
    std::vector<int16_t> GetPCMData();

    /// @brief Checks if the recorder is currently capturing audio.
    /// @return true if recording is in progress.
    bool IsRecording() const;

    /// @brief Gets the duration of the current recording in seconds.
    /// @return Duration in seconds, calculated from the sample count.
    float GetDurationSeconds() const;

    /// @brief miniaudio data callback — invoked on the audio capture thread.
    /// @param pDevice Pointer to the ma_device (userdata points to Recorder).
    /// @param pOutput Unused (capture mode — no output).
    /// @param pInput Pointer to captured audio frames (int16_t samples).
    /// @param frameCount Number of frames available in this callback.
    /// @warning This runs on a real-time audio thread. Must be lock-free or
    ///          use minimal locking. Do NOT call blocking APIs here.
    static void AudioCallback(
        void* pDevice,
        void* pOutput,
        const void* pInput,
        uint32_t frameCount
    );

private:
    /// @brief Forward-declared PIMPL struct containing miniaudio objects.
    struct Impl;

    /// @brief PIMPL pointer — hides ma_device, ma_device_config.
    std::unique_ptr<Impl> m_impl;

    /// @brief Raw PCM sample buffer (16-bit signed, mono, 16kHz).
    std::vector<int16_t> m_pcmBuffer;

    /// @brief Mutex protecting m_pcmBuffer (shared between audio thread and main thread).
    mutable std::mutex m_bufferMutex;

    /// @brief Whether a recording is currently in progress.
    bool m_isRecording = false;
};
