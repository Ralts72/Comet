#pragma once

namespace Comet {
    class SemaphoreRes;

    class Semaphore{
    public:
        Semaphore() = default;
        explicit Semaphore(SemaphoreRes*);

        Semaphore(const Semaphore &);
        Semaphore(Semaphore&&) noexcept;
        Semaphore& operator=(const Semaphore&) noexcept;
        Semaphore& operator=(Semaphore&&) noexcept;
        ~Semaphore();

        operator bool() const noexcept;
        void release();

        const SemaphoreRes& getRes() const noexcept { return *m_res; }
        SemaphoreRes& getRes() noexcept { return *m_res; }
    private:
        SemaphoreRes *m_res{};
    };
}