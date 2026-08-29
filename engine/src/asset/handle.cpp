#include "asset/handle.h"

#include <algorithm>
#include <array>
#include <functional>
#include <random>

namespace Comet {
    namespace {
        std::mt19937_64 make_generator() {
            std::random_device random_device;
            std::array<std::random_device::result_type, 8> seed_data{};
            std::ranges::generate(seed_data, std::ref(random_device));
            std::seed_seq seed(seed_data.begin(), seed_data.end());
            return std::mt19937_64(seed);
        }
    }

    AssetHandle AssetHandle::generate() {
        thread_local std::mt19937_64 generator = make_generator();

        ValueType value = 0;
        while(value == 0) {
            value = generator();
        }
        return AssetHandle(value);
    }
}
