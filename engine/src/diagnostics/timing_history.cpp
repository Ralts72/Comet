#include "diagnostics/timing_history.h"

#include <algorithm>

namespace Comet {
    namespace {
        void merge(TimingHistory::Statistics& target, const TimingHistory::Statistics& value) {
            target.sum += value.sum;
            target.maximum = std::max(target.maximum, value.maximum);
            target.count += value.count;
        }
    }

    void TimingHistory::clear() {
        m_buckets = {};
        m_names.clear();
        m_last.reset();
    }

    void TimingHistory::record(
        double total_ms, std::span<const Entry> details, Clock::time_point now) {
        if(m_last && now < *m_last)
            return;
        if(m_names.size() != details.size()
            || !std::equal(m_names.begin(), m_names.end(), details.begin(),
                [](const auto& name, const auto& entry) { return name == entry.name; })) {
            clear();
            for(const auto& entry : details)
                m_names.push_back(entry.name);
        }
        const auto tick = now.time_since_epoch() / BUCKET_DURATION;
        auto& bucket = m_buckets[static_cast<size_t>(tick) % BUCKET_COUNT];
        if(bucket.tick != tick) {
            bucket = {};
            bucket.tick = tick;
            bucket.details.resize(details.size());
        }
        merge(bucket.total, {total_ms, total_ms, 1});
        for(size_t index = 0; index < details.size(); ++index)
            merge(bucket.details[index],
                {details[index].milliseconds, details[index].milliseconds, 1});
        m_last = now;
    }

    TimingHistory::Summary TimingHistory::summarize(Clock::time_point now) const {
        Summary result;
        for(const auto& name : m_names)
            result.details.push_back({name, {}});
        const auto current = now.time_since_epoch() / BUCKET_DURATION;
        constexpr auto summary_buckets = std::chrono::seconds(1) / BUCKET_DURATION;
        for(size_t index = 0; index < BUCKET_COUNT; ++index) {
            const auto tick = current - static_cast<Clock::duration::rep>(BUCKET_COUNT - 1 - index);
            if(tick < 0)
                continue;
            const auto& bucket = m_buckets[static_cast<size_t>(tick) % BUCKET_COUNT];
            if(bucket.tick != tick)
                continue;
            result.trend[index] = bucket.total;
            if(current - tick >= summary_buckets)
                continue;
            merge(result.total, bucket.total);
            for(size_t detail = 0; detail < bucket.details.size(); ++detail)
                merge(result.details[detail].timing, bucket.details[detail]);
        }
        return result;
    }
}
