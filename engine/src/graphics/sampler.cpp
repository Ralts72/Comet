#include "sampler.h"
#include "../resource/sampler_res.h"

namespace Comet {
    Sampler::Sampler(SamplerRes* res): m_res(res) {}

    Sampler::Sampler(const Sampler& other): m_res(other.m_res) {
        if(m_res) {
            m_res->increase();
        }
    }

    Sampler::Sampler(Sampler&& other) noexcept: m_res(other.m_res) {
        other.m_res = nullptr;
    }

    Sampler& Sampler::operator=(const Sampler& other) noexcept {
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

    Sampler& Sampler::operator=(Sampler&& other) noexcept {
        if(&other != this) {
            m_res = other.m_res;
            other.m_res = nullptr;
        }
        return *this;
    }

    Sampler::~Sampler() {
        release();
    }

    Sampler::operator bool() const noexcept {
        return m_res;
    }

    void Sampler::release() {
        if(m_res) {
            m_res->decrease();
            m_res = nullptr;
        }
    }
}


