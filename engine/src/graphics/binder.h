#pragma once
#include "../pch.h"

namespace Comet {
    class BindGroupRes;
    class BindLayoutRes;

    class BindGroup {
    public:
        //     struct BufferBinding final {
        //         enum class Type {
        //             Storage,
        //             Uniform,
        //             DynamicStorage,
        //             DynamicUniform,
        //         };
        //
        //         Type type = Type::Uniform;
        //         Buffer buffer;
        //         std::optional<uint32_t> offset;
        //         std::optional<uint32_t> size;
        //     };
        //
        //     struct SamplerBinding final {
        //         Sampler sampler;
        //
        //         SamplerBinding() = default;
        //     };
        //
        //     struct CombinedSamplerBinding final {
        //         ImageView view;
        //         Sampler sampler;
        //     };
        //
        //     struct ImageBinding final {
        //         enum class Type {
        //             Image,
        //             StorageImage,
        //         };
        //
        //         Type type = Type::Image;
        //         ImageView view;
        //     };
        //
        //     struct BindingPoint {
        //         std::variant<SamplerBinding, BufferBinding, CombinedSamplerBinding,
        //             ImageBinding>
        //         entry;
        //     };
        //
        //     struct Entry final {
        //         BindingPoint binding;
        //         Flags<ShaderStage> shader_stage;
        //         uint32_t arraySize = 1;
        //     };
        //
        //     struct Descriptor final {
        //         std::map<uint32_t, Entry> entries;
        //     };
        //
        //     BindGroup() = default;
        //
        //     explicit BindGroup(BindGroupImpl*);
        //
        //     BindGroup(BindGroup&&) noexcept;
        //
        //     BindGroup& operator=(BindGroup&&) noexcept;
        //
        //     ~BindGroup();
        //
        //     const BindGroupImpl& Impl() const noexcept;
        //
        //     BindGroupImpl& Impl() noexcept;
        //
        //     operator bool() const noexcept;
        //
        //     const Descriptor& GetDescriptor() const;
        //
        // private:
        //     BindGroupImpl* m_impl{};
    };

    class BindLayout {};
}
