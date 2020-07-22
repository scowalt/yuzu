// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <cstring>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/bit_util.h"
#include "video_core/gpu.h"
#include "video_core/textures/decoders.h"
#include "video_core/textures/texture.h"

namespace Tegra::Texture {
namespace {

/**
 * This table represents the internal swizzle of a gob,
 * in format 16 bytes x 2 sector packing.
 * Calculates the offset of an (x, y) position within a swizzled texture.
 * Taken from the Tegra X1 Technical Reference Manual. pages 1187-1188
 */
template <std::size_t N, std::size_t M, u32 Align>
struct alignas(64) SwizzleTable {
    static_assert(M * Align == 64, "Swizzle Table does not align to GOB");
    constexpr SwizzleTable() {
        for (u32 y = 0; y < N; ++y) {
            for (u32 x = 0; x < M; ++x) {
                const u32 x2 = x * Align;
                values[y][x] = static_cast<u16>(((x2 % 64) / 32) * 256 + ((y % 8) / 2) * 64 +
                                                ((x2 % 32) / 16) * 32 + (y % 2) * 16 + (x2 % 16));
            }
        }
    }
    const std::array<u16, M>& operator[](std::size_t index) const {
        return values[index];
    }
    std::array<std::array<u16, M>, N> values{};
};

constexpr u32 FAST_SWIZZLE_ALIGN = 16;

constexpr auto LEGACY_SWIZZLE_TABLE = SwizzleTable<GOB_SIZE_X, GOB_SIZE_X, GOB_SIZE_Z>();
constexpr auto FAST_SWIZZLE_TABLE = SwizzleTable<GOB_SIZE_Y, 4, FAST_SWIZZLE_ALIGN>();

/**
 * This function manages ALL the GOBs(Group of Bytes) Inside a single block.
 * Instead of going gob by gob, we map the coordinates inside a block and manage from
 * those. Block_Width is assumed to be 1.
 */
void PreciseProcessBlock(u8* const swizzled_data, u8* const unswizzled_data, const bool unswizzle,
                         const u32 x_start, const u32 y_start, const u32 z_start, const u32 x_end,
                         const u32 y_end, const u32 z_end, const u32 tile_offset,
                         const u32 xy_block_size, const u32 layer_z, const u32 stride_x,
                         const u32 bytes_per_pixel, const u32 out_bytes_per_pixel) {
    std::array<u8*, 2> data_ptrs;
    u32 z_address = tile_offset;

    for (u32 z = z_start; z < z_end; z++) {
        u32 y_address = z_address;
        u32 pixel_base = layer_z * z + y_start * stride_x;
        for (u32 y = y_start; y < y_end; y++) {
            const auto& table = LEGACY_SWIZZLE_TABLE[y % GOB_SIZE_Y];
            for (u32 x = x_start; x < x_end; x++) {
                const u32 swizzle_offset{y_address + table[x * bytes_per_pixel % GOB_SIZE_X]};
                const u32 pixel_index{x * out_bytes_per_pixel + pixel_base};
                data_ptrs[unswizzle] = swizzled_data + swizzle_offset;
                data_ptrs[!unswizzle] = unswizzled_data + pixel_index;
                std::memcpy(data_ptrs[0], data_ptrs[1], bytes_per_pixel);
            }
            pixel_base += stride_x;
            if ((y + 1) % GOB_SIZE_Y == 0)
                y_address += GOB_SIZE;
        }
        z_address += xy_block_size;
    }
}

/**
 * This function manages ALL the GOBs(Group of Bytes) Inside a single block.
 * Instead of going gob by gob, we map the coordinates inside a block and manage from
 * those. Block_Width is assumed to be 1.
 */
void FastProcessBlock(u8* const swizzled_data, u8* const unswizzled_data, const bool unswizzle,
                      const u32 x_start, const u32 y_start, const u32 z_start, const u32 x_end,
                      const u32 y_end, const u32 z_end, const u32 tile_offset,
                      const u32 xy_block_size, const u32 layer_z, const u32 stride_x,
                      const u32 bytes_per_pixel, const u32 out_bytes_per_pixel) {
    std::array<u8*, 2> data_ptrs;
    u32 z_address = tile_offset;
    const u32 x_startb = x_start * bytes_per_pixel;
    const u32 x_endb = x_end * bytes_per_pixel;

    for (u32 z = z_start; z < z_end; z++) {
        u32 y_address = z_address;
        u32 pixel_base = layer_z * z + y_start * stride_x;
        for (u32 y = y_start; y < y_end; y++) {
            const auto& table = FAST_SWIZZLE_TABLE[y % GOB_SIZE_Y];
            for (u32 xb = x_startb; xb < x_endb; xb += FAST_SWIZZLE_ALIGN) {
                const u32 swizzle_offset{y_address + table[(xb / FAST_SWIZZLE_ALIGN) % 4]};
                const u32 out_x = xb * out_bytes_per_pixel / bytes_per_pixel;
                const u32 pixel_index{out_x + pixel_base};
                data_ptrs[unswizzle ? 1 : 0] = swizzled_data + swizzle_offset;
                data_ptrs[unswizzle ? 0 : 1] = unswizzled_data + pixel_index;
                std::memcpy(data_ptrs[0], data_ptrs[1], FAST_SWIZZLE_ALIGN);
            }
            pixel_base += stride_x;
            if ((y + 1) % GOB_SIZE_Y == 0)
                y_address += GOB_SIZE;
        }
        z_address += xy_block_size;
    }
}

/**
 * This function unswizzles or swizzles a texture by mapping Linear to BlockLinear Textue.
 * The body of this function takes care of splitting the swizzled texture into blocks,
 * and managing the extents of it. Once all the parameters of a single block are obtained,
 * the function calls 'ProcessBlock' to process that particular Block.
 *
 * Documentation for the memory layout and decoding can be found at:
 *  https://envytools.readthedocs.io/en/latest/hw/memory/g80-surface.html#blocklinear-surfaces
 */
template <bool fast>
void SwizzledData(u8* const swizzled_data, u8* const unswizzled_data, const bool unswizzle,
                  const u32 width, const u32 height, const u32 depth, const u32 bytes_per_pixel,
                  const u32 out_bytes_per_pixel, const u32 block_height, const u32 block_depth,
                  const u32 width_spacing) {
    auto div_ceil = [](const u32 x, const u32 y) { return ((x + y - 1) / y); };
    const u32 stride_x = width * out_bytes_per_pixel;
    const u32 layer_z = height * stride_x;
    const u32 gob_elements_x = GOB_SIZE_X / bytes_per_pixel;
    constexpr u32 gob_elements_y = GOB_SIZE_Y;
    constexpr u32 gob_elements_z = GOB_SIZE_Z;
    const u32 block_x_elements = gob_elements_x;
    const u32 block_y_elements = gob_elements_y * block_height;
    const u32 block_z_elements = gob_elements_z * block_depth;
    const u32 aligned_width = Common::AlignUp(width, gob_elements_x * width_spacing);
    const u32 blocks_on_x = div_ceil(aligned_width, block_x_elements);
    const u32 blocks_on_y = div_ceil(height, block_y_elements);
    const u32 blocks_on_z = div_ceil(depth, block_z_elements);
    const u32 xy_block_size = GOB_SIZE * block_height;
    const u32 block_size = xy_block_size * block_depth;
    u32 tile_offset = 0;
    for (u32 zb = 0; zb < blocks_on_z; zb++) {
        const u32 z_start = zb * block_z_elements;
        const u32 z_end = std::min(depth, z_start + block_z_elements);
        for (u32 yb = 0; yb < blocks_on_y; yb++) {
            const u32 y_start = yb * block_y_elements;
            const u32 y_end = std::min(height, y_start + block_y_elements);
            for (u32 xb = 0; xb < blocks_on_x; xb++) {
                const u32 x_start = xb * block_x_elements;
                const u32 x_end = std::min(width, x_start + block_x_elements);
                if constexpr (fast) {
                    FastProcessBlock(swizzled_data, unswizzled_data, unswizzle, x_start, y_start,
                                     z_start, x_end, y_end, z_end, tile_offset, xy_block_size,
                                     layer_z, stride_x, bytes_per_pixel, out_bytes_per_pixel);
                } else {
                    PreciseProcessBlock(swizzled_data, unswizzled_data, unswizzle, x_start, y_start,
                                        z_start, x_end, y_end, z_end, tile_offset, xy_block_size,
                                        layer_z, stride_x, bytes_per_pixel, out_bytes_per_pixel);
                }
                tile_offset += block_size;
            }
        }
    }
}

} // Anonymous namespace

void CopySwizzledData(u32 width, u32 height, u32 depth, u32 bytes_per_pixel,
                      u32 out_bytes_per_pixel, u8* const swizzled_data, u8* const unswizzled_data,
                      bool unswizzle, u32 block_height, u32 block_depth, u32 width_spacing) {
    const u32 block_height_size{1U << block_height};
    const u32 block_depth_size{1U << block_depth};
    if (bytes_per_pixel % 3 != 0 && (width * bytes_per_pixel) % FAST_SWIZZLE_ALIGN == 0) {
        SwizzledData<true>(swizzled_data, unswizzled_data, unswizzle, width, height, depth,
                           bytes_per_pixel, out_bytes_per_pixel, block_height_size,
                           block_depth_size, width_spacing);
    } else {
        SwizzledData<false>(swizzled_data, unswizzled_data, unswizzle, width, height, depth,
                            bytes_per_pixel, out_bytes_per_pixel, block_height_size,
                            block_depth_size, width_spacing);
    }
}

void UnswizzleTexture(u8* const unswizzled_data, u8* address, u32 tile_size_x, u32 tile_size_y,
                      u32 bytes_per_pixel, u32 width, u32 height, u32 depth, u32 block_height,
                      u32 block_depth, u32 width_spacing) {
    CopySwizzledData((width + tile_size_x - 1) / tile_size_x,
                     (height + tile_size_y - 1) / tile_size_y, depth, bytes_per_pixel,
                     bytes_per_pixel, address, unswizzled_data, true, block_height, block_depth,
                     width_spacing);
}

std::vector<u8> UnswizzleTexture(u8* address, u32 tile_size_x, u32 tile_size_y, u32 bytes_per_pixel,
                                 u32 width, u32 height, u32 depth, u32 block_height,
                                 u32 block_depth, u32 width_spacing) {
    std::vector<u8> unswizzled_data(width * height * depth * bytes_per_pixel);
    UnswizzleTexture(unswizzled_data.data(), address, tile_size_x, tile_size_y, bytes_per_pixel,
                     width, height, depth, block_height, block_depth, width_spacing);
    return unswizzled_data;
}

void SwizzleSubrect(u32 subrect_width, u32 subrect_height, u32 source_pitch, u32 swizzled_width,
                    u32 bytes_per_pixel, u8* swizzled_data, const u8* unswizzled_data,
                    u32 block_height_bit, u32 offset_x, u32 offset_y) {
    const u32 block_height = 1U << block_height_bit;
    const u32 image_width_in_gobs =
        (swizzled_width * bytes_per_pixel + (GOB_SIZE_X - 1)) / GOB_SIZE_X;
    for (u32 line = 0; line < subrect_height; ++line) {
        const u32 dst_y = line + offset_y;
        const u32 gob_address_y =
            (dst_y / (GOB_SIZE_Y * block_height)) * GOB_SIZE * block_height * image_width_in_gobs +
            ((dst_y % (GOB_SIZE_Y * block_height)) / GOB_SIZE_Y) * GOB_SIZE;
        const auto& table = LEGACY_SWIZZLE_TABLE[dst_y % GOB_SIZE_Y];
        for (u32 x = 0; x < subrect_width; ++x) {
            const u32 dst_x = x + offset_x;
            const u32 gob_address =
                gob_address_y + (dst_x * bytes_per_pixel / GOB_SIZE_X) * GOB_SIZE * block_height;
            const u32 swizzled_offset = gob_address + table[(dst_x * bytes_per_pixel) % GOB_SIZE_X];
            const u32 unswizzled_offset = line * source_pitch + x * bytes_per_pixel;

            const u8* const source_line = unswizzled_data + unswizzled_offset;
            u8* const dest_addr = swizzled_data + swizzled_offset;
            std::memcpy(dest_addr, source_line, bytes_per_pixel);
        }
    }
}

void UnswizzleSubrect(u32 subrect_width, u32 subrect_height, u32 dest_pitch, u32 swizzled_width,
                      u32 bytes_per_pixel, u8* swizzled_data, u8* unswizzled_data,
                      u32 block_height_bit, u32 offset_x, u32 offset_y) {
    const u32 block_height = 1U << block_height_bit;
    for (u32 line = 0; line < subrect_height; ++line) {
        const u32 y2 = line + offset_y;
        const u32 gob_address_y = (y2 / (GOB_SIZE_Y * block_height)) * GOB_SIZE * block_height +
                                  ((y2 % (GOB_SIZE_Y * block_height)) / GOB_SIZE_Y) * GOB_SIZE;
        const auto& table = LEGACY_SWIZZLE_TABLE[y2 % GOB_SIZE_Y];
        for (u32 x = 0; x < subrect_width; ++x) {
            const u32 x2 = (x + offset_x) * bytes_per_pixel;
            const u32 gob_address = gob_address_y + (x2 / GOB_SIZE_X) * GOB_SIZE * block_height;
            const u32 swizzled_offset = gob_address + table[x2 % GOB_SIZE_X];
            const u32 unswizzled_offset = line * dest_pitch + x * bytes_per_pixel;
            u8* dest_line = unswizzled_data + unswizzled_offset;
            u8* source_addr = swizzled_data + swizzled_offset;

            std::memcpy(dest_line, source_addr, bytes_per_pixel);
        }
    }
}

void SwizzleSliceToVoxel(u32 line_length_in, u32 line_count, u32 pitch, u32 width, u32 height,
                         u32 bytes_per_pixel, u32 block_height, u32 block_depth, u32 origin_x,
                         u32 origin_y, u8* output, const u8* input) {
    UNIMPLEMENTED_IF(origin_x > 0);
    UNIMPLEMENTED_IF(origin_y > 0);

    const u32 stride = width * bytes_per_pixel;
    const u32 gobs_in_x = (stride + GOB_SIZE_X - 1) / GOB_SIZE_X;
    const u32 block_size = gobs_in_x << (GOB_SIZE_SHIFT + block_height + block_depth);

    const u32 block_height_mask = (1U << block_height) - 1;
    const u32 x_shift = Common::CountTrailingZeroes32(GOB_SIZE << (block_height + block_depth));

    for (u32 line = 0; line < line_count; ++line) {
        const auto& table = LEGACY_SWIZZLE_TABLE[line % GOB_SIZE_Y];
        const u32 block_y = line / GOB_SIZE_Y;
        const u32 dst_offset_y =
            (block_y >> block_height) * block_size + (block_y & block_height_mask) * GOB_SIZE;
        for (u32 x = 0; x < line_length_in; ++x) {
            const u32 dst_offset =
                ((x / GOB_SIZE_X) << x_shift) + dst_offset_y + table[x % GOB_SIZE_X];
            const u32 src_offset = x * bytes_per_pixel + line * pitch;
            std::memcpy(output + dst_offset, input + src_offset, bytes_per_pixel);
        }
    }
}

void SwizzleKepler(const u32 width, const u32 height, const u32 dst_x, const u32 dst_y,
                   const u32 block_height_bit, const std::size_t copy_size, const u8* source_data,
                   u8* swizzle_data) {
    const u32 block_height = 1U << block_height_bit;
    const u32 image_width_in_gobs{(width + GOB_SIZE_X - 1) / GOB_SIZE_X};
    std::size_t count = 0;
    for (std::size_t y = dst_y; y < height && count < copy_size; ++y) {
        const std::size_t gob_address_y =
            (y / (GOB_SIZE_Y * block_height)) * GOB_SIZE * block_height * image_width_in_gobs +
            ((y % (GOB_SIZE_Y * block_height)) / GOB_SIZE_Y) * GOB_SIZE;
        const auto& table = LEGACY_SWIZZLE_TABLE[y % GOB_SIZE_Y];
        for (std::size_t x = dst_x; x < width && count < copy_size; ++x) {
            const std::size_t gob_address =
                gob_address_y + (x / GOB_SIZE_X) * GOB_SIZE * block_height;
            const std::size_t swizzled_offset = gob_address + table[x % GOB_SIZE_X];
            const u8* source_line = source_data + count;
            u8* dest_addr = swizzle_data + swizzled_offset;
            count++;

            std::memcpy(dest_addr, source_line, 1);
        }
    }
}

std::size_t CalculateSize(bool tiled, u32 bytes_per_pixel, u32 width, u32 height, u32 depth,
                          u32 block_height, u32 block_depth) {
    if (tiled) {
        const u32 aligned_width = Common::AlignBits(width * bytes_per_pixel, GOB_SIZE_X_SHIFT);
        const u32 aligned_height = Common::AlignBits(height, GOB_SIZE_Y_SHIFT + block_height);
        const u32 aligned_depth = Common::AlignBits(depth, GOB_SIZE_Z_SHIFT + block_depth);
        return aligned_width * aligned_height * aligned_depth;
    } else {
        return width * height * depth * bytes_per_pixel;
    }
}

u64 GetGOBOffset(u32 width, u32 height, u32 dst_x, u32 dst_y, u32 block_height,
                 u32 bytes_per_pixel) {
    auto div_ceil = [](const u32 x, const u32 y) { return ((x + y - 1) / y); };
    const u32 gobs_in_block = 1 << block_height;
    const u32 y_blocks = GOB_SIZE_Y << block_height;
    const u32 x_per_gob = GOB_SIZE_X / bytes_per_pixel;
    const u32 x_blocks = div_ceil(width, x_per_gob);
    const u32 block_size = GOB_SIZE * gobs_in_block;
    const u32 stride = block_size * x_blocks;
    const u32 base = (dst_y / y_blocks) * stride + (dst_x / x_per_gob) * block_size;
    const u32 relative_y = dst_y % y_blocks;
    return base + (relative_y / GOB_SIZE_Y) * GOB_SIZE;
}

} // namespace Tegra::Texture
