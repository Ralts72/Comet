#pragma once

namespace Comet {

    class Refcount {
    public:
        Refcount();
        virtual ~Refcount() = default;
        uint32_t getCount() const noexcept;
        void increase();
        virtual void decrease();
        bool isAlive() const noexcept;

    private:
        uint32_t m_refcount;
    };

}  