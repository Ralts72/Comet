#include "gameplay/navigation_mesh.h"
#include "scene/components.h"
#include "asset/data/navigation_data.h"
#include "common/binary.h"
#include <bit>
#include <cstring>
#include <utility>
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
    namespace {
        Result<rcConfig> configuration(std::span<const Math::Vec3> triangles, Math::Vec3 center,
            const NavigationRegionComponent& s) {
            const auto fail = [](const char* message) {
                return Result<rcConfig>::failure(message);
            };
            if(!Math::is_finite(center) || !Math::is_finite(s.half_extents)
                || !Math::all_greater(s.half_extents, Math::Vec3(0)) || !std::isfinite(s.cell_size)
                || s.cell_size < 0.05f || s.cell_size > 2 || !std::isfinite(s.agent_radius)
                || s.agent_radius <= 0 || s.agent_radius > 10 || !std::isfinite(s.agent_height)
                || s.agent_height < s.agent_radius * 2 || s.agent_height > 20
                || !std::isfinite(s.max_climb) || s.max_climb < 0 || s.max_climb > s.agent_height
                || !std::isfinite(s.max_slope) || s.max_slope < 0 || s.max_slope >= 90)
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
            for(const auto point : triangles)
                if(!Math::is_finite(point))
                    return fail("Navigation geometry contains nonfinite vertices");
            return Result<rcConfig>::success(cfg);
        }
        Result<void> validate_links(std::span<const NavigationMesh::Link> links) {
            if(links.size() > 256)
                return Result<void>::failure("最多支持 256 个导航连接");
            for(const auto& link : links)
                if(!Math::is_finite(link.start) || !Math::is_finite(link.end)
                    || !std::isfinite(link.radius) || link.radius <= 0 || link.radius > 10
                    || Math::length(link.end - link.start) < .01f)
                    return Result<void>::failure("无效的导航连接端点或半径");
            return Result<void>::success();
        }
        Result<void> validate_product(const NavigationData& product) {
            const auto fail = [](const char* message) { return Result<void>::failure(message); };
            if(auto valid = product.validate(); !valid)
                return valid;
            const uint32_t byte_order = std::endian::native == std::endian::little ? 1 : 2;
            if(product.library_version != DT_NAVMESH_VERSION
                || product.reference_bytes != sizeof(dtPolyRef) || product.byte_order != byte_order
                || product.tile.size() < sizeof(dtMeshHeader))
                return fail("Navigation backend format changed; rebake the product");
            dtMeshHeader header;
            std::memcpy(&header, product.tile.data(), sizeof(header));
            if(header.magic != DT_NAVMESH_MAGIC || header.version != DT_NAVMESH_VERSION
                || header.polyCount <= 0 || header.polyCount > 1000000 || header.vertCount < 3
                || header.vertCount >= 65535 || header.maxLinkCount <= 0
                || header.detailMeshCount != header.offMeshBase || header.detailVertCount < 0
                || header.detailTriCount <= 0 || header.bvNodeCount < 0
                || header.offMeshConCount < 0 || header.offMeshConCount > 256
                || header.offMeshBase <= 0
                || int64_t(header.offMeshBase) + header.offMeshConCount != header.polyCount)
                return fail("Invalid navigation tile counts");
            const auto aligned = [](uint64_t size) { return (size + 3) & ~uint64_t(3); };
            const uint64_t vertices = aligned(sizeof(dtMeshHeader));
            const uint64_t polygons =
                vertices + aligned(uint64_t(header.vertCount) * 3 * sizeof(float));
            const uint64_t links = polygons + aligned(uint64_t(header.polyCount) * sizeof(dtPoly));
            const uint64_t details =
                links + aligned(uint64_t(header.maxLinkCount) * sizeof(dtLink));
            const uint64_t detail_vertices =
                details + aligned(uint64_t(header.detailMeshCount) * sizeof(dtPolyDetail));
            const uint64_t triangles =
                detail_vertices + aligned(uint64_t(header.detailVertCount) * 3 * sizeof(float));
            const uint64_t tree = triangles + aligned(uint64_t(header.detailTriCount) * 4);
            const uint64_t off_mesh =
                tree + aligned(uint64_t(header.bvNodeCount) * sizeof(dtBVNode));
            const uint64_t size =
                off_mesh + aligned(uint64_t(header.offMeshConCount) * sizeof(dtOffMeshConnection));
            if(size != product.tile.size())
                return fail("Navigation tile counts exceed its payload");
            const auto read = [&]<typename T>(uint64_t offset) {
                T value;
                std::memcpy(&value, product.tile.data() + offset, sizeof(value));
                return value;
            };
            for(int axis = 0; axis < 3; ++axis)
                if(!std::isfinite(header.bmin[axis]) || !std::isfinite(header.bmax[axis])
                    || header.bmin[axis] > header.bmax[axis])
                    return fail("Invalid navigation tile bounds");
            if(!std::isfinite(header.bvQuantFactor) || header.bvQuantFactor <= 0)
                return fail("Invalid navigation tile quantization");
            for(const auto& [offset, count] : {std::pair{vertices, header.vertCount},
                    std::pair{detail_vertices, header.detailVertCount}})
                for(int i = 0; i < count * 3; ++i)
                    if(!std::isfinite(
                           read.template operator()<float>(offset + uint64_t(i) * sizeof(float))))
                        return fail("Navigation tile contains nonfinite vertices");
            for(int i = 0; i < header.polyCount; ++i) {
                const auto poly =
                    read.template operator()<dtPoly>(polygons + uint64_t(i) * sizeof(dtPoly));
                const bool ground = i < header.offMeshBase;
                if(poly.vertCount < (ground ? 3 : 2) || poly.vertCount > DT_VERTS_PER_POLYGON
                    || (ground ? poly.getType() != DT_POLYTYPE_GROUND
                               : poly.getType() != DT_POLYTYPE_OFFMESH_CONNECTION
                                     || poly.vertCount != 2))
                    return fail("Invalid navigation polygon");
                for(int vertex = 0; vertex < poly.vertCount; ++vertex)
                    if(poly.verts[vertex] >= header.vertCount
                        || (!(poly.neis[vertex] & DT_EXT_LINK)
                            && poly.neis[vertex] > header.polyCount))
                        return fail("Navigation polygon references an invalid vertex or neighbor");
                if(!ground)
                    continue;
                const auto detail = read.template operator()<dtPolyDetail>(
                    details + uint64_t(i) * sizeof(dtPolyDetail));
                if(uint64_t(detail.vertBase) + detail.vertCount > uint64_t(header.detailVertCount)
                    || uint64_t(detail.triBase) + detail.triCount > uint64_t(header.detailTriCount))
                    return fail("Invalid navigation detail range");
                for(unsigned triangle = 0; triangle < detail.triCount; ++triangle)
                    for(unsigned vertex = 0; vertex < 3; ++vertex) {
                        const auto index = std::to_integer<unsigned>(
                            product.tile[triangles + (uint64_t(detail.triBase) + triangle) * 4
                                         + vertex]);
                        if(index >= unsigned(poly.vertCount) + detail.vertCount)
                            return fail("Navigation detail triangle references an invalid vertex");
                    }
            }
            for(int i = 0; i < header.offMeshConCount; ++i) {
                const auto connection = read.template operator()<dtOffMeshConnection>(
                    off_mesh + uint64_t(i) * sizeof(dtOffMeshConnection));
                if(connection.poly != header.offMeshBase + i || !std::isfinite(connection.rad)
                    || connection.rad <= 0 || (connection.flags & ~DT_OFFMESH_CON_BIDIR)
                    || (connection.side != 0xff && connection.side > 7))
                    return fail("Invalid navigation off-mesh connection");
                for(const auto coordinate : connection.pos)
                    if(!std::isfinite(coordinate))
                        return fail("Nonfinite navigation off-mesh endpoint");
            }
            for(int i = 0; i < header.bvNodeCount; ++i) {
                const auto node =
                    read.template operator()<dtBVNode>(tree + uint64_t(i) * sizeof(dtBVNode));
                if(node.i >= header.polyCount
                    || (node.i < 0 && -int64_t(node.i) > header.bvNodeCount - i))
                    return fail("Navigation BVH references an invalid node");
            }
            return Result<void>::success();
        }
    }

    struct NavigationMesh::Impl {
        std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> mesh{nullptr, dtFreeNavMesh};
        std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> query{
            nullptr, dtFreeNavMeshQuery};
        dtQueryFilter filter;
        Math::Vec3 extents{0.5f, 1, 0.5f};
        bool empty = false;
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

    Result<uint64_t> NavigationMesh::input_fingerprint(std::span<const Math::Vec3> triangles,
        Math::Vec3 center, const NavigationRegionComponent& settings, std::span<const Link> links) {
        if(auto described = configuration(triangles, center, settings); !described)
            return Result<uint64_t>::failure(described.error());
        if(auto valid = validate_links(links); !valid)
            return Result<uint64_t>::failure(valid.error());
        std::vector<uint64_t> triangle_hashes;
        triangle_hashes.reserve(triangles.size() / 3);
        for(size_t i = 0; i < triangles.size(); i += 3) {
            std::array<float, 9> values;
            for(size_t vertex = 0; vertex < 3; ++vertex)
                for(int axis = 0; axis < 3; ++axis) {
                    const float value = triangles[i + vertex][axis];
                    values[vertex * 3 + axis] = value == 0 ? 0 : value;
                }
            triangle_hashes.push_back(Binary::hash_bytes(std::as_bytes(std::span(values))));
        }
        // 后端或实体插入顺序改变不会让相同碰撞几何失效；绕序仍参与身份。
        std::ranges::sort(triangle_hashes);
        Binary::Writer input;
        input.write_u32(1); // 烘焙算法版本；改变 Recast 构建策略时更新。
        input.write_u32(DT_NAVMESH_VERSION);
        input.write_u32(sizeof(dtPolyRef));
        input.write_u32(std::endian::native == std::endian::little ? 1 : 2);
        for(const auto vector : {center, settings.half_extents})
            for(int axis = 0; axis < 3; ++axis)
                input.write_float(vector[axis] == 0 ? 0 : vector[axis]);
        for(const float value : {settings.cell_size, settings.agent_radius, settings.agent_height,
                settings.max_climb, settings.max_slope})
            input.write_float(value == 0 ? 0 : value);
        if(!links.empty()) {
            input.write_u32(0x4c494e4b);
            input.write_u32(uint32_t(links.size()));
            for(const auto& link : links) {
                for(const auto point : {link.start, link.end})
                    for(int axis = 0; axis < 3; ++axis)
                        input.write_float(point[axis] == 0 ? 0 : point[axis]);
                input.write_float(link.radius);
                input.write_u32(link.bidirectional ? 1 : 0);
            }
        }
        return Result<uint64_t>::success(Binary::hash_bytes(
            std::as_bytes(std::span(triangle_hashes)), Binary::hash_bytes(input.data())));
    }

    Result<NavigationData> NavigationMesh::product(const uint64_t fingerprint) const {
        if(!m_impl || m_impl->empty)
            return Result<NavigationData>::failure("Navigation product requires a built mesh");
        const auto* tile = std::as_const(*m_impl->mesh).getTile(0);
        if(!tile || !tile->data || tile->dataSize <= 0)
            return Result<NavigationData>::failure("Navigation mesh has no tile");
        NavigationData result{.library_version = DT_NAVMESH_VERSION,
            .reference_bytes = sizeof(dtPolyRef),
            .byte_order = std::endian::native == std::endian::little ? 1u : 2u,
            .input_fingerprint = fingerprint,
            .query_extents = m_impl->extents};
        const auto* bytes = reinterpret_cast<const std::byte*>(tile->data);
        result.tile.assign(bytes, bytes + tile->dataSize);
        if(auto valid = result.validate(); !valid)
            return Result<NavigationData>::failure(valid.error());
        return Result<NavigationData>::success(std::move(result));
    }

    Result<void> NavigationMesh::load(const NavigationData& product) {
        if(auto valid = validate_product(product); !valid)
            return valid;
        const auto fail = [](const char* message) { return Result<void>::failure(message); };
        auto result = std::make_unique<Impl>();
        result->mesh.reset(dtAllocNavMesh());
        result->query.reset(dtAllocNavMeshQuery());
        auto* data = static_cast<unsigned char*>(dtAlloc(product.tile.size(), DT_ALLOC_PERM));
        std::unique_ptr<unsigned char, decltype(&dtFree)> owned(data, dtFree);
        if(!result->mesh || !result->query || !data)
            return fail("Cannot allocate navigation product");
        std::memcpy(data, product.tile.data(), product.tile.size());
        if(dtStatusFailed(result->mesh->init(data, int(product.tile.size()), DT_TILE_FREE_DATA)))
            return fail("Cannot initialize navigation product");
        owned.release();
        if(dtStatusFailed(result->query->init(result->mesh.get(), 2048)))
            return fail("Cannot initialize navigation product query");
        result->extents = product.query_extents;
        m_impl = std::move(result);
        return Result<void>::success();
    }

    Result<void> NavigationMesh::build(std::span<const Math::Vec3> triangles, Math::Vec3 center,
        const NavigationRegionComponent& s, std::span<const Link> links,
        std::span<const Obstacle> obstacles) {
        if(auto valid = validate_links(links); !valid)
            return valid;
        if(obstacles.size() > 128)
            return Result<void>::failure("最多支持 128 个动态导航障碍");
        for(const auto& obstacle : obstacles)
            if(!Math::is_finite(obstacle.minimum) || !Math::is_finite(obstacle.maximum)
                || !Math::all_less(obstacle.minimum, obstacle.maximum))
                return Result<void>::failure("无效的动态导航障碍范围");
        auto described = configuration(triangles, center, s);
        if(!described)
            return Result<void>::failure(described.error());
        const auto cfg = described.value();
        const auto fail = [](const char* message) { return Result<void>::failure(message); };
        std::vector<float> vertices;
        vertices.reserve(triangles.size() * 3);
        for(const auto point : triangles) {
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
               &context, cfg.walkableHeight, cfg.walkableClimb, *height, *compact))
            return fail("Cannot build compact navigation heightfield");
        for(const auto& obstacle : obstacles) {
            // 头部空间也受障碍影响；水平净空统一交给后续 walkableRadius 侵蚀。
            const auto minimum = obstacle.minimum - Math::Vec3(0, s.agent_height, 0);
            rcMarkBoxArea(&context, &minimum.x, &obstacle.maximum.x, RC_NULL_AREA, *compact);
        }
        if(!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact)
            || !rcBuildDistanceField(&context, *compact)
            || !rcBuildRegions(&context, *compact, 0, cfg.minRegionArea, cfg.mergeRegionArea)
            || !rcBuildContours(
                &context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours))
            return fail("Cannot build navigation contours");
        if(contours->nconts == 0 && !obstacles.empty()) {
            m_impl = std::make_unique<Impl>();
            m_impl->empty = true;
            return Result<void>::success();
        }
        if(!rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *polygons)
            || !rcBuildPolyMeshDetail(&context, *polygons, *compact, cfg.detailSampleDist,
                cfg.detailSampleMaxError, *detail))
            return fail("Cannot build navigation polygons");
        if(!polygons->npolys)
            return fail("Navigation region contains no walkable surface");
        if(polygons->nverts + links.size() * 2 >= 65535)
            return fail("Navigation links exceed the tile vertex budget");
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
        std::vector<float> connection_vertices, connection_radii;
        std::vector<unsigned short> connection_flags(links.size(), 1);
        std::vector<unsigned char> connection_areas(links.size(), 0), connection_directions;
        std::vector<unsigned> connection_ids;
        for(std::size_t index = 0; index < links.size(); ++index) {
            const auto& link = links[index];
            for(const auto point : {link.start, link.end})
                connection_vertices.insert(connection_vertices.end(), {point.x, point.y, point.z});
            connection_radii.push_back(link.radius);
            connection_directions.push_back(link.bidirectional ? DT_OFFMESH_CON_BIDIR : 0);
            connection_ids.push_back(unsigned(index + 1));
        }
        params.offMeshConCount = int(links.size());
        params.offMeshConVerts = connection_vertices.data();
        params.offMeshConRad = connection_radii.data();
        params.offMeshConFlags = connection_flags.data();
        params.offMeshConAreas = connection_areas.data();
        params.offMeshConDir = connection_directions.data();
        params.offMeshConUserID = connection_ids.data();
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
        if(m_impl->empty)
            return Output::success({});
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
        std::array<unsigned char, 256> flags{};
        std::array<dtPolyRef, 256> references{};
        int point_count = 0;
        status = m_impl->query->findStraightPath(&a.x, &b.x, corridor.data(), count, points.data(),
            flags.data(), references.data(), &point_count, 256);
        if(dtStatusFailed(status))
            return Output::failure("Detour straight path query failed");
        if(dtStatusDetail(status, DT_BUFFER_TOO_SMALL))
            return Output::success({Status::BudgetExceeded, {}});
        Path result{Status::Complete, {}};
        for(int i = 0; i < point_count; ++i) {
            result.points.emplace_back(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
            if(flags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) {
                const auto* connection = m_impl->mesh->getOffMeshConnectionByRef(references[i]);
                if(!connection)
                    return Output::failure("Missing navigation connection in path");
                const auto found =
                    std::find(corridor.begin(), corridor.begin() + count, references[i]);
                Math::Vec3 entry, endpoint;
                if(found == corridor.begin() || found == corridor.begin() + count
                    || dtStatusFailed(m_impl->mesh->getOffMeshConnectionPolyEndPoints(
                        *(found - 1), references[i], &entry.x, &endpoint.x)))
                    return Output::failure("Invalid navigation connection corridor");
                result.jumps.push_back({std::size_t(i), endpoint, connection->rad});
            }
        }
        return Output::success(std::move(result));
    }
    std::optional<Math::Vec3> NavigationMesh::surface_step(
        Math::Vec3 start, Math::Vec3 target) const {
        if(!m_impl || m_impl->empty || !Math::is_finite(start) || !Math::is_finite(target))
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
