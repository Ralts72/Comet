#include "shader_reload.h"

#include "diagnostics/logger.h"

#include <algorithm>
#include <stdexcept>

namespace CometEditor {
    ShaderReload::ShaderReload(Comet::TaskScheduler& scheduler, Requests requests)
        : ShaderReload(scheduler, std::move(requests), Options{}) {}

    ShaderReload::ShaderReload(
        Comet::TaskScheduler& scheduler, Requests requests, Options options)
        : m_scheduler(scheduler), m_requests(std::move(requests)), m_options(options) {
        if(m_requests.empty() || m_requests.size() > 16
            || options.poll_interval.count() < 0 || options.debounce.count() < 0)
            throw std::invalid_argument(
                "Shader reload requires 1..16 sources and nonnegative intervals");
        for(const auto& [name, request] : m_requests) {
            if(name.empty() || request.source.empty())
                throw std::invalid_argument(
                    "Shader reload requires names and source paths");
            m_watched.emplace(request.source, read_stamp(request.source));
        }
    }

    ShaderReload::Stamp ShaderReload::read_stamp(const std::filesystem::path& path) {
        Stamp result;
        std::error_code error;
        result.modified = std::filesystem::last_write_time(path, error);
        if(!error)
            result.size = std::filesystem::file_size(path, error);
        result.error = error.value();
        return result;
    }

    void ShaderReload::request_update(const Clock::time_point now) {
        ++m_statistics.revision;
        m_pending = true;
        m_debounce_until = now + m_options.debounce;
    }

    void ShaderReload::watch_inputs(const Job& job) {
        std::map<std::filesystem::path, Stamp> next;
        const auto watch = [&](const std::filesystem::path& path) {
            const auto old = m_watched.find(path);
            if(old != m_watched.end())
                next.emplace(path, old->second);
            else
                next.emplace(path, read_stamp(path));
        };
        for(const auto& [name, request] : m_requests)
            watch(request.source);
        for(const auto& input : job.inputs) {
            for(const auto& dependency : input.dependencies) {
                watch(dependency.path);
                watch(dependency.resolved_path);
            }
        }
        m_watched.swap(next);
    }

    std::optional<Comet::ShaderManager::Bytecodes> ShaderReload::update(
        const Clock::time_point now) {
        if(now >= m_next_poll) {
            m_next_poll = now + m_options.poll_interval;
            bool changed = false;
            for(auto& [path, stamp] : m_watched) {
                const auto current = read_stamp(path);
                changed |= current != stamp;
                stamp = current;
            }
            if(changed)
                request_update(now);
        }
        if(m_in_flight
            && m_in_flight->completion.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready) {
            const auto job = m_in_flight->job;
            try {
                m_in_flight->completion.get();
            } catch(const std::exception& error) {
                job->error = error.what();
            }
            m_in_flight.reset();
            if(job->revision != m_statistics.revision) {
                ++m_statistics.discarded;
            } else {
                watch_inputs(*job);
                const bool unchanged =
                    std::ranges::all_of(job->inputs, [](const auto& input) {
                        return Comet::ShaderCompiler::inputs_unchanged(input);
                    });
                if(!unchanged) {
                    ++m_statistics.discarded;
                    request_update(now);
                } else if(!job->error.empty()) {
                    ++m_statistics.failed;
                    LOG_ERROR("Shader reload kept previous GPU version: {}", job->error);
                } else {
                    return std::move(job->bytecodes);
                }
            }
        }
        if(m_pending && !m_in_flight && now >= m_debounce_until) {
            auto job = std::make_shared<Job>();
            job->revision = m_statistics.revision;
            auto completion = m_scheduler.try_submit([job, requests = m_requests] {
                for(const auto& [name, request] : requests) {
                    auto compiled = Comet::ShaderCompiler::compile(request);
                    if(!compiled.succeeded()) {
                        job->error += name + ": " + compiled.diagnostics + "\n";
                    } else {
                        try {
                            const Comet::ShaderInterface interface(
                                compiled.words, request.entry_point);
                            job->bytecodes.emplace(name,
                                Comet::ShaderManager::Bytecode{
                                    std::move(compiled.words), request.entry_point});
                        } catch(const std::exception& error) {
                            job->error += name + ": " + error.what() + "\n";
                        }
                    }
                    job->inputs.push_back(std::move(compiled));
                }
                if(!job->error.empty())
                    job->bytecodes.clear();
            });
            if(completion) {
                m_in_flight.emplace(InFlight{std::move(job), std::move(*completion)});
                m_pending = false;
                ++m_statistics.submitted;
            } else {
                ++m_statistics.backpressure;
            }
        }
        return std::nullopt;
    }
}
