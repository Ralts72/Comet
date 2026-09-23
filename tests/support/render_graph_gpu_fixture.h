#pragma once

#include "support/render_gpu_test.h"
#include "render/render_graph.h"
#include "render/passes/output_pass.h"
#include "render/passes/shadow_pass.h"
#include <cmath>
#include "core/engine.h"
#include "core/window.h"
#include "config/config.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/command/command_context.h"
#include "graphics/render_pass.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/frame_scheduler.h"
#include "render/scene/scene_renderer.h"
#include "render/scene/render_scene.h"
#include "render/render_target.h"
#include "graphics/resource/image.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "diagnostics/logger.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render/resource/environment.h"
#include <numbers>
#include "render/resource/render_resources.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "asset/import/environment_importer.h"
#include "render/material/material.h"
#include "common/file_io.h"
#include "shader/compiler.h"
#include "support/temporary_directory.h"
#include "support/hdr_image.h"
#include "pbr_vert.h"

#include <gtest/gtest.h>
#include <glm/gtc/packing.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <cstring>
#include <cstdlib>
#include <sstream>

namespace Comet::Tests {
    class RenderGraphGpuTest: public RenderGpuTest {
    protected:
        static glm::dvec3 pbr_reference(glm::dvec3 normal, glm::dvec3 view, glm::dvec3 light,
            glm::dvec3 base, double metallic, double roughness, double radiance) {
            if(glm::length(normal) < 1e-6)
                return glm::dvec3(0);
            normal = glm::normalize(normal);
            view = glm::normalize(view);
            light = glm::normalize(light);
            const double nl = std::clamp(glm::dot(normal, light), 0.0, 1.0);
            const double nv = std::clamp(glm::dot(normal, view), 0.0, 1.0);
            if(nl <= 0 || nv <= 0)
                return glm::dvec3(0);
            const auto halfway = glm::normalize(light + view);
            const double nh = std::clamp(glm::dot(normal, halfway), 0.0, 1.0);
            const double vh = std::clamp(glm::dot(view, halfway), 0.0, 1.0);
            const double a2 = std::pow(std::clamp(roughness, 0.045, 1.0), 4);
            const double distribution =
                a2 / (glm::pi<double>() * std::pow(nh * nh * (a2 - 1) + 1, 2));
            const double geometry = 2 * nl * nv
                                    / (nv * std::sqrt(a2 + (1 - a2) * nl * nl)
                                        + nl * std::sqrt(a2 + (1 - a2) * nv * nv));
            const double grazing = std::pow(1 - vh, 5);
            const double fresnel = 0.04 + 0.96 * grazing;
            const auto dielectric = base * (1 - fresnel) / glm::pi<double>()
                                    + glm::dvec3(fresnel * distribution * geometry / (4 * nl * nv));
            const auto metal =
                (base + (1.0 - base) * grazing) * distribution * geometry / (4 * nl * nv);
            return glm::mix(dielectric, metal, std::clamp(metallic, 0.0, 1.0)) * radiance * nl;
        }
        static int mapped_byte(float hdr, float exposure = 1.0f) {
            const auto linear = 1.0f - std::exp(-std::max(hdr, 0.0f) * exposure);
            return static_cast<int>(std::lround(encode_srgb(linear) * 255.0f));
        }
        std::shared_ptr<Mesh> lit_quad(Math::Vec3 normal = {0, 0, 1}) {
            MeshData data{
                .vertices = {{{-1, -1, 0.5f}, {0, 0}, normal}, {{1, -1, 0.5f}, {1, 0}, normal},
                    {{1, 1, 0.5f}, {1, 1}, normal}, {{-1, 1, 0.5f}, {0, 1}, normal}},
                .indices = {0, 1, 2, 2, 3, 0}};
            auto created = engine->get_render_resources().try_create_mesh(data);
            if(!created) {
                ADD_FAILURE() << created.error().message;
                return nullptr;
            }
            auto mesh = std::move(created).value();
            mesh->get_ready_completion().wait();
            return mesh;
        }
    };

}
