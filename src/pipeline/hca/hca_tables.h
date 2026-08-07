#pragma once

#include <cstdint>
#include <vector>

// Ported from VGAudio by Alex Barney (MIT License).
// Source tables: VGAudio.Codecs.CriHca.CriHcaTables.
namespace ff7r::hca::tables
{
extern const std::vector<std::vector<std::uint8_t>> quantize_spectrum_bits;
extern const std::vector<std::vector<std::uint8_t>> quantize_spectrum_value;
extern const std::vector<std::uint8_t> quantized_spectrum_max_bits;
extern const std::vector<std::uint8_t> scale_to_resolution_curve;
extern const std::vector<double> mdct_window;
}
