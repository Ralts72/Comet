#pragma once

namespace Comet {
    class ShaderRes;

    class Shader {
    public:
        Shader() = default;

        explicit Shader(ShaderRes*);

        Shader(const Shader& other);

        Shader(Shader&& other) noexcept;

        Shader& operator=(const Shader& other) noexcept;

        Shader& operator=(Shader&& other) noexcept;

        ~Shader();

        operator bool() const noexcept;

        void release();

        const ShaderRes& getRes() const noexcept { return *m_res; }
        ShaderRes& getRes() noexcept { return *m_res; }

    private:
        ShaderRes* m_res{};
    };
}
