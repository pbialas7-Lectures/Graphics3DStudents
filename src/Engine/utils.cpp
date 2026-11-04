//
// Created by pbialas on 10.12.22.
//
#include "Engine/utils.h"

#include <algorithm>
#include <filesystem>

#include "glm/glm.hpp"

#include "Application/utils.h"


namespace xe {
    void check_and_use_program(GLuint program) {
        GLint current_program_;
        OGL_CALL(glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_));
        if (program != current_program_)
            OGL_CALL(glUseProgram(program));
    }


    glm::vec4 get_color(const float *c) {
        glm::vec4 color;
        for (int i = 0; i < 3; i++)
            color[i] = c[i];
        color[3] = 1.0;
        return color;
    }

    std::string texture_path(const std::string &mtl_dir, const std::string &name) {
        namespace fs = std::filesystem;

        std::string normalized = name;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const fs::path texture(normalized);

        // For an absolute texture the / operator gives the texture itself.
        const fs::path path = fs::path(mtl_dir) / texture;
        std::error_code error;
        if (fs::exists(path, error))
            return path.string();

        const fs::path in_mtl_dir = fs::path(mtl_dir) / texture.filename();
        if (fs::exists(in_mtl_dir, error))
            return in_mtl_dir.string();

        return path.string();
    }

    glm::vec3 srgb_inverse_gamma_correction(glm::vec3 color) {
        color = glm::clamp(color, 0.0f, 1.0f);
        color = glm::mix(color * 1.0f / 12.92f,
                         pow((color + 0.055f) / 1.055f, glm::vec3(2.4)), step(0.04045f, color));
        return color;
    }


}