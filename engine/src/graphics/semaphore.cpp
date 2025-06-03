#include "semaphore.h"
#include "../resource/semaphore_res.h"

namespace Comet {
    Semaphore::Semaphore(SemaphoreRes* res): m_res(res) {}


    Semaphore::Semaphore(const Semaphore & other): m_res(other.m_res) {
        if(m_res) {
            m_res->increase();
        }
    }

    Semaphore::Semaphore(Semaphore&& other) noexcept: m_res(other.m_res) {
        other.m_res = nullptr;
    }

    Semaphore& Semaphore::operator=(const Semaphore& other) noexcept {
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

    Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
        if(&other != this) {
            m_res = other.m_res;
            other.m_res = nullptr;
        }
        return *this;
    }

    Semaphore::~Semaphore() {
        release();
    }

    Semaphore::operator bool() const noexcept {
        return m_res;
    }

    void Semaphore::release() {
        if(m_res) {
            m_res->decrease();
            m_res = nullptr;
        }
    }
}