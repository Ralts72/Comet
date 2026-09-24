#include "file_recheck_trigger.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <system_error>
#include <utility>

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#endif

namespace CometEditor {
#ifdef __APPLE__
    namespace {
        constexpr auto RECONNECT_INTERVAL = std::chrono::seconds(2);
        constexpr std::size_t MAX_CHANGED_PATHS = 1024;
    }
#endif

    struct FileRecheckTrigger::Backend {
        std::atomic<bool> available = false;
        std::mutex changes_mutex;
        bool pending = false;
        bool requires_full_scan = false;
        std::vector<std::filesystem::path> paths;

#ifdef __APPLE__
        FSEventStreamRef stream = nullptr;
        dispatch_queue_t queue = nullptr;
        bool started = false;

        explicit Backend(const std::filesystem::path& root) {
            std::error_code error;
            if(!std::filesystem::is_directory(root, error) || error)
                return;

            const auto path = std::filesystem::absolute(root, error).lexically_normal().string();
            if(error)
                return;
            CFStringRef root_string =
                CFStringCreateWithCString(kCFAllocatorDefault, path.c_str(), kCFStringEncodingUTF8);
            if(!root_string)
                return;
            const void* values[] = {root_string};
            CFArrayRef paths =
                CFArrayCreate(kCFAllocatorDefault, values, 1, &kCFTypeArrayCallBacks);
            CFRelease(root_string);
            if(!paths)
                return;

            FSEventStreamContext context{.version = 0, .info = this};
            stream = FSEventStreamCreate(
                kCFAllocatorDefault,
                [](ConstFSEventStreamRef, void* info, size_t count, void* event_paths,
                    const FSEventStreamEventFlags flags[], const FSEventStreamEventId[]) {
                    auto& backend = *static_cast<Backend*>(info);
                    const auto* paths = static_cast<char**>(event_paths);
                    std::lock_guard lock(backend.changes_mutex);
                    for(size_t index = 0; index < count; ++index) {
                        const auto event = flags[index];
                        if(event
                            & (kFSEventStreamEventFlagRootChanged | kFSEventStreamEventFlagMount
                                | kFSEventStreamEventFlagUnmount))
                            backend.available.store(false);
                        if(event
                            & (kFSEventStreamEventFlagMustScanSubDirs
                                | kFSEventStreamEventFlagUserDropped
                                | kFSEventStreamEventFlagKernelDropped
                                | kFSEventStreamEventFlagRootChanged | kFSEventStreamEventFlagMount
                                | kFSEventStreamEventFlagUnmount)) {
                            backend.requires_full_scan = true;
                        } else if(event & kFSEventStreamEventFlagItemIsFile) {
                            if(!paths || !paths[index]) {
                                backend.requires_full_scan = true;
                                continue;
                            }
                            if(!backend.requires_full_scan) {
                                if(backend.paths.size() == MAX_CHANGED_PATHS) {
                                    backend.paths.clear();
                                    backend.requires_full_scan = true;
                                } else {
                                    backend.paths.emplace_back(paths[index]);
                                }
                            }
                        } else if(!(event & kFSEventStreamEventFlagItemIsDir)
                                  || event
                                         & (kFSEventStreamEventFlagItemCreated
                                             | kFSEventStreamEventFlagItemRemoved
                                             | kFSEventStreamEventFlagItemRenamed)) {
                            backend.requires_full_scan = true;
                        }
                    }
                    backend.pending = backend.requires_full_scan || !backend.paths.empty();
                },
                &context, paths, kFSEventStreamEventIdSinceNow, 0.05,
                kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot
                    | kFSEventStreamCreateFlagNoDefer);
            CFRelease(paths);
            if(!stream)
                return;
            queue = dispatch_queue_create("org.comet.directory-change", DISPATCH_QUEUE_SERIAL);
            if(!queue)
                return;
            FSEventStreamSetDispatchQueue(stream, queue);
            if(FSEventStreamStart(stream)) {
                started = true;
                available.store(true);
            }
        }

        ~Backend() {
            if(stream) {
                if(started)
                    FSEventStreamStop(stream);
                FSEventStreamInvalidate(stream);
                if(queue)
                    dispatch_sync_f(queue, nullptr, [](void*) {});
                FSEventStreamRelease(stream);
            }
            if(queue)
                dispatch_release(queue);
        }
#else
        explicit Backend(const std::filesystem::path&) {}
#endif
    };

    FileRecheckTrigger::FileRecheckTrigger(
        std::filesystem::path root, const std::chrono::milliseconds fallback_interval)
        : m_root(std::move(root)), m_backend(std::make_unique<Backend>(m_root)),
          m_fallback_interval(std::max(fallback_interval, std::chrono::milliseconds::zero())) {}

    FileRecheckTrigger::~FileRecheckTrigger() = default;

    FileRecheckTrigger::Reason FileRecheckTrigger::poll(const Clock::time_point now) {
        return poll_changes(now).reason;
    }

    FileRecheckTrigger::Changes FileRecheckTrigger::poll_changes(const Clock::time_point now) {
        {
            std::lock_guard lock(m_backend->changes_mutex);
            if(m_backend->pending) {
                Changes changes{.reason = Reason::Notification,
                    .paths = std::move(m_backend->paths),
                    .requires_full_scan = m_backend->requires_full_scan};
                m_backend->pending = false;
                m_backend->requires_full_scan = false;
                return changes;
            }
        }
        if(m_backend->available.load())
            return {};
        if(now < m_next_fallback)
            return {};
        m_next_fallback = now + m_fallback_interval;
#ifdef __APPLE__
        if(!m_root.empty() && now >= m_next_reconnect) {
            m_next_reconnect = now + RECONNECT_INTERVAL;
            m_backend = std::make_unique<Backend>(m_root);
        }
#endif
        return {.reason = Reason::Fallback, .requires_full_scan = true};
    }

    bool FileRecheckTrigger::uses_native_notifications() const {
        return m_backend->available.load();
    }
}
