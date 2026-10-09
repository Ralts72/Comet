#pragma once

#include "render/material/material_runtime.h"
#include "render/scene/render_submission.h"
#include "scene/material_parameters.h"

#include <functional>

namespace Comet::DrawOrder {
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
