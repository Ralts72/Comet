#pragma once

#include "common/export.h"

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    // 按时间分桶，而非跳帧采样；高帧率下也保留每个样本的权重与峰值。
    class COMET_API TimingHistory {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto BUCKET_DURATION = std::chrono::milliseconds(50);
        static constexpr size_t BUCKET_COUNT = 100;
        struct Entry {
            std::string name;
            double milliseconds = 0;
        };
        struct Statistics {
            double sum = 0;
            double maximum = 0;
            size_t count = 0;
            [[nodiscard]] double average() const {
                if(count == 0)
                    return 0;
                return sum / static_cast<double>(count);
            }
        };
        struct Detail {
            std::string name;
            Statistics timing;
        };
        struct Summary {
            Statistics total;
            std::vector<Detail> details;
            std::array<Statistics, BUCKET_COUNT> trend{};
        };

        void record(
            double total_ms, std::span<const Entry> details, Clock::time_point now = Clock::now());
        [[nodiscard]] Summary summarize(Clock::time_point now = Clock::now()) const;
        [[nodiscard]] std::optional<Clock::time_point> last_sample_time() const { return m_last; }
        void clear();

    private:
        struct Bucket {
            Clock::duration::rep tick = -1;
            Statistics total;
            std::vector<Statistics> details;
        };
        std::array<Bucket, BUCKET_COUNT> m_buckets;
        std::vector<std::string> m_names;
        std::optional<Clock::time_point> m_last;
    };
}
