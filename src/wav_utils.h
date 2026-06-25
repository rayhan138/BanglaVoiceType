/// @file wav_utils.h
/// @brief Header-only utility for encoding raw PCM audio samples into WAV format.
/// @details Converts a buffer of 16-bit signed PCM samples into a complete
///          RIFF/WAVE byte array suitable for uploading to speech-to-text APIs.
///          No dependencies beyond the C++ standard library.

#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

namespace WavUtils {

    /// @brief Standard WAV file header (44 bytes, RIFF/WAVE format).
    /// @note Uses #pragma pack to ensure no padding between fields,
    ///       matching the exact binary layout of a WAV file header.
    #pragma pack(push, 1)
    struct WavHeader {
        // RIFF chunk descriptor
        char     riffId[4]      = {'R', 'I', 'F', 'F'};
        uint32_t riffSize       = 0;        // File size - 8 bytes
        char     waveId[4]      = {'W', 'A', 'V', 'E'};

        // Format sub-chunk ("fmt ")
        char     fmtId[4]       = {'f', 'm', 't', ' '};
        uint32_t fmtSize        = 16;       // PCM format = 16 bytes
        uint16_t audioFormat    = 1;        // 1 = PCM (uncompressed)
        uint16_t numChannels    = 1;        // Mono
        uint32_t sampleRate     = 16000;    // 16 kHz (optimal for Whisper)
        uint32_t byteRate       = 32000;    // sampleRate * blockAlign
        uint16_t blockAlign     = 2;        // numChannels * bitsPerSample / 8
        uint16_t bitsPerSample  = 16;       // 16-bit signed integer

        // Data sub-chunk
        char     dataId[4]      = {'d', 'a', 't', 'a'};
        uint32_t dataSize       = 0;        // Raw PCM data size in bytes
    };
    #pragma pack(pop)

    // Compile-time validation: header must be exactly 44 bytes
    static_assert(sizeof(WavHeader) == 44, "WavHeader must be 44 bytes");

    /// @brief Encodes raw PCM samples into a complete WAV file (in memory).
    /// @param pcmSamples Vector of 16-bit signed PCM samples (16kHz, mono).
    /// @return Complete WAV file as a byte vector (header + PCM data).
    /// @note The returned vector can be directly uploaded to Groq/Google STT APIs.
    ///
    /// @par Example Usage:
    /// @code
    ///     std::vector<int16_t> samples = recorder.GetPCMData();
    ///     std::vector<uint8_t> wavFile = WavUtils::Encode(samples);
    ///     // wavFile is now a valid .wav file ready for API upload
    /// @endcode
    inline std::vector<uint8_t> Encode(const std::vector<int16_t>& pcmSamples) {
        const uint32_t dataSize = static_cast<uint32_t>(
            pcmSamples.size() * sizeof(int16_t)
        );

        // Populate the WAV header
        WavHeader header;
        header.dataSize = dataSize;
        header.riffSize = 36 + dataSize;  // 36 = header size (44) - 8 (RIFF chunk prefix)

        // Allocate output buffer: [44-byte header] + [PCM data]
        std::vector<uint8_t> wavFile(sizeof(WavHeader) + dataSize);

        // Copy header
        std::memcpy(wavFile.data(), &header, sizeof(WavHeader));

        // Copy PCM samples (as raw bytes)
        if (dataSize > 0) {
            std::memcpy(
                wavFile.data() + sizeof(WavHeader),
                pcmSamples.data(),
                dataSize
            );
        }

        return wavFile;
    }

    /// @brief Calculates the duration of a recording in seconds.
    /// @param sampleCount Number of PCM samples.
    /// @param sampleRate Sample rate in Hz (default: 16000).
    /// @return Duration in seconds.
    inline float GetDurationSeconds(size_t sampleCount, uint32_t sampleRate = 16000) {
        return static_cast<float>(sampleCount) / static_cast<float>(sampleRate);
    }

    /// @brief Maximum recording duration in seconds (to stay within API limits).
    constexpr uint32_t MAX_RECORDING_SECONDS = 720;  // 12 minutes

    /// @brief Maximum PCM samples for the max recording duration.
    constexpr size_t MAX_SAMPLES = 16000 * MAX_RECORDING_SECONDS;

} // namespace WavUtils
