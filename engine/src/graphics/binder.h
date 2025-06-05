#pragma once
#include "../pch.h"
#include "buffer.h"
#include "sampler.h"
#include "image.h"

namespace Comet {
    class BindGroupRes;
    class BindLayoutRes;

    class BindGroup {
    public:
            struct BufferBinding final {
                enum class Type {
                    Storage,
                    Uniform,
                    DynamicStorage,
                    DynamicUniform,
                };
        
                Type type = Type::Uniform;
                Buffer buffer;
                std::optional<uint32_t> offset;
                std::optional<uint32_t> size;
            };
        
            struct SamplerBinding final {
                Sampler sampler;
        
                SamplerBinding() = default;
            };
        
            struct CombinedSamplerBinding final {
                ImageView view;
                Sampler sampler;
            };
        
            struct ImageBinding final {
                enum class Type {
                    Image,
                    StorageImage,
                };
        
                Type type = Type::Image;
                ImageView view;
            };
        
            struct BindingPoint {
                std::variant<SamplerBinding, BufferBinding, CombinedSamplerBinding,
                    ImageBinding>
                entry;
            };
        
            struct Entry final {
                BindingPoint binding;
                Flags<ShaderStage> shaderStage;
                uint32_t arraySize = 1;
            };
        
            struct Descriptor final {
                std::map<uint32_t, Entry> entries;
            };
        
            BindGroup() = default;
        
            explicit BindGroup(BindGroupRes*);
        
            BindGroup(BindGroup&&) noexcept;
        
            BindGroup& operator=(BindGroup&&) noexcept;
        
            ~BindGroup();
        
            const BindGroupRes& getRes() const noexcept { return *m_res; }
        
            BindGroupRes& getRes() noexcept {return *m_res;}

            operator bool() const noexcept { return m_res; }

            const Descriptor& getDescriptor() const;
        
        private:
            BindGroupRes* m_res{};
    };

    class BindLayout {};
}
