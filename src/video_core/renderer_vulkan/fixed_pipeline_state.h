// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <type_traits>

#include "common/bit_field.h"
#include "common/common_types.h"

#include "video_core/engines/maxwell_3d.h"
#include "video_core/surface.h"

namespace Vulkan {

using Maxwell = Tegra::Engines::Maxwell3D::Regs;

struct FixedPipelineState {
    static u32 PackComparisonOp(Maxwell::ComparisonOp op) noexcept;
    static Maxwell::ComparisonOp UnpackComparisonOp(u32 packed) noexcept;

    static u32 PackStencilOp(Maxwell::StencilOp op) noexcept;
    static Maxwell::StencilOp UnpackStencilOp(u32 packed) noexcept;

    static u32 PackCullFace(Maxwell::CullFace cull) noexcept;
    static Maxwell::CullFace UnpackCullFace(u32 packed) noexcept;

    static u32 PackFrontFace(Maxwell::FrontFace face) noexcept;
    static Maxwell::FrontFace UnpackFrontFace(u32 packed) noexcept;

    static u32 PackPolygonMode(Maxwell::PolygonMode mode) noexcept;
    static Maxwell::PolygonMode UnpackPolygonMode(u32 packed) noexcept;

    static u32 PackLogicOp(Maxwell::LogicOperation op) noexcept;
    static Maxwell::LogicOperation UnpackLogicOp(u32 packed) noexcept;

    static u32 PackBlendEquation(Maxwell::Blend::Equation equation) noexcept;
    static Maxwell::Blend::Equation UnpackBlendEquation(u32 packed) noexcept;

    static u32 PackBlendFactor(Maxwell::Blend::Factor factor) noexcept;
    static Maxwell::Blend::Factor UnpackBlendFactor(u32 packed) noexcept;

    struct BlendingAttachment {
        union {
            u32 raw;
            BitField<0, 1, u32> mask_r;
            BitField<1, 1, u32> mask_g;
            BitField<2, 1, u32> mask_b;
            BitField<3, 1, u32> mask_a;
            BitField<4, 3, u32> equation_rgb;
            BitField<7, 3, u32> equation_a;
            BitField<10, 5, u32> factor_source_rgb;
            BitField<15, 5, u32> factor_dest_rgb;
            BitField<20, 5, u32> factor_source_a;
            BitField<25, 5, u32> factor_dest_a;
            BitField<30, 1, u32> enable;
        };

        void Fill(const Maxwell& regs, std::size_t index);

        constexpr std::array<bool, 4> Mask() const noexcept {
            return {mask_r != 0, mask_g != 0, mask_b != 0, mask_a != 0};
        }

        Maxwell::Blend::Equation EquationRGB() const noexcept {
            return UnpackBlendEquation(equation_rgb.Value());
        }

        Maxwell::Blend::Equation EquationAlpha() const noexcept {
            return UnpackBlendEquation(equation_a.Value());
        }

        Maxwell::Blend::Factor SourceRGBFactor() const noexcept {
            return UnpackBlendFactor(factor_source_rgb.Value());
        }

        Maxwell::Blend::Factor DestRGBFactor() const noexcept {
            return UnpackBlendFactor(factor_dest_rgb.Value());
        }

        Maxwell::Blend::Factor SourceAlphaFactor() const noexcept {
            return UnpackBlendFactor(factor_source_a.Value());
        }

        Maxwell::Blend::Factor DestAlphaFactor() const noexcept {
            return UnpackBlendFactor(factor_dest_a.Value());
        }
    };

    union VertexAttribute {
        u32 raw;
        BitField<0, 1, u32> enabled;
        BitField<1, 5, u32> buffer;
        BitField<6, 14, u32> offset;
        BitField<20, 3, u32> type;
        BitField<23, 6, u32> size;

        constexpr Maxwell::VertexAttribute::Type Type() const noexcept {
            return static_cast<Maxwell::VertexAttribute::Type>(type.Value());
        }

        constexpr Maxwell::VertexAttribute::Size Size() const noexcept {
            return static_cast<Maxwell::VertexAttribute::Size>(size.Value());
        }
    };

    template <std::size_t Position>
    union StencilFace {
        BitField<Position + 0, 3, u32> action_stencil_fail;
        BitField<Position + 3, 3, u32> action_depth_fail;
        BitField<Position + 6, 3, u32> action_depth_pass;
        BitField<Position + 9, 3, u32> test_func;

        Maxwell::StencilOp ActionStencilFail() const noexcept {
            return UnpackStencilOp(action_stencil_fail);
        }

        Maxwell::StencilOp ActionDepthFail() const noexcept {
            return UnpackStencilOp(action_depth_fail);
        }

        Maxwell::StencilOp ActionDepthPass() const noexcept {
            return UnpackStencilOp(action_depth_pass);
        }

        Maxwell::ComparisonOp TestFunc() const noexcept {
            return UnpackComparisonOp(test_func);
        }
    };

    union VertexBinding {
        u16 raw;
        BitField<0, 12, u16> stride;
        BitField<12, 1, u16> enabled;
    };

    struct DynamicState {
        union {
            u32 raw1;
            StencilFace<0> front;
            StencilFace<12> back;
            BitField<24, 1, u32> stencil_enable;
            BitField<25, 1, u32> depth_write_enable;
            BitField<26, 1, u32> depth_bounds_enable;
            BitField<27, 1, u32> depth_test_enable;
            BitField<28, 1, u32> front_face;
            BitField<29, 3, u32> depth_test_func;
        };
        union {
            u32 raw2;
            BitField<0, 4, u32> topology;
            BitField<4, 2, u32> cull_face;
            BitField<6, 1, u32> cull_enable;
        };
        std::array<VertexBinding, Maxwell::NumVertexArrays> vertex_bindings;

        void Fill(const Maxwell& regs);

        Maxwell::ComparisonOp DepthTestFunc() const noexcept {
            return UnpackComparisonOp(depth_test_func);
        }

        Maxwell::CullFace CullFace() const noexcept {
            return UnpackCullFace(cull_face.Value());
        }

        Maxwell::FrontFace FrontFace() const noexcept {
            return UnpackFrontFace(front_face.Value());
        }

        constexpr Maxwell::PrimitiveTopology Topology() const noexcept {
            return static_cast<Maxwell::PrimitiveTopology>(topology.Value());
        }
    };

    union {
        u32 raw;
        BitField<0, 1, u32> no_extended_dynamic_state;
        BitField<2, 1, u32> primitive_restart_enable;
        BitField<3, 1, u32> depth_bias_enable;
        BitField<4, 1, u32> depth_clamp_disabled;
        BitField<5, 1, u32> ndc_minus_one_to_one;
        BitField<6, 2, u32> polygon_mode;
        BitField<8, 5, u32> patch_control_points_minus_one;
        BitField<13, 2, u32> tessellation_primitive;
        BitField<15, 2, u32> tessellation_spacing;
        BitField<17, 1, u32> tessellation_clockwise;
        BitField<18, 1, u32> logic_op_enable;
        BitField<19, 4, u32> logic_op;
        BitField<23, 1, u32> rasterize_enable;
    };
    u32 point_size;
    std::array<u32, Maxwell::NumVertexArrays> binding_divisors;
    std::array<VertexAttribute, Maxwell::NumVertexAttributes> attributes;
    std::array<BlendingAttachment, Maxwell::NumRenderTargets> attachments;
    std::array<u16, Maxwell::NumViewports> viewport_swizzles;
    DynamicState dynamic_state;

    void Fill(const Maxwell& regs, bool has_extended_dynamic_state);

    std::size_t Hash() const noexcept;

    bool operator==(const FixedPipelineState& rhs) const noexcept;

    bool operator!=(const FixedPipelineState& rhs) const noexcept {
        return !operator==(rhs);
    }

    std::size_t Size() const noexcept {
        const std::size_t total_size = sizeof *this;
        return total_size - (no_extended_dynamic_state != 0 ? 0 : sizeof(DynamicState));
    }
};
static_assert(std::has_unique_object_representations_v<FixedPipelineState>);
static_assert(std::is_trivially_copyable_v<FixedPipelineState>);
static_assert(std::is_trivially_constructible_v<FixedPipelineState>);

} // namespace Vulkan

namespace std {

template <>
struct hash<Vulkan::FixedPipelineState> {
    std::size_t operator()(const Vulkan::FixedPipelineState& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std
