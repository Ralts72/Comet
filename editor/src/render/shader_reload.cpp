#include "render/shader_reload.h"

#include "core/task_scheduler.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    ShaderReload::ShaderReload(Comet::TaskScheduler& scheduler, Requests requests,
        std::filesystem::path watch_root, const std::chrono::milliseconds quiet_period)
        : m_scheduler(scheduler), m_requests(std::move(requests)), m_changes(std::move(watch_root)),
          m_quiet_period(quiet_period) {}

    void ShaderReload::request(Clock::time_point now) {
        ++m_revision;
        m_requested = true;
        m_due = now + m_quiet_period;
        m_delivery_retry.reset();
    }

    bool ShaderReload::retry_delivery(uint64_t revision, Clock::time_point now) {
        if(m_requested || m_pending || !m_observed || !m_observed->succeeded
            || revision != m_revision || m_observed->revision != revision)
            return false;
        return m_delivery_retry.schedule(now);
    }

    bool ShaderReload::inputs_unchanged(const Compilation& compilation) {
        return std::ranges::all_of(compilation.stages, [](const auto& stage) {
            return Comet::ShaderCompiler::inputs_unchanged(stage.second);
        });
    }

    std::shared_ptr<const ShaderReload::Compilation> ShaderReload::update(Clock::time_point now) {
        const auto recheck = m_changes.poll(now);
        if(m_pending
            && m_pending->completion.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready) {
            auto pending = std::move(*m_pending);
            m_pending.reset();
            auto output = std::move(pending.output);
            pending.completion.get();
            if(output->revision == m_revision) {
                if(!inputs_unchanged(*output)) {
                    request(now);
                } else {
                    m_observed = output;
                    return output;
                }
            }
        }
        if(m_pending)
            return {};
        if(recheck != FileRecheckTrigger::Reason::None && m_observed
            && !inputs_unchanged(*m_observed)) {
            if(!m_requested)
                request(now);
            else if(recheck == FileRecheckTrigger::Reason::Notification)
                m_due = now + m_quiet_period;
        }
        if(m_delivery_retry.consume(now)) {
            if(m_observed && m_observed->revision == m_revision && inputs_unchanged(*m_observed))
                return m_observed;
            request(now);
        }
        if(!m_requested || now < m_due)
            return {};

        auto output = std::make_shared<Compilation>();
        output->revision = m_revision;
        auto completion = m_scheduler.try_submit([output, requests = m_requests] {
            if(requests.empty() || requests.size() > 16
                || std::ranges::any_of(requests, [](const auto& entry) {
                       return entry.first.empty() || entry.second.source.empty();
                   })) {
                output->diagnostics = "Shader compilation requires 1..16 named source requests";
                return;
            }
            bool success = true;
            for(const auto& [name, request] : requests) {
                auto& stage = output->stages[name];
                stage = Comet::ShaderCompiler::compile(request);
                success &= stage.succeeded();
                if(!stage.diagnostics.empty())
                    output->diagnostics +=
                        name + " (" + request.source.string() + "):\n" + stage.diagnostics + '\n';
            }
            output->succeeded = success;
        });
        if(completion) {
            m_pending.emplace(Pending{std::move(output), std::move(*completion)});
            m_requested = false;
        } else {
            m_due = now + m_quiet_period;
        }
        return {};
    }
}
