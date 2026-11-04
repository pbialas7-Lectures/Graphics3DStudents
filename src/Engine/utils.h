//
// Created by pbialas on 10.12.22.
//

#pragma once

#include <string>

#include "glad/gl.h"
#include "glm/glm.hpp"

namespace xe {
    void check_and_use_program(GLuint program);

    glm::vec4 get_color(const float c[3]);

    glm::vec3 srgb_inverse_gamma_correction(glm::vec3 color);

    // Path of a texture named in an MTL file, which gives it relative to the directory of the MTL file. Backslashes
    // are taken as directory separators, since MTL files written on Windows use them. An absolute name is used as it
    // is. If the file does not exist there (e.g. an absolute Windows path from another machine), the file name alone
    // is looked for in mtl_dir. If it is not found either, the first path is returned, for the error message.
    std::string texture_path(const std::string &mtl_dir, const std::string &name);
}