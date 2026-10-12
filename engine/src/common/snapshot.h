#pragma once

#include <memory>

namespace Comet {
    // 仅生产者线程调用；读者继续持有旧快照，独占时复用存储。
    template<typename T> T& writable_snapshot(std::shared_ptr<T>& snapshot) {
        if(!snapshot)
            snapshot = std::make_shared<T>();
        else if(snapshot.use_count() != 1)
            snapshot = std::make_shared<T>(*snapshot);
        return *snapshot;
    }
}
