// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_types.h"

namespace Tegra::Texture {

enum class TextureFormat : u32 {
    R32G32B32A32 = 0x01,
    R32G32B32 = 0x02,
    R16G16B16A16 = 0x03,
    R32G32 = 0x04,
    R32_B24G8 = 0x05,
    ETC2_RGB = 0x06,
    X8B8G8R8 = 0x07,
    A8R8G8B8 = 0x08,
    A2B10G10R10 = 0x09,
    ETC2_RGB_PTA = 0x0a,
    ETC2_RGBA = 0x0b,
    R16G16 = 0x0c,
    R24G8 = 0x0d,
    R8G24 = 0x0e,
    R32 = 0x0f,
    BC6H_SFLOAT = 0x10,
    BC6H_UFLOAT = 0x11,
    A4B4G4R4 = 0x12,
    A5B5G5R1 = 0x13,
    A1B5G5R5 = 0x14,
    B5G6R5 = 0x15,
    B6G5R5 = 0x16,
    BC7 = 0x17,
    R8G8 = 0x18,
    EAC = 0x19,
    EACX2 = 0x1a,
    R16 = 0x1b,
    Y8_VIDEO = 0x1c,
    R8 = 0x1d,
    G4R4 = 0x1e,
    R1 = 0x1f,
    E5B9G9R9 = 0x20,
    B10G11R11 = 0x21,
    G8B8G8R8 = 0x22,
    B8G8R8G8 = 0x23,
    BC1_RGBA = 0x24,
    BC2 = 0x25,
    BC3 = 0x26,
    BC4 = 0x27,
    BC5 = 0x28,
    S8D24 = 0x29,
    X8Z24 = 0x2a,
    D24S8 = 0x2b,
    X4V4Z24__COV4R4V = 0x2c,
    X4V4Z24__COV8R8V = 0x2d,
    V8Z24__COV4R12V = 0x2e,
    D32 = 0x2f,
    D32S8 = 0x30,
    X8Z24_X20V4S8__COV4R4V = 0x31,
    X8Z24_X20V4S8__COV8R8V = 0x32,
    ZF32_X20V4X8__COV4R4V = 0x33,
    ZF32_X20V4X8__COV8R8V = 0x34,
    ZF32_X20V4S8__COV4R4V = 0x35,
    ZF32_X20V4S8__COV8R8V = 0x36,
    X8Z24_X16V8S8__COV4R12V = 0x37,
    ZF32_X16V8X8__COV4R12V = 0x38,
    ZF32_X16V8S8__COV4R12V = 0x39,
    D16 = 0x3a,
    V8Z24__COV8R24V = 0x3b,
    X8Z24_X16V8S8__COV8R24V = 0x3c,
    ZF32_X16V8X8__COV8R24V = 0x3d,
    ZF32_X16V8S8__COV8R24V = 0x3e,
    ASTC_2D_4X4 = 0x40,
    ASTC_2D_5X5 = 0x41,
    ASTC_2D_6X6 = 0x42,
    ASTC_2D_8X8 = 0x44,
    ASTC_2D_10X10 = 0x45,
    ASTC_2D_12X12 = 0x46,
    ASTC_2D_5X4 = 0x50,
    ASTC_2D_6X5 = 0x51,
    ASTC_2D_8X6 = 0x52,
    ASTC_2D_10X8 = 0x53,
    ASTC_2D_12X10 = 0x54,
    ASTC_2D_8X5 = 0x55,
    ASTC_2D_10X5 = 0x56,
    ASTC_2D_10X6 = 0x57,
};

enum class TextureType : u32 {
    Texture1D = 0,
    Texture2D = 1,
    Texture3D = 2,
    TextureCubemap = 3,
    Texture1DArray = 4,
    Texture2DArray = 5,
    Texture1DBuffer = 6,
    Texture2DNoMipmap = 7,
    TextureCubeArray = 8,
};

enum class TICHeaderVersion : u32 {
    OneDBuffer = 0,
    PitchColorKey = 1,
    Pitch = 2,
    BlockLinear = 3,
    BlockLinearColorKey = 4,
};

enum class ComponentType : u32 {
    SNORM = 1,
    UNORM = 2,
    SINT = 3,
    UINT = 4,
    SNORM_FORCE_FP16 = 5,
    UNORM_FORCE_FP16 = 6,
    FLOAT = 7
};

enum class SwizzleSource : u32 {
    Zero = 0,

    R = 2,
    G = 3,
    B = 4,
    A = 5,
    OneInt = 6,
    OneFloat = 7,
};

enum class MsaaMode : u32 {
    Msaa1x1 = 0,
    Msaa2x1 = 1,
    Msaa2x2 = 2,
    Msaa4x2 = 3,
    Msaa4x2_D3D = 4,
    Msaa2x1_D3D = 5,
    Msaa4x4 = 6,
    Msaa2x2_VC4 = 8,
    Msaa2x2_VC12 = 9,
    Msaa4x2_VC8 = 10,
    Msaa4x2_VC24 = 11,
};

union TextureHandle {
    TextureHandle(u32 raw) : raw{raw} {}

    u32 raw;
    BitField<0, 20, u32> tic_id;
    BitField<20, 12, u32> tsc_id;
};
static_assert(sizeof(TextureHandle) == 4, "TextureHandle has wrong size");

struct TICEntry {
    static constexpr u32 DefaultBlockHeight = 16;
    static constexpr u32 DefaultBlockDepth = 1;

    union {
        u32 raw;
        BitField<0, 7, TextureFormat> format;
        BitField<7, 3, ComponentType> r_type;
        BitField<10, 3, ComponentType> g_type;
        BitField<13, 3, ComponentType> b_type;
        BitField<16, 3, ComponentType> a_type;

        BitField<19, 3, SwizzleSource> x_source;
        BitField<22, 3, SwizzleSource> y_source;
        BitField<25, 3, SwizzleSource> z_source;
        BitField<28, 3, SwizzleSource> w_source;
    };
    u32 address_low;
    union {
        BitField<0, 16, u32> address_high;
        BitField<21, 3, TICHeaderVersion> header_version;
    };
    union {
        BitField<0, 3, u32> block_width;
        BitField<3, 3, u32> block_height;
        BitField<6, 3, u32> block_depth;

        BitField<10, 3, u32> tile_width_spacing;

        // High 16 bits of the pitch value
        BitField<0, 16, u32> pitch_high;
        BitField<26, 1, u32> use_header_opt_control;
        BitField<27, 1, u32> depth_texture;
        BitField<28, 4, u32> max_mip_level;

        BitField<0, 16, u32> buffer_high_width_minus_one;
    };
    union {
        BitField<0, 16, u32> width_minus_1;
        BitField<22, 1, u32> srgb_conversion;
        BitField<23, 4, TextureType> texture_type;
        BitField<29, 3, u32> border_size;

        BitField<0, 16, u32> buffer_low_width_minus_one;
    };
    union {
        BitField<0, 16, u32> height_minus_1;
        BitField<16, 14, u32> depth_minus_1;
    };
    union {
        BitField<6, 13, u32> mip_lod_bias;
        BitField<27, 3, u32> max_anisotropy;
    };

    union {
        BitField<0, 4, u32> res_min_mip_level;
        BitField<4, 4, u32> res_max_mip_level;
        BitField<8, 4, MsaaMode> msaa_mode;
        BitField<12, 12, u32> min_lod_clamp;
    };

    GPUVAddr Address() const {
        return static_cast<GPUVAddr>((static_cast<GPUVAddr>(address_high) << 32) | address_low);
    }

    u32 Pitch() const {
        ASSERT(header_version == TICHeaderVersion::Pitch ||
               header_version == TICHeaderVersion::PitchColorKey);
        // The pitch value is 21 bits, and is 32B aligned.
        return pitch_high << 5;
    }

    u32 Width() const {
        if (header_version != TICHeaderVersion::OneDBuffer) {
            return width_minus_1 + 1;
        }
        return ((buffer_high_width_minus_one << 16) | buffer_low_width_minus_one) + 1;
    }

    u32 Height() const {
        return height_minus_1 + 1;
    }

    u32 Depth() const {
        return depth_minus_1 + 1;
    }

    u32 BlockWidth() const {
        ASSERT(IsTiled());
        return block_width;
    }

    u32 BlockHeight() const {
        ASSERT(IsTiled());
        return block_height;
    }

    u32 BlockDepth() const {
        ASSERT(IsTiled());
        return block_depth;
    }

    bool IsTiled() const {
        return header_version == TICHeaderVersion::BlockLinear ||
               header_version == TICHeaderVersion::BlockLinearColorKey;
    }

    bool IsLineal() const {
        return header_version == TICHeaderVersion::Pitch ||
               header_version == TICHeaderVersion::PitchColorKey;
    }

    bool IsBuffer() const {
        return header_version == TICHeaderVersion::OneDBuffer;
    }

    bool IsSrgbConversionEnabled() const {
        return srgb_conversion != 0;
    }
};
static_assert(sizeof(TICEntry) == 0x20, "TICEntry has wrong size");

enum class WrapMode : u32 {
    Wrap = 0,
    Mirror = 1,
    ClampToEdge = 2,
    Border = 3,
    Clamp = 4,
    MirrorOnceClampToEdge = 5,
    MirrorOnceBorder = 6,
    MirrorOnceClampOGL = 7,
};

enum class DepthCompareFunc : u32 {
    Never = 0,
    Less = 1,
    Equal = 2,
    LessEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterEqual = 6,
    Always = 7,
};

enum class TextureFilter : u32 {
    Nearest = 1,
    Linear = 2,
};

enum class TextureMipmapFilter : u32 {
    None = 1,
    Nearest = 2,
    Linear = 3,
};

enum class Anisotropy {
    Default,
    Filter2x,
    Filter4x,
    Filter8x,
    Filter16x,
};

struct TSCEntry {
    union {
        struct {
            union {
                BitField<0, 3, WrapMode> wrap_u;
                BitField<3, 3, WrapMode> wrap_v;
                BitField<6, 3, WrapMode> wrap_p;
                BitField<9, 1, u32> depth_compare_enabled;
                BitField<10, 3, DepthCompareFunc> depth_compare_func;
                BitField<13, 1, u32> srgb_conversion;
                BitField<20, 3, u32> max_anisotropy;
            };
            union {
                BitField<0, 2, TextureFilter> mag_filter;
                BitField<4, 2, TextureFilter> min_filter;
                BitField<6, 2, TextureMipmapFilter> mipmap_filter;
                BitField<9, 1, u32> cubemap_interface_filtering;
                BitField<12, 13, u32> mip_lod_bias;
            };
            union {
                BitField<0, 12, u32> min_lod_clamp;
                BitField<12, 12, u32> max_lod_clamp;
                BitField<24, 8, u32> srgb_border_color_r;
            };
            union {
                BitField<12, 8, u32> srgb_border_color_g;
                BitField<20, 8, u32> srgb_border_color_b;
            };
            std::array<f32, 4> border_color;
        };
        std::array<u8, 0x20> raw;
    };

    std::array<float, 4> GetBorderColor() const noexcept;

    float GetMaxAnisotropy() const noexcept;

    float GetMinLod() const {
        return static_cast<float>(min_lod_clamp) / 256.0f;
    }

    float GetMaxLod() const {
        return static_cast<float>(max_lod_clamp) / 256.0f;
    }

    float GetLodBias() const {
        // Sign extend the 13-bit value.
        constexpr u32 mask = 1U << (13 - 1);
        return static_cast<float>(static_cast<s32>((mip_lod_bias ^ mask) - mask)) / 256.0f;
    }
};
static_assert(sizeof(TSCEntry) == 0x20, "TSCEntry has wrong size");

struct FullTextureInfo {
    TICEntry tic;
    TSCEntry tsc;
};

} // namespace Tegra::Texture
