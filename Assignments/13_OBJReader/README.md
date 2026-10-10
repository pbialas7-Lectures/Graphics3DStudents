# Reading Wavefront OBJ files

In this assignment, we will add the possibility of loading the models from files
in [Wavefront OBJ](https://paulbourke.net/dataformats/obj/) format and
associated [Wavefront Material Template Library (MTL)](https://paulbourke.net/dataformats/mtl/) files.

Start by copying the `12_oa_Textures` assignment (not `12_ob_ComputeShader` or `12_oc_PostProcessing`, which are a side
branch) to a new `13_OBJReader` directory, as described in the
[Preparing the assignments](../README.md#preparing-the-assignments) section.

## Owning OpenGL objects

So far every OpenGL object was created in `init` and simply lived until the end of the program; we did not even
bother to delete most of them. From now on objects will be created in other places, e.g. a material will load its
texture from a file, and it becomes important to know who _owns_ an object, i.e. who is responsible for deleting it.
Forgetting to delete an object leaks GPU memory; deleting it while something still uses it gives black textures or
errors.

C++ solves this with the RAII idiom (_resource acquisition is initialization_): the resource is owned by a C++ object
that releases it in its destructor. The `Application/gl_handle.h` header provides such owners for OpenGL objects:
`xe::gl::Texture`, `xe::gl::Buffer`, `xe::gl::VertexArray` and `xe::gl::Program`. Each holds one OpenGL name and calls
the matching `glDelete*` function when it is destroyed:

```c++
xe::gl::Texture texture;                                      // empty, holds no object
OGL_CALL(glCreateTextures(GL_TEXTURE_2D, 1, texture.put()));  // put() gives a GLuint* to store the new name in
OGL_CALL(glBindTextureUnit(0, texture.get()));                // get() returns the name
if (texture) { ... }                                          // true if it holds an object
```

A handle cannot be copied, as then two owners would delete the same object. It can only be _moved_, which transfers
the ownership and leaves the source empty:

```c++
xe::gl::Texture other = std::move(texture);   // now `other` owns the texture and `texture` is empty
```

The object is deleted when its last owner is destroyed, so the owner must live as long as the object is used, and it
must be destroyed while the OpenGL context still exists. Fields of `SimpleShapeApplication` and of the materials
satisfy both: they are destroyed when the application or the material is, before the window and its context.

To see why, follow what happens to `app` in `main`:

1. `app.run()` ends by calling `cleanup()`, which deletes all the `RegisteredObject`s, i.e. the meshes and the
   materials together with their fields.
2. At the end of `main` the `app` object is destroyed. In C++ an object of a derived class is destroyed in the
   reverse order of its construction: first the body of the `~SimpleShapeApplication()` destructor runs, then the
   fields of `SimpleShapeApplication` are destroyed (in the reverse order of their declaration), so a `gl::` handle
   that is a field of the application deletes its object here.
3. Only then does the destructor of the base class, `~Application()`, run, and it destroys the window and its
   OpenGL context.

The base class is constructed first and destroyed last, so the context created in the `Application` constructor
outlives every field of the derived class. This holds only for the fields of the application object itself. An
owner with static storage duration, e.g. a global variable or a `static` field, is destroyed after `main` returns,
when the context no longer exists, so do not store a `gl::` handle in one.

1. Start with the transformations uniform buffer: change the type of the `u_trans_buffer_handle_` field in `app.h`
   from `GLuint` to `xe::gl::Buffer` (include `Application/gl_handle.h`). Create the buffer with
   `glCreateBuffers(1, u_trans_buffer_handle_.put())` and use `u_trans_buffer_handle_.get()` wherever the name is
   passed to OpenGL. The buffer, which was never deleted so far, is now deleted automatically.

## Loading textures

1. Add a new header file `texture.h` in the `Engine` directory and declare `create_texture` function in the `xe`
   namespace:
   ```c++
   #pragma once

   #include <string>

   #include "glad/gl.h"

   #include "Application/gl_handle.h"

   namespace xe {
       gl::Texture create_texture(const std::string &name, bool is_sRGB = true);
   }
   ```

2. Add the definition of this function in the `texture.cpp` file. This function should take the name of the texture file
   and return a `gl::Texture` owning the new texture: create it with `glCreateTextures(GL_TEXTURE_2D, 1,
   texture.put())` and end with `return texture;`.

   Use the code creating the texture that was previously in the `app.cpp` file; `texture.cpp` has to include the
   `stb/stb_image.h`, `spdlog/spdlog.h` and `Application/utils.h` (for `OGL_CALL`) headers.

   As before, choose the formats depending on the number of channels of the image: the format of the data is
   `GL_RGB` for three and `GL_RGBA` for four channels. If `is_sRGB` is `true`, the internal format should be
   `GL_SRGB8` or `GL_SRGB8_ALPHA8`, otherwise `GL_RGB8` or `GL_RGBA8`.

   Remember to free the image with `stbi_image_free` after loading it into the texture.

   The function will load any image, so the rows of the data need not start at multiples of four bytes. Set
   `GL_UNPACK_ALIGNMENT` to 1 before `glTextureSubImage2D`. This setting is global, not a property of the texture,
   so read the previous value with `glGetIntegerv(GL_UNPACK_ALIGNMENT, ...)` before and restore it afterwards, so
   that the function does not change the state for the rest of the program.

   The `Engine` library collects its source files using `file(GLOB ...)`, so after creating `texture.cpp` you have to
   re-run CMake, as you did after creating `KdMaterial.cpp`.

3. This time also create the mipmaps. A mipmap is a sequence of smaller and smaller copies of the texture, each half
   the size of the previous one, with every texel the average of four texels of the larger copy. When the texture is
   seen from far away, so that many texels fall into one pixel, the sampler reads the copy of the matching size
   instead of picking a few texels of the full texture. Without mipmaps such textures shimmer and show moiré patterns
   when the camera moves.

   Each copy is a separate mipmap level of the texture, and immutable storage must have all the levels allocated
   up front: the second argument of `glTextureStorage2D` is their number. So far we have passed one, and with one
   level there is no room for the copies. A full chain halves the larger dimension until it reaches one texel, so it
   has
   ```c++
   GLsizei levels = 1 + static_cast<GLsizei>(std::floor(std::log2(std::max(width, height))));
   ```
   levels, e.g. 11 for a 1024x1024 image (include `<algorithm>` and `<cmath>`). Pass this number to
   `glTextureStorage2D`; `glTextureSubImage2D` still loads the image into level zero.

   After `glTextureSubImage2D` call `glGenerateTextureMipmap(texture.get())`, which computes the copies in all the
   other levels, and set `GL_TEXTURE_MIN_FILTER` to `GL_LINEAR_MIPMAP_LINEAR`, which interpolates within and between
   the two nearest copies. `GL_TEXTURE_MAG_FILTER` stays `GL_LINEAR`: mipmaps are only used when the texture is
   shrunk. To see the difference, zoom out the pyramid with and without the mipmaps; it is even more visible on
   the Earth, which you will load at the end of the next section.

   If you forget to allocate the levels, `glGenerateTextureMipmap` has nothing to fill, and there are no mipmaps,
   even though OpenGL does not report any error.

4. If the image cannot be loaded, or it has neither three nor four channels, do not exit the program as before, but
   print an error message and return an empty handle, `return {};`. In the second case free the image first. The
   callers can check for it with `if (texture)` (see the `create_from_mtl` code in the next section).

5. In the `app.cpp` file, use this newly defined function to load the texture. The `KdMaterial` constructor takes a
   plain `GLuint`, so pass it `texture.get()`; the material only uses the texture, it does not own it. The texture
   must therefore be owned by something that lives as long as the material: change the type of the `texture_` field
   of `SimpleShapeApplication` from `GLuint` to `xe::gl::Texture`, and remove the `glDeleteTextures` call from
   `cleanup`, as the handle now deletes the texture itself. If you kept it in a local variable of `init`, it would be
   deleted at the end of `init`, and the material would bind a deleted texture: `glBindTextureUnit` would report
   `GL_INVALID_OPERATION`, or, if the name was reused by a texture created later, the pyramid would show that texture.

## Materials from MTL files

1. In class `KdMaterial` add a new _factory_ method that will create a material object from the MTL description. This
   method must be `static`:
   ```c++
   static Material *create_from_mtl(const mtl_material_t &mat, std::string mtl_dir);
   ```
   You will need to include `ObjectReader/sMesh.h` file where the `mtl_material_t` is defined.
   Also add a `void set_texture(GLuint texture)` method to the `KdMaterial` class that sets the `texture_` field, and a
   field `gl::Texture map_Kd_texture_;` (see below), for which `KdMaterial.h` has to include `Application/gl_handle.h`.

2. Add the definition of this function
   ```c++
   Material *KdMaterial::create_from_mtl(const mtl_material_t &mat, std::string mtl_dir) {
       glm::vec4 color = get_color(mat.diffuse);
       color = glm::vec4(xe::srgb_inverse_gamma_correction(glm::vec3(color)), color.a);
       SPDLOG_DEBUG("Adding KdMaterial {}", glm::to_string(color));
       auto material = new xe::KdMaterial(color);
       if (!mat.diffuse_texname.empty()) {
           auto texture = xe::create_texture(mtl_dir + "/" + mat.diffuse_texname, true);
           SPDLOG_DEBUG("Adding Texture {} {:1d}", mat.diffuse_texname, texture.get());
           if (texture) {
               material->set_texture(texture.get());
               material->map_Kd_texture_ = std::move(texture);
           }
       }

       return material;
   }
   ```
   The `get_color` and `srgb_inverse_gamma_correction` functions are declared in `Engine/utils.h` and `create_texture`
   in `Engine/texture.h`, so include both headers in `KdMaterial.cpp`.
   `glm::to_string` requires defining `GLM_ENABLE_EXPERIMENTAL` before including the `glm/gtx/string_cast.hpp` header.

   A texture passed to the constructor or to `set_texture` belongs to the caller, as the texture of the pyramid
   belongs to `SimpleShapeApplication`. But the texture created here has no other owner: only the material knows
   about it. So the material takes the ownership: `std::move` transfers it from the local variable `texture` to the
   `map_Kd_texture_` field. The texture is then deleted together with the material. Materials are
   `RegisteredObject`s, deleted in `Application::cleanup()` while the OpenGL context still exists, so no destructor
   has to be written.

   This function assumes that the textures are in sRGB space, and so are the colors in MTL files, which are usually
   picked in a color picker. Our shader works with linear colors and gamma-corrects the result, so the `Kd` color has to
   be converted to linear space using `srgb_inverse_gamma_correction`; otherwise it would be gamma-corrected twice and
   look too light.

   The MTL format itself does not say in which color space the colors are given. Treating them as sRGB, as they are
   shown in a color picker, is the convention used in this course, but some programs, e.g. Blender, write and read them
   as linear values. Our `blue_marble.mtl` was exported from Blender, but its `Kd` color is white, which is the same
   in both spaces.

   In the `KdMaterial::init` function add the following code (the `add_mat_function` function is declared in
   `Engine/mesh_loader.h`, so include it in `KdMaterial.cpp`):
   ```c++
   xe::add_mat_function("KdMaterial", KdMaterial::create_from_mtl);
   ```
   that will register this function as a factory method for the `KdMaterial` class. The OBJ loader uses the registered
   factories to create the materials, so `KdMaterial::init()` has to be called __before__ loading any meshes.

   Which factory is used for a given material is decided by its `illum` statement in the MTL file, see
   `Engine/mesh_loader.cpp`: `illum 0` selects `KdMaterial`. All the materials used by `pyramid.obj` and `blue_marble.obj`
   use `illum 0`. If the factory selected by `illum` is not registered, or the `illum` value is unknown, an error is
   printed and the material is replaced by a gray `KdMaterial`, with a warning. Only when `KdMaterial` itself is not
   registered does the submesh get the `NullMaterial`, which draws it with whatever program and buffers the previously
   drawn material left bound.

3. Replace all the code creating the pyramid mesh and material by
   ```c++
   auto pyramid = xe::load_mesh_from_obj(std::string(ROOT_DIR) + "/Models/pyramid.obj",
                                         std::string(ROOT_DIR) + "/Models");
   if (!pyramid) {
       SPDLOG_CRITICAL("Cannot load the pyramid model");
       exit(-1);
   }
   add_mesh(pyramid);
   ```
   The `load_mesh_from_obj` function is declared in `Engine/mesh_loader.h`, include it in `app.cpp`. It returns
   `nullptr` when the model cannot be read, e.g. because of a wrong path, hence the check.
   You should again see the textured pyramid. The texture is now loaded and owned by the material, so you can remove
   the `texture_` field from `SimpleShapeApplication`, and the `cleanup` override, which now only calls
   `Application::cleanup()`.

4. Finally, load the `Models/blue_marble.obj` model instead of the pyramid.
   You should see the Earth model with the texture.

   The loader does not share vertices between triangles, so each triangle adds three vertices, and it stores the
   indices as 16-bit numbers. So it can only load models with at most 65536 vertices, i.e. 21845 triangles, and reports
   an error for larger ones. This is enough for the models used in this course.
