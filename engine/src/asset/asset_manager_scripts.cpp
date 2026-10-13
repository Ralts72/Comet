#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "asset/script.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace Comet {
    struct AssetManager::ScriptReloadSource {
        AssetRecord record;
        AssetRevision revision;
        std::shared_ptr<Script> previous;
        std::set<std::filesystem::path> dependencies;
    };

    namespace {
        bool shares_dependency(const std::set<std::filesystem::path>& left,
            const std::set<std::filesystem::path>& right) {
            return std::ranges::any_of(
                left, [&](const auto& path) { return right.contains(path); });
        }
    }

    Result<std::shared_ptr<Script>, Error> AssetManager::load_script(const AssetHandle handle) {
        const auto* record = m_database.find(handle);
        if(!record || record->type != AssetType::Script)
            return Result<std::shared_ptr<Script>, Error>::failure(
                {"Script asset is not indexed: " + std::to_string(handle.value())});
        if(auto script = m_registry.resolve<Script>(handle))
            return Result<std::shared_ptr<Script>, Error>::success(std::move(script));
        if(m_registry.contains(handle))
            return Result<std::shared_ptr<Script>, Error>::failure({"Runtime asset type conflict"});
        std::unordered_set<AssetHandle> processed;
        auto sources = script_reload_sources();
        if(auto published = publish_script_group(handle, processed, sources); !published)
            return Result<std::shared_ptr<Script>, Error>::failure(published.error());
        return Result<std::shared_ptr<Script>, Error>::success(m_registry.resolve<Script>(handle));
    }

    std::vector<AssetHandle> AssetManager::refresh_scripts(
        const std::span<const AssetHandle> handles) {
        AssetCompletionBudget budget{
            std::numeric_limits<std::size_t>::max(), std::chrono::nanoseconds::max()};
        return refresh_scripts(handles, budget, std::chrono::steady_clock::now());
    }

    std::vector<AssetHandle> AssetManager::refresh_scripts(
        const std::span<const AssetHandle> handles, AssetCompletionBudget& budget,
        const std::chrono::steady_clock::time_point start) {
        if(handles.empty() || budget.max_results == 0
            || budget.max_time <= std::chrono::nanoseconds::zero())
            return {};
        auto sources = script_reload_sources();
        std::unordered_set<AssetHandle> processed;
        std::vector<AssetHandle> published;
        // 输入收集与索引准备计入首组；正预算至少尝试一个不可分割的组。
        bool attempted_group = false;
        for(const auto handle : handles) {
            if(processed.contains(handle))
                continue;
            if(budget.max_results == 0
                || (attempted_group && std::chrono::steady_clock::now() - start >= budget.max_time))
                break;
            attempted_group = true;
            --budget.max_results;
            std::unordered_set<AssetHandle> group;
            if(auto result = publish_script_group(handle, group, sources); !result)
                LOG_WARN("Cannot refresh script group containing {}: {}", handle.value(),
                    result.error().message);
            else
                published.insert(published.end(), group.begin(), group.end());
            processed.insert(group.begin(), group.end());
            for(const auto member : group) {
                const auto* record = m_database.find(member);
                if(record && m_registry.resolve<Script>(member))
                    sources.insert_or_assign(member, script_reload_source(*record));
                else
                    sources.erase(member);
            }
        }
        std::ranges::sort(published);
        const auto duplicate = std::ranges::unique(published);
        published.erase(duplicate.begin(), duplicate.end());
        budget.max_time -= std::chrono::steady_clock::now() - start;
        return published;
    }

    AssetManager::ScriptReloadSource AssetManager::script_reload_source(
        const AssetRecord& record) const {
        auto previous = m_registry.resolve<Script>(record.handle);
        const auto indexed = m_database.get_import_dependencies(record.handle);
        std::set<std::filesystem::path> dependencies(indexed.begin(), indexed.end());
        if(previous)
            dependencies.insert(previous->dependencies().begin(), previous->dependencies().end());
        return {record, m_database.get_revision(record.handle), std::move(previous),
            std::move(dependencies)};
    }

    AssetManager::ScriptReloadSources AssetManager::script_reload_sources() const {
        ScriptReloadSources sources;
        for(const auto& record : m_database.get_assets()) {
            if(record.type == AssetType::Script && m_registry.resolve<Script>(record.handle))
                sources.emplace(record.handle, script_reload_source(record));
        }
        return sources;
    }

    Result<void, Error> AssetManager::publish_script_group(const AssetHandle seed,
        std::unordered_set<AssetHandle>& processed, ScriptReloadSources& sources) {
        if(!sources.contains(seed)) {
            const auto* record = m_database.find(seed);
            if(!record || record->type != AssetType::Script)
                return Result<void, Error>::failure({"Script asset is no longer indexed"});
            sources.emplace(seed, script_reload_source(*record));
        }

        std::set<AssetHandle> group{seed};
        std::set<std::filesystem::path> dependencies;
        for(;;) {
            bool expanded = true;
            while(expanded) {
                expanded = false;
                for(const auto handle : group) {
                    const auto& source = sources.at(handle);
                    dependencies.insert(source.dependencies.begin(), source.dependencies.end());
                }
                for(const auto& [handle, source] : sources) {
                    if(!group.contains(handle)
                        && shares_dependency(source.dependencies, dependencies)) {
                        group.insert(handle);
                        expanded = true;
                    }
                }
            }

            std::vector<std::filesystem::path> paths;
            for(const auto handle : group)
                paths.push_back(sources.at(handle).record.path);
            auto candidates = Script::load_group(m_database.paths().assets(), paths);
            std::map<std::filesystem::path, std::vector<std::filesystem::path>> attempted;
            if(candidates) {
                for(const auto& script : candidates.value())
                    attempted.emplace(script->source_path(), script->dependencies());
            } else {
                attempted = candidates.error().dependencies;
            }
            for(const auto& [path, inputs] : attempted)
                dependencies.insert(inputs.begin(), inputs.end());

            // 新源码可能首次引用另一组已加载模块，扩组后重读为同一份不可变输入。
            for(const auto& [handle, source] : sources) {
                if(!group.contains(handle)
                    && shares_dependency(source.dependencies, dependencies)) {
                    group.insert(handle);
                    expanded = true;
                }
            }
            if(expanded)
                continue;

            for(const auto handle : group) {
                processed.insert(handle);
                m_refresh_requests.erase(handle);
                const auto& source = sources.at(handle);
                if(!m_database.is_current(handle, source.revision)
                    || m_registry.resolve<Script>(handle) != source.previous)
                    return Result<void, Error>::failure(
                        {"Script group changed during preparation"});
            }
            for(const auto handle : group) {
                const auto& source = sources.at(handle);
                std::set<std::filesystem::path> watched;
                if(!candidates)
                    watched = source.dependencies;
                if(const auto found = attempted.find(source.record.path); found != attempted.end())
                    watched.insert(found->second.begin(), found->second.end());
                if(auto indexed =
                        update_import_dependencies(handle, {watched.begin(), watched.end()});
                    !indexed)
                    return Result<void, Error>::failure({indexed.error()});
            }

            bool current = false;
            if(candidates)
                current = candidates.value().front()->inputs_are_current();
            else
                current = candidates.error().inputs_are_current();
            if(!current) {
                // 只重试已证实过期的快照；稳定的内容错误等待下一次源文件通知。
                for(const auto handle : group)
                    m_refresh_requests[handle] = m_database.get_revision(handle);
                return Result<void, Error>::failure({"Script inputs changed during preparation"});
            }
            if(!candidates)
                return Result<void, Error>::failure({candidates.error().message});

            for(const auto handle : group) {
                const auto& source = sources.at(handle);
                if(m_registry.contains(handle) && !source.previous)
                    return Result<void, Error>::failure({"Runtime asset type conflict"});
            }
            // owner 线程无回调地连续发布；前面已经验证所有目标的身份与类型。
            size_t index = 0;
            for(const auto handle : group) {
                auto script = candidates.value()[index++];
                const auto& previous = sources.at(handle).previous;
                if(previous && previous->has_same_sources(*script))
                    continue;
                bool published = false;
                if(previous)
                    published = m_registry.replace_asset(handle, std::move(script));
                else
                    published = m_registry.register_asset(handle, std::move(script));
                if(!published)
                    return Result<void, Error>::failure({"Cannot publish validated script group"});
            }
            return Result<void, Error>::success();
        }
    }
}
