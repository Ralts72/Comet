#include "core/math_utils.h"

#include <glm/gtx/matrix_decompose.hpp>

namespace Comet::Math {
    std::optional<Mat4> without_scale(const Mat4& matrix) {
        Vec3 scale, translation, skew;
        Vec4 perspective;
        Quat rotation;
        if(!glm::decompose(matrix, scale, rotation, translation, skew, perspective))
            return std::nullopt;
        return translate(Mat4(1), translation) * to_mat4(glm::normalize(rotation));
    }
}
