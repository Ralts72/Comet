#pragma once
#include "render/scene/render_submission.h"
#include <gtest/gtest.h>
#include <span>

namespace Comet::Tests {
    inline void expect_render_items_equal(
        std::span<const ResolvedRenderItem> actual, std::span<const ResolvedRenderItem> expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for(std::size_t index = 0; index < expected.size(); ++index) {
            SCOPED_TRACE(index);
            const auto& item = actual[index];
            const auto& reference = expected[index];
            EXPECT_EQ(item.entity_id, reference.entity_id);
            EXPECT_EQ(item.transform_revision, reference.transform_revision);
            EXPECT_EQ(item.model_matrix, reference.model_matrix);
            EXPECT_EQ(item.mesh_handle, reference.mesh_handle);
            EXPECT_EQ(item.mesh_revision, reference.mesh_revision);
            EXPECT_EQ(item.mesh, reference.mesh);
            EXPECT_EQ(item.material.material_handle, reference.material.material_handle);
            EXPECT_EQ(item.material.asset_revision, reference.material.asset_revision);
            EXPECT_EQ(item.material.resource, reference.material.resource);
            EXPECT_EQ(item.material.overrides, reference.material.overrides);
        }
    }
}
