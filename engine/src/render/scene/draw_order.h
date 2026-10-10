#pragma once

#include "render/material/material_runtime.h"
#include "render/scene/render_submission.h"
#include "scene/material_parameters.h"

#include <algorithm>
#include <functional>
#include <numeric>

namespace Comet::DrawOrder {
    template<typename Range, typename Compare> void sort_if_needed(Range& items, Compare less) {
        if(!std::is_sorted(items.begin(), items.end(), less))
            std::sort(items.begin(), items.end(), less);
    }

    // 只保留槽位索引；每帧以当前输入校验，增删、重排与资源替换无需额外失效协议。
    template<typename Range, typename Compare>
    void sort_indices(std::vector<size_t>& indices, const Range& items, Compare less) {
        if(indices.size() != items.size()) {
            indices.resize(items.size());
            std::iota(indices.begin(), indices.end(), size_t{0});
        }
        sort_if_needed(
            indices, [&](const size_t a, const size_t b) { return less(items[a], items[b]); });
    }

    inline MaterialInstanceKey material_key(const MaterialBinding& material) {
        return {material.material_handle, material.overrides ? material.overrides->instance_id : 0};
    }

    inline bool same_material_input(const MaterialBinding& a, const MaterialBinding& b) {
        return a.material_handle == b.material_handle && a.resource == b.resource
               && a.overrides == b.overrides;
    }

    inline bool by_material(const ResolvedRenderItem& a, const ResolvedRenderItem& b) {
        const auto a_key = material_key(a.material);
        const auto b_key = material_key(b.material);
        if(a_key != b_key)
            return a_key < b_key;
        return std::less<const ResolvedRenderItem*>{}(&a, &b);
    }

    inline bool by_mesh(const ResolvedRenderItem& a, const ResolvedRenderItem& b) {
        if(a.mesh != b.mesh)
            return std::less<const Mesh*>{}(a.mesh.get(), b.mesh.get());
        return std::less<const ResolvedRenderItem*>{}(&a, &b);
    }
}
