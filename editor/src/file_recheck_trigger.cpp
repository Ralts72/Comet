#include "file_recheck_trigger.h"

#include <algorithm>
#include <atomic>
#include <system_error>

#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#endif

namespace CometEditor {
    struct FileRecheckTrigger::Backend {
        std::atomic<bool> pending = false;
        std::atomic<bool> available = false;

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
                [](ConstFSEventStreamRef, void* info, size_t count, void*,
                    const FSEventStreamEventFlags flags[], const FSEventStreamEventId[]) {
                    auto& backend = *static_cast<Backend*>(info);
                    for(size_t index = 0; index < count; ++index) {
                        if(flags[index]
                            & (kFSEventStreamEventFlagRootChanged | kFSEventStreamEventFlagMount
                                | kFSEventStreamEventFlagUnmount))
                            backend.available.store(false);
                    }
                    backend.pending.store(true);
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
        : m_backend(std::make_unique<Backend>(root)),
          m_fallback_interval(std::max(fallback_interval, std::chrono::milliseconds::zero())) {}

    FileRecheckTrigger::~FileRecheckTrigger() = default;

    FileRecheckTrigger::Reason FileRecheckTrigger::poll(const Clock::time_point now) {
        if(m_backend->pending.exchange(false))
            return Reason::Notification;
        if(m_backend->available.load())
            return Reason::None;
        if(now < m_next_fallback)
            return Reason::None;
        m_next_fallback = now + m_fallback_interval;
        return Reason::Fallback;
    }

    bool FileRecheckTrigger::uses_native_notifications() const {
        return m_backend->available.load();
    }
}
