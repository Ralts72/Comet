#include "geometry_utils.h"

namespace Comet{
    Comet::Cube GeometryUtils::create_cube(float left, float right, float top, float bottom, float near, float far, const Math::Mat4& transform) {
        const Math::Mat4 normal_matrix = Math::transpose(Math::inverse(transform));
        //    v6----- v5
        //   /|      /|
        //  v1------v0|
        //  | |     | |
        //  | |v7---|-|v4
        //  |/      |/
        //  v2------v3
        Cube cube_data;
        cube_data.vertices = {
                // v0-v1-v2-v3 front
                { transform * Math::Vec4(right, top, near, 1.f) },
                { transform * Math::Vec4(left, top, near, 1.f) },
                { transform * Math::Vec4(left, bottom, near, 1.f) },
                { transform * Math::Vec4(right, bottom, near, 1.f) },
                // v0-v3-v4-v5 right
                { transform * Math::Vec4(right, top, near, 1.f) },
                { transform * Math::Vec4(right, bottom, near, 1.f) },
                { transform * Math::Vec4(right, bottom, far, 1.f) },
                { transform * Math::Vec4(right, top, far, 1.f) },
                // v0-v5-v6-v1 up
                { transform * Math::Vec4(right, top, near, 1.f) },
                { transform * Math::Vec4(right, top, far, 1.f) },
                { transform * Math::Vec4(left, top, far, 1.f) },
                { transform * Math::Vec4(left, top, near, 1.f) },
                // v1-v6-v7-v2 left
                { transform * Math::Vec4(left, top, near, 1.f) },
                { transform * Math::Vec4(left, top, far, 1.f) },
                { transform * Math::Vec4(left, bottom, far, 1.f) },
                { transform * Math::Vec4(left, bottom, near, 1.f) },
                // v7-v4-v3-v2 bottom
                { transform * Math::Vec4(left, bottom, far, 1.f) },
                { transform * Math::Vec4(right, bottom, far, 1.f) },
                { transform * Math::Vec4(right, bottom, near, 1.f) },
                { transform * Math::Vec4(left, bottom, near, 1.f) },
                // v4-v7-v6-v5 back
                { transform * Math::Vec4(right, bottom, far, 1.f) },
                { transform * Math::Vec4(left, bottom, far, 1.f) },
                { transform * Math::Vec4(left, top, far, 1.f) },
                { transform * Math::Vec4(right, top, far, 1.f) }
        };
        cube_data.vertices[0].texcoord = Math::Vec2(0.0f, 0.0f);
        cube_data.vertices[1].texcoord = Math::Vec2(1.0f, 0.0f);
        cube_data.vertices[2].texcoord = Math::Vec2(1.0f, 1.0f);
        cube_data.vertices[3].texcoord = Math::Vec2(0.0f, 1.0f);
        cube_data.vertices[4].texcoord = Math::Vec2(1.0f, 0.0f);
        cube_data.vertices[5].texcoord = Math::Vec2(1.0f, 1.0f);
        cube_data.vertices[6].texcoord = Math::Vec2(0.0f, 1.0f);
        cube_data.vertices[7].texcoord = Math::Vec2(0.0f, 0.0f);
        cube_data.vertices[8].texcoord = Math::Vec2(1.0f, 0.0f);
        cube_data.vertices[9].texcoord = Math::Vec2(1.0f, 1.0f);
        cube_data.vertices[10].texcoord = Math::Vec2(0.0f, 1.0f);
        cube_data.vertices[11].texcoord = Math::Vec2(0.0f, 0.0f);
        cube_data.vertices[12].texcoord = Math::Vec2(0.0f, 0.0f);
        cube_data.vertices[13].texcoord = Math::Vec2(1.0f, 0.0f);
        cube_data.vertices[14].texcoord = Math::Vec2(1.0f, 1.0f);
        cube_data.vertices[15].texcoord = Math::Vec2(0.0f, 1.0f);
        cube_data.vertices[16].texcoord = Math::Vec2(0.0f, 0.0f);
        cube_data.vertices[17].texcoord = Math::Vec2(1.0f, 0.0f);
        cube_data.vertices[18].texcoord = Math::Vec2(1.0f, 1.0f);
        cube_data.vertices[19].texcoord = Math::Vec2(0.0f, 1.0f);
        cube_data.vertices[20].texcoord = Math::Vec2(1.0f, 1.0f);
        cube_data.vertices[21].texcoord = Math::Vec2(0.0f, 1.0f);
        cube_data.vertices[22].texcoord = Math::Vec2(0.0f, 0.0f);
        cube_data.vertices[23].texcoord = Math::Vec2(1.0f, 0.0f);

        cube_data.vertices[0].normal = normal_matrix * Math::Vec4(0, 0, 1, 1);
        cube_data.vertices[1].normal = normal_matrix * Math::Vec4(0, 0, 1, 1);
        cube_data.vertices[2].normal = normal_matrix * Math::Vec4(0, 0, 1, 1);
        cube_data.vertices[3].normal = normal_matrix * Math::Vec4(0, 0, 1, 1);
        cube_data.vertices[4].normal = normal_matrix * Math::Vec4(1, 0, 0, 1);
        cube_data.vertices[5].normal = normal_matrix * Math::Vec4(1, 0, 0, 1);
        cube_data.vertices[6].normal = normal_matrix * Math::Vec4(1, 0, 0, 1);
        cube_data.vertices[7].normal = normal_matrix * Math::Vec4(1, 0, 0, 1);
        cube_data.vertices[8].normal = normal_matrix * Math::Vec4(0, 1, 0, 1);
        cube_data.vertices[9].normal = normal_matrix * Math::Vec4(0, 1, 0, 1);
        cube_data.vertices[10].normal = normal_matrix * Math::Vec4(0, 1, 0, 1);
        cube_data.vertices[11].normal = normal_matrix * Math::Vec4(0, 1, 0, 1);
        cube_data.vertices[12].normal = normal_matrix * Math::Vec4(-1, 0, 0, 1);
        cube_data.vertices[13].normal = normal_matrix * Math::Vec4(-1, 0, 0, 1);
        cube_data.vertices[14].normal = normal_matrix * Math::Vec4(-1, 0, 0, 1);
        cube_data.vertices[15].normal = normal_matrix * Math::Vec4(-1, 0, 0, 1);
        cube_data.vertices[16].normal = normal_matrix * Math::Vec4(0, -1, 0, 1);
        cube_data.vertices[17].normal = normal_matrix * Math::Vec4(0, -1, 0, 1);
        cube_data.vertices[18].normal = normal_matrix * Math::Vec4(0, -1, 0, 1);
        cube_data.vertices[19].normal = normal_matrix * Math::Vec4(0, -1, 0, 1);
        cube_data.vertices[20].normal = normal_matrix * Math::Vec4(0, 0, -1, 1);
        cube_data.vertices[21].normal = normal_matrix * Math::Vec4(0, 0, -1, 1);
        cube_data.vertices[22].normal = normal_matrix * Math::Vec4(0, 0, -1, 1);
        cube_data.vertices[23].normal = normal_matrix * Math::Vec4(0, 0, -1, 1);

        cube_data.indices = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23
        };
        return cube_data;
    }
}
