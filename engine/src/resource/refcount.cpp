#include "refcount.h"

namespace Comet {
    Refcount::Refcount() : m_refcount(1) {}

    uint32_t Refcount::getCount() const noexcept {
        return m_refcount;
    }

    void Refcount::increase() {
        if(m_refcount > 0) {
            ++m_refcount;
        }
    }

    void Refcount::decrease() {
        if(m_refcount > 0) {
            --m_refcount;
        }
    }

    bool Refcount::isAlive() const noexcept {
        return m_refcount > 0;
    }
}
