#include "core/math_utils.h"

#include <glm/gtx/matrix_decompose.hpp>

namespace Comet::Math {
    std::optional<float> uniform_scale(const Mat4& matrix) {
        for(int i = 0; i < 4; ++i)
            if(!is_finite(matrix[i]))
                return std::nullopt;
        if(std::abs(matrix[0][3]) > 1e-5f || std::abs(matrix[1][3]) > 1e-5f
            || std::abs(matrix[2][3]) > 1e-5f || std::abs(matrix[3][3] - 1) > 1e-5f)
            return std::nullopt;
        Mat3 basis(matrix);
        const float scale = glm::length(basis[0]);
        if(scale < 1e-6f)
            return std::nullopt;
        basis /= scale;
        if(std::abs(glm::length(basis[1]) - 1) > .0001f
            || std::abs(glm::length(basis[2]) - 1) > .0001f
            || std::abs(glm::dot(basis[0], basis[1])) > .0001f
            || std::abs(glm::dot(basis[0], basis[2])) > .0001f
            || std::abs(glm::dot(basis[1], basis[2])) > .0001f || glm::determinant(basis) < 0)
            return std::nullopt;
        return scale;
    }
    std::optional<Mat4> without_scale(const Mat4& matrix) {
        Vec3 scale, translation, skew;
        Vec4 perspective;
        Quat rotation;
        if(!glm::decompose(matrix, scale, rotation, translation, skew, perspective))
            return std::nullopt;
        return translate(Mat4(1), translation) * to_mat4(glm::normalize(rotation));
    }
}
