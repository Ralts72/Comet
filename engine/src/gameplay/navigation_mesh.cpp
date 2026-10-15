#include "gameplay/navigation_mesh.h"
#include "scene/components.h"
#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourAlloc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace Comet::Gameplay {
    struct NavigationMesh::Impl {
        std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{nullptr, dtFreeNavMesh};
        std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
            nullptr, dtFreeNavMeshQuery};
        dtQueryFilter filter;
        Math::Vec3 extents{0.5f, 1, 0.5f};
        bool nearest(Math::Vec3 point, dtPolyRef& ref, Math::Vec3& projected) const {
            return dtStatusSucceed(
                       query->findNearestPoly(&point.x, &extents.x, &filter, &ref, &projected.x))
                   && ref;
        }
    };
    NavigationMesh::NavigationMesh() = default;
    NavigationMesh::~NavigationMesh() = default;
    void NavigationMesh::clear() {
        m_impl.reset();
    }
    bool NavigationMesh::ready() const {
        return bool(m_impl);
    }

    Result<void> NavigationMesh::build(std::span<const Math::Vec3> triangles, Math::Vec3 center,
        const NavigationRegionComponent& s) {
        clear();
        const auto fail = [](const char* message) { return Result<void>::failure(message); };
        if(!Math::is_finite(center) || !Math::is_finite(s.half_extents)
            || !glm::all(glm::greaterThan(s.half_extents, Math::Vec3(0)))
            || !std::isfinite(s.cell_size) || s.cell_size < 0.05f || s.cell_size > 2
            || !std::isfinite(s.agent_radius) || s.agent_radius <= 0 || s.agent_radius > 10
            || !std::isfinite(s.agent_height) || s.agent_height < s.agent_radius * 2
            || s.agent_height > 20 || !std::isfinite(s.max_climb) || s.max_climb < 0
            || s.max_climb > s.agent_height || !std::isfinite(s.max_slope) || s.max_slope < 0
            || s.max_slope >= 90)
            return fail("Invalid navigation region dimensions or agent settings");
        if(triangles.empty() || triangles.size() % 3 || triangles.size() > 3000000)
            return fail("Navigation requires between one and one million triangles");
        rcConfig cfg{};
        cfg.cs = s.cell_size;
        cfg.ch = std::min(0.1f, cfg.cs * 0.5f);
        const auto minimum = center - s.half_extents;
        const auto maximum = center + s.half_extents;
        if(!Math::is_finite(minimum) || !Math::is_finite(maximum)
            || 2.0 * s.half_extents.x / cfg.cs > 4096 || 2.0 * s.half_extents.z / cfg.cs > 4096
            || 2.0 * s.half_extents.y / cfg.ch > 65000)
            return fail("Navigation region exceeds voxel limits");
        std::copy_n(&minimum.x, 3, cfg.bmin);
        std::copy_n(&maximum.x, 3, cfg.bmax);
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);
        if(cfg.width < 3 || cfg.height < 3 || int64_t(cfg.width) * cfg.height > 2000000)
            return fail("Navigation region exceeds two million ground cells");
        cfg.walkableSlopeAngle = s.max_slope;
        cfg.walkableHeight = int(std::ceil(s.agent_height / cfg.ch));
        cfg.walkableClimb = int(std::floor(s.max_climb / cfg.ch));
        cfg.walkableRadius = int(std::ceil(s.agent_radius / cfg.cs));
        cfg.maxEdgeLen = int(12 / cfg.cs);
        cfg.maxSimplificationError = 1.3f;
        cfg.minRegionArea = 0;
        cfg.mergeRegionArea = 20 * 20;
        cfg.maxVertsPerPoly = 6;
        cfg.detailSampleDist = cfg.cs * 6;
        cfg.detailSampleMaxError = cfg.ch;
        std::vector<float> vertices;
        vertices.reserve(triangles.size() * 3);
        for(const auto point : triangles) {
            if(!Math::is_finite(point))
                return fail("Navigation geometry contains nonfinite vertices");
            vertices.insert(vertices.end(), {point.x, point.y, point.z});
        }
        std::vector<int> indices(triangles.size());
        std::iota(indices.begin(), indices.end(), 0);
        const int count = int(triangles.size() / 3);
        std::vector<unsigned char> areas(count, RC_NULL_AREA);
        rcContext context;
        std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> height(
            rcAllocHeightfield(), rcFreeHeightField);
        std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(
            rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(
            rcAllocContourSet(), rcFreeContourSet);
        std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> polygons(
            rcAllocPolyMesh(), rcFreePolyMesh);
        std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail(
            rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
        if(!height || !compact || !contours || !polygons || !detail)
            return fail("Cannot allocate navigation build data");
        if(!rcCreateHeightfield(
               &context, *height, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
            return fail("Cannot create navigation heightfield");
        rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, vertices.data(),
            int(triangles.size()), indices.data(), count, areas.data());
        if(!rcRasterizeTriangles(&context, vertices.data(), int(triangles.size()), indices.data(),
               areas.data(), count, *height, cfg.walkableClimb))
            return fail("Cannot rasterize navigation geometry");
        rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *height);
        rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *height);
        rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *height);
        if(!rcBuildCompactHeightfield(
               &context, cfg.walkableHeight, cfg.walkableClimb, *height, *compact)
            || !rcErodeWalkableArea(&context, cfg.walkableRadius, *compact)
            || !rcBuildDistanceField(&context, *compact)
            || !rcBuildRegions(&context, *compact, 0, cfg.minRegionArea, cfg.mergeRegionArea)
            || !rcBuildContours(
                &context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours)
            || !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *polygons)
            || !rcBuildPolyMeshDetail(&context, *polygons, *compact, cfg.detailSampleDist,
                cfg.detailSampleMaxError, *detail))
            return fail("Cannot build navigation polygons");
        if(!polygons->npolys)
            return fail("Navigation region contains no walkable surface");
        for(int i = 0; i < polygons->npolys; ++i) {
            polygons->areas[i] = 0;
            polygons->flags[i] = 1;
        }
        dtNavMeshCreateParams params{};
        params.verts = polygons->verts;
        params.vertCount = polygons->nverts;
        params.polys = polygons->polys;
        params.polyAreas = polygons->areas;
        params.polyFlags = polygons->flags;
        params.polyCount = polygons->npolys;
        params.nvp = polygons->nvp;
        params.detailMeshes = detail->meshes;
        params.detailVerts = detail->verts;
        params.detailVertsCount = detail->nverts;
        params.detailTris = detail->tris;
        params.detailTriCount = detail->ntris;
        params.walkableHeight = s.agent_height;
        params.walkableRadius = s.agent_radius;
        params.walkableClimb = s.max_climb;
        std::copy_n(polygons->bmin, 3, params.bmin);
        std::copy_n(polygons->bmax, 3, params.bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;
        unsigned char* data = nullptr;
        int size = 0;
        if(!dtCreateNavMeshData(&params, &data, &size))
            return fail("Cannot create Detour navigation mesh");
        std::unique_ptr<unsigned char, decltype(&dtFree)> owned(data, dtFree);
        auto result = std::make_unique<Impl>();
        result->mesh.reset(dtAllocNavMesh());
        result->query.reset(dtAllocNavMeshQuery());
        if(!result->mesh || !result->query
            || dtStatusFailed(result->mesh->init(data, size, DT_TILE_FREE_DATA)))
            return fail("Cannot initialize navigation mesh");
        owned.release();
        if(dtStatusFailed(result->query->init(result->mesh.get(), 2048)))
            return fail("Cannot initialize navigation query");
        result->extents = {std::max(cfg.cs * 2, 0.3f), std::max(s.max_climb + cfg.ch * 2, 0.5f),
            std::max(cfg.cs * 2, 0.3f)};
        m_impl = std::move(result);
        return Result<void>::success();
    }

    Result<NavigationMesh::Path> NavigationMesh::find_path(
        Math::Vec3 start, Math::Vec3 target) const {
        using Output = Result<Path>;
        if(!m_impl || !Math::is_finite(start) || !Math::is_finite(target))
            return Output::failure(
                "Path query requires a built navigation mesh and finite positions");
        dtPolyRef first = 0, last = 0;
        Math::Vec3 a, b;
        if(!m_impl->nearest(start, first, a) || !m_impl->nearest(target, last, b))
            return Output::success({});
        std::array<dtPolyRef, 256> corridor{};
        int count = 0;
        auto status = m_impl->query->findPath(first, last, &a.x, &b.x, &m_impl->filter,
            corridor.data(), &count, int(corridor.size()));
        if(dtStatusFailed(status))
            return Output::failure("Detour path query failed");
        if(dtStatusDetail(status, DT_OUT_OF_NODES) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
            return Output::success({Status::BudgetExceeded, {}});
        if(!count || corridor[count - 1] != last || dtStatusDetail(status, DT_PARTIAL_RESULT))
            return Output::success({});
        std::array<float, 256 * 3> points{};
        int point_count = 0;
        status = m_impl->query->findStraightPath(
            &a.x, &b.x, corridor.data(), count, points.data(), nullptr, nullptr, &point_count, 256);
        if(dtStatusFailed(status))
            return Output::failure("Detour straight path query failed");
        if(dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
            return Output::success({Status::BudgetExceeded, {}});
        Path result{Status::Complete, {}};
        for(int i = 0; i < point_count; ++i)
            result.points.emplace_back(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
        return Output::success(std::move(result));
    }
    std::optional<Math::Vec3> NavigationMesh::surface_step(
        Math::Vec3 start, Math::Vec3 target) const {
        if(!m_impl || !Math::is_finite(start) || !Math::is_finite(target))
            return std::nullopt;
        dtPolyRef ref = 0;
        Math::Vec3 projected;
        if(!m_impl->nearest(start, ref, projected))
            return std::nullopt;
        float fraction = 0;
        float normal[3]{};
        std::array<dtPolyRef, 32> crossed{};
        int count = 0;
        const auto status = m_impl->query->raycast(ref, &projected.x, &target.x, &m_impl->filter,
            &fraction, normal, crossed.data(), &count, int(crossed.size()));
        if(dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) || fraction < 1
            || !count)
            return std::nullopt;
        if(dtStatusFailed(m_impl->query->getPolyHeight(crossed[count - 1], &target.x, &target.y)))
            return std::nullopt;
        return target;
    }
}
