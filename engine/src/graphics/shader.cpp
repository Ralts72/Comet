#include "shader.h"
#include "../resource/shader_res.h"

namespace Comet {

    Shader::Shader(ShaderRes* res): m_res(res) {}

    Shader::Shader(const Shader& other): m_res(other.m_res) {
        if(m_res) {
            m_res->increase();
        }
    }

    Shader::Shader(Shader&& other) noexcept: m_res(other.m_res) {
        other.m_res = nullptr;
    }

    Shader& Shader::operator=(const Shader& other) noexcept {
        if(&other != this) {
            if(m_res) {
                m_res->decrease();
            }
            m_res = other.m_res;
            if(m_res) {
                m_res->increase();
            }
        }
        return *this;
    }

    Shader& Shader::operator=(Shader&& other) noexcept {
        if(&other != this) {
            m_res = other.m_res;
            other.m_res = nullptr;
        }
        return *this;
    }

    Shader::~Shader() {
        release();
    }

    Shader::operator bool() const noexcept {
        return m_res;
    }

    void Shader::release() {
        if(m_res) {
            m_res->decrease();
            m_res = nullptr;
        }
    }
}