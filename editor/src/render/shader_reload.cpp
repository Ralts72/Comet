#include "render/shader_reload.h"

#include "core/task_scheduler.h"
#include "graphics/pipeline/shader_interface.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr auto POLL_INTERVAL = std::chrono::milliseconds(500);
        constexpr auto DEBOUNCE = std::chrono::milliseconds(200);
    }

    ShaderReload::ShaderReload(Comet::TaskScheduler& scheduler, Requests requests)
        : m_scheduler(scheduler), m_requests(std::move(requests)) {}

    void ShaderReload::request(Clock::time_point now) {
        ++m_revision;
        m_requested = true;
        m_due = now + DEBOUNCE;
    }

    bool ShaderReload::inputs_unchanged(const Compilation& compilation) {
        return std::ranges::all_of(compilation.stages,
            [](const auto& stage) { return Comet::ShaderCompiler::inputs_unchanged(stage); });
    }

    std::shared_ptr<const ShaderReload::Compilation> ShaderReload::update(Clock::time_point now) {
        if(m_pending
            && m_pending->completion.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready) {
            auto output = std::move(m_pending->output);
            try {
                m_pending->completion.get();
            } catch(const std::exception& error) {
                output->succeeded = false;
                output->diagnostics = error.what();
            }
            m_pending.reset();
            if(output->revision == m_revision) {
                if(!inputs_unchanged(*output)) {
                    request(now);
                } else {
                    m_observed = output;
                    m_next_poll = now + POLL_INTERVAL;
                    return output;
                }
            }
        }
        if(m_pending)
            return {};
        if(!m_requested && now >= m_next_poll) {
            m_next_poll = now + POLL_INTERVAL;
            if(m_observed && !inputs_unchanged(*m_observed))
                request(now);
        }
        if(!m_requested || now < m_due)
            return {};

        auto output = std::make_shared<Compilation>();
        output->revision = m_revision;
        auto completion = m_scheduler.try_submit([output, requests = m_requests] {
            bool success = true;
            for(size_t index = 0; index < requests.size(); ++index) {
                auto& stage = output->stages[index];
                stage = Comet::ShaderCompiler::compile(requests[index]);
                success &= stage.succeeded();
                if(stage.succeeded()) {
                    const auto reflected =
                        Comet::ShaderInterface::reflect(stage.words, requests[index].entry_point);
                    if(!reflected) {
                        success = false;
                        stage.diagnostics += reflected.error();
                    }
                }
                if(!stage.diagnostics.empty())
                    output->diagnostics +=
                        requests[index].source.string() + ":\n" + stage.diagnostics + '\n';
            }
            output->succeeded = success;
        });
        if(completion) {
            m_pending.emplace(Pending{std::move(output), std::move(*completion)});
            m_requested = false;
        } else {
            m_due = now + DEBOUNCE;
        }
        return {};
    }
}
