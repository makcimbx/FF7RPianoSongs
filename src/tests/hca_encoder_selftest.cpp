#include "pipeline/hca/hca_encoder.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int fail(const std::string& message)
{
    std::cerr << message << '\n';
    return 1;
}

std::uint16_t read_u16_be(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint32_t read_u32_be(const std::vector<std::uint8_t>& bytes, const std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
        static_cast<std::uint32_t>(bytes[offset + 3]);
}

} // namespace

int main()
{
    try {
        std::vector<std::int16_t> pcm(996000u * 2u, 0);
        const std::vector<std::uint8_t> hca = encode_hca_48k_stereo_256k(pcm);

        if (hca.size() != FF7R_HCA_EXPECTED_BYTES) {
            return fail("unexpected HCA byte count");
        }
        if (hca[0] != 'H' || hca[1] != 'C' || hca[2] != 'A' || hca[3] != 0) {
            return fail("missing HCA header magic");
        }
        if (read_u16_be(hca, 6) != FF7R_HCA_EXPECTED_DATA_OFFSET) {
            return fail("unexpected HCA data offset");
        }
        if (read_u32_be(hca, 16) != FF7R_HCA_EXPECTED_BLOCK_COUNT) {
            return fail("unexpected HCA block count");
        }
        if (read_u16_be(hca, 28) != FF7R_HCA_EXPECTED_BLOCK_SIZE) {
            return fail("unexpected HCA block size");
        }
        if (hca[FF7R_HCA_EXPECTED_DATA_OFFSET] != 0xff || hca[FF7R_HCA_EXPECTED_DATA_OFFSET + 1] != 0xff) {
            return fail("first frame sync is not 0xffff");
        }

        HcaEncodeConfig variable_config;
        variable_config.target_samples = 48000;
        std::vector<std::int16_t> variable_pcm(variable_config.target_samples * 2u, 0);
        const std::vector<std::uint8_t> variable_hca = encode_hca_48k_stereo_256k(variable_pcm, variable_config);
        constexpr std::uint32_t kVariableFrames = 47;
        const std::size_t variable_bytes = FF7R_HCA_EXPECTED_DATA_OFFSET +
            static_cast<std::size_t>(kVariableFrames) * FF7R_HCA_EXPECTED_BLOCK_SIZE;
        if (variable_hca.size() != variable_bytes || read_u32_be(variable_hca, 16) != kVariableFrames) {
            return fail("variable HCA frame count or byte size is incorrect");
        }
        if (read_u16_be(variable_hca, 20) != FF7R_HCA_INSERTED_SAMPLES || read_u16_be(variable_hca, 22) != 0) {
            return fail("variable HCA inserted/appended samples are incorrect");
        }

        for (const std::size_t target : {1u, 896u, 897u}) {
            HcaEncodeConfig boundary_config;
            boundary_config.target_samples = target;
            std::vector<std::int16_t> boundary_pcm(target * 2u, 0);
            const auto boundary_hca = encode_hca_48k_stereo_256k(boundary_pcm, boundary_config);
            const std::uint32_t frames = static_cast<std::uint32_t>((target + FF7R_HCA_INSERTED_SAMPLES +
                FF7R_HCA_SAMPLES_PER_FRAME - 1u) / FF7R_HCA_SAMPLES_PER_FRAME);
            const std::uint16_t appended = static_cast<std::uint16_t>(
                frames * FF7R_HCA_SAMPLES_PER_FRAME - target - FF7R_HCA_INSERTED_SAMPLES);
            if (read_u32_be(boundary_hca, 16) != frames || read_u16_be(boundary_hca, 22) != appended) {
                return fail("HCA frame/appended-sample boundary is incorrect");
            }
        }

        bool rejected_empty = false;
        try {
            HcaEncodeConfig empty_config;
            empty_config.target_samples = 0;
            (void)encode_hca_48k_stereo_256k({}, empty_config);
        } catch (const std::exception&) {
            rejected_empty = true;
        }
        if (!rejected_empty) {
            return fail("empty HCA input was accepted");
        }

        std::vector<std::uint8_t> concurrent_a;
        std::vector<std::uint8_t> concurrent_b;
        std::thread first([&] { concurrent_a = encode_hca_48k_stereo_256k(variable_pcm, variable_config); });
        std::thread second([&] { concurrent_b = encode_hca_48k_stereo_256k(variable_pcm, variable_config); });
        first.join();
        second.join();
        if (concurrent_a != variable_hca || concurrent_b != variable_hca) {
            return fail("concurrent HCA encodes were not byte-identical to the serial baseline");
        }
    } catch (const std::exception& ex) {
        return fail(std::string("HCA encode failed: ") + ex.what());
    }

    std::cout << "hca_encoder_selftest ok\n";
    return 0;
}
