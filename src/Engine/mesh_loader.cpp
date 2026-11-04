//
// Created by Piotr Białas on 04/11/2021.
//


#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG

#include "mesh_loader.h"

#include <unordered_map>

#include <filesystem>
#include <memory>

#include "spdlog/spdlog.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "ObjectReader/obj_reader.h"
#include "Engine/Material.h"
#include "Engine/Mesh.h"


namespace xe {

    static std::unordered_map<std::string, mat_function_t> mat_functions;

    using uint = unsigned int;

    // Like get_mat_function, but without reporting a missing function.
    static mat_function_t find_mat_function(const std::string &name) {
        auto it = mat_functions.find(name);
        return it != mat_functions.end() ? it->second : nullptr;
    }

    static Material *create_material(const std::string &name, const mtl_material_t &mat, const std::string &mtl_dir) {
        auto func = find_mat_function(name);
        if (!func) {
            spdlog::error("Material {} is not registered, call its init() before loading meshes", name);
            return nullptr;
        }
        return func(mat, mtl_dir);
    }

    // Used for submeshes without a usable material. NullMaterial would draw them with whatever program and uniform
    // buffers the previously drawn material left bound.
    static Material *default_material(const std::string &reason) {
        if (!find_mat_function("KdMaterial")) {
            spdlog::warn("{} and KdMaterial is not registered, it will be drawn with the current OpenGL state", reason);
            return xe::NullMaterial::null_material();
        }
        spdlog::warn("{}, using a gray KdMaterial", reason);
        mtl_material_t mat{};
        mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 0.8f;
        mat.dissolve = 1.0f;
        return create_material("KdMaterial", mat, "");
    }

    Mesh *load_mesh_from_obj(std::string path, std::string mtl_dir) {
        // The texture paths in the MTL file are relative to its directory. Like load_smesh_from_obj, look for it
        // next to the OBJ file by default, so the materials get the directory too and not an empty string.
        if (mtl_dir.empty()) {
            auto obj_dir = std::filesystem::path(path).parent_path();
            mtl_dir = obj_dir.empty() ? "." : obj_dir.string();
        }

        auto smesh = xe::load_smesh_from_obj(path, mtl_dir);
        if (smesh.vertex_coords.empty())
            return nullptr;


        uint n_floats_per_vertex = 3;
        for (auto &&t: smesh.has_texcoords) {
            if (t)
                n_floats_per_vertex += 2;
        }
        if (smesh.has_normals)
            n_floats_per_vertex += 3;
        if (smesh.has_tangents)
            n_floats_per_vertex += 4;


        size_t stride = n_floats_per_vertex * sizeof(GLfloat);

        auto n_vertices = smesh.vertex_coords.size();
        auto n_indices = 3 * smesh.faces.size(); //assumes triangles

        SPDLOG_DEBUG("Loaded sMesh n_floats_per_vertex from {} : {} n_vertices: {} n_indices: {}", path,
                     n_floats_per_vertex,
                     n_vertices, n_indices);

        size_t vertex_buffer_size = smesh.vertex_coords.size() * stride;
        size_t index_buffer_size = smesh.faces.size() * 3 * sizeof(uint16_t);

        SPDLOG_DEBUG("vertex_buffer_size: {} index_buffer_size: {}", vertex_buffer_size, index_buffer_size);
        auto mesh = new Mesh(stride, vertex_buffer_size, GL_STATIC_DRAW,
                             index_buffer_size, GL_UNSIGNED_SHORT, GL_STATIC_DRAW);


        mesh->load_indices(0, n_indices * sizeof(uint16_t), smesh.faces.data());
        mesh->add_attribute(xe::AttributeType::POSITION, 3, GL_FLOAT, 0);


        auto v_ptr = reinterpret_cast<uint8_t *>(mesh->map_vertex_buffer());

        size_t offset = 0;

        {

            auto v_offset = offset;
            for (auto i = 0; i < smesh.vertex_coords.size(); i++, v_offset += stride) {
                SPDLOG_TRACE("vertex[{}] {} ", i, glm::to_string(smesh.vertex_coords[i]));
                std::memcpy(v_ptr + v_offset, glm::value_ptr(smesh.vertex_coords[i]), sizeof(glm::vec3));
            }
        }
        offset += 3 * sizeof(GLfloat);

        for (int it = 0; it < xe::sMesh::MAX_TEXCOORDS; it++) {
            if (smesh.has_texcoords[it]) {
                mesh->add_attribute(static_cast<xe::AttributeType>(xe::AttributeType::TEXCOORD_0 + it), 2, GL_FLOAT,
                                    offset);

                auto v_offset = offset;
                for (auto i = 0; i < smesh.vertex_texcoords[it].size(); i++, v_offset += stride) {
                    SPDLOG_TRACE("texcoord[{}] {} ", i, glm::to_string(smesh.vertex_texcoords[it][i]));
                    std::memcpy(v_ptr + v_offset, glm::value_ptr(smesh.vertex_texcoords[it][i]), sizeof(glm::vec2));
                }
                offset += 2 * sizeof(GLfloat);
            }
        }
        if (smesh.has_normals) {

            mesh->add_attribute(xe::AttributeType::NORMAL, 3, GL_FLOAT, offset);

            auto v_offset = offset;
            for (auto i = 0; i < smesh.vertex_normals.size(); i++, v_offset += stride) {
                SPDLOG_TRACE("normal[{}] {} ", i, glm::to_string(smesh.vertex_normals[i]));
                std::memcpy(v_ptr + v_offset, glm::value_ptr(smesh.vertex_normals[i]), sizeof(glm::vec3));
            }

            offset += 3 * sizeof(GLfloat);
        }

        if (smesh.has_tangents) {
            mesh->add_attribute(xe::AttributeType::TANGENT, 4, GL_FLOAT, offset);

            auto v_offset = offset;
            for (auto i = 0; i < smesh.vertex_tangents.size(); i++, v_offset += stride) {
                SPDLOG_TRACE("tangent[{}] {} ", i, glm::to_string(smesh.vertex_tangents[i]));
                std::memcpy(v_ptr + v_offset, glm::value_ptr(smesh.vertex_tangents[i]), sizeof(glm::vec4));
            }

            offset += 4 * sizeof(GLfloat);
        }

        mesh->unmap_vertex_buffer();


        // Submeshes are split at every usemtl, so the same material can come up several times. Create it once and
        // share it, so its textures are not loaded again. The materials are owned by the RegisteredObject registry.
        std::unordered_map<int, Material *> materials;

        for (int i = 0; i < smesh.submeshes.size(); i++) {
            auto sm = smesh.submeshes[i];

            auto cached = materials.find(sm.mat_idx);
            if (cached != materials.end()) {
                mesh->add_submesh(3 * sm.start, 3 * sm.end, cached->second);
                continue;
            }

            Material *material = nullptr;
            if (sm.mat_idx >= 0) {
                auto mat = smesh.materials[sm.mat_idx];
                SPDLOG_DEBUG("Material illum {}", mat.illum);
                switch (mat.illum) {
                    case 0:
                        material = create_material("KdMaterial", mat, mtl_dir);
                        break;
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                    case 10:
                        // illum 4 to 10 (written e.g. by Blender) add transparency and refraction to illum 2 and 3,
                        // which we do not support. The material is better drawn without them than replaced by a
                        // gray one.
                        if (mat.illum > 3)
                            spdlog::warn("Material {} uses illum {}, treated like illum 2: transparency and refraction "
                                         "are ignored", mat.name, mat.illum);
                        // Lighting needs normals; without them the shaders would compute NaN.
                        if (smesh.has_normals) {
                            material = create_material("BlinnPhongMaterial", mat, mtl_dir);
                        } else if (find_mat_function("KdMaterial")) {
                            spdlog::warn("Material {} needs normals, which {} does not have, using KdMaterial",
                                         mat.name, path);
                            material = create_material("KdMaterial", mat, mtl_dir);
                        } else {
                            spdlog::warn("Material {} needs normals, which {} does not have", mat.name, path);
                        }
                        break;
                    default:
                        spdlog::error("Unknown illumination model {}", mat.illum);
                        break;
                }
            }
            if (!material) {
                material = sm.mat_idx < 0
                               ? default_material(fmt::format("Submesh {} of {} has no material", i, path))
                               : default_material(fmt::format("Material {} of submesh {} of {} cannot be used",
                                                              smesh.materials[sm.mat_idx].name, i, path));
            }

            materials[sm.mat_idx] = material;
            SPDLOG_DEBUG("Adding primitive {:4d} {:4d} {:4d}", i, 3 * sm.start, 3 * sm.end);
            mesh->add_submesh(3 * sm.start, 3 * sm.end, material);
        }

        return mesh;
    }

    mat_function_t add_mat_function(std::string name, mat_function_t func) {
        mat_functions[name] = func;
        return func;
    }

    mat_function_t get_mat_function(std::string name) {
        auto func = find_mat_function(name);
        if (func) {
            return func;
        } else {
            spdlog::error("Cannot find material function {}", name);
            return nullptr;
        }
    }
}

