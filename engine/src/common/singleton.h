#pragma once
#include <cassert>
#include <memory>

namespace Comet {
    template<typename T, bool ExplicitInit>
    class Singleton;

    template<typename T>
    class Singleton<T, false> {
    public:
        static T& getInstance() {
            static T instance;
            return instance;
        }
    };

    template<typename T>
    class Singleton<T, true> {
    public:
        static T& getInstance() {
            assert(m_instance != nullptr);
            return *m_instance;
        }

        template<typename... Args>
        static void init(Args&&... args) {
            m_instance = std::make_unique<T>(std::forward<Args>(args)...);
        }

        static void shutdown() {
            m_instance.reset();
            m_instance = nullptr;
        }

    private:
        inline static std::unique_ptr<T> m_instance;
    };
}
