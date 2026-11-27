#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace gbcllr::detail {
// Private network identity projection. Native checkpoints stay unchanged.
std::vector<uint8_t> simulation_state(std::span<const uint8_t>,bool validate_local);
// Existing FNV master, left, right and wire/continuation digests in one pass.
std::array<uint32_t,4> simulation_digest(std::span<const uint8_t>,bool cycle);
}
