# Processing textures with a compute shader

So far all our shaders were parts of the rendering pipeline: the vertex shader is invoked for every vertex and the
fragment shader for every fragment. OpenGL also provides _compute shaders_, which are not part of this pipeline at all.
A compute shader is simply a function that is executed on the GPU a given number of times, in parallel, and that can
read and write textures and buffers. This makes it a convenient tool for processing images.

In this assignment, we will use a compute shader to convert the texture from the previous assignment to grayscale
before using it on the pyramid. The conversion is done pixel by pixel: each pixel of the output depends only on the
same pixel of the input, so each pixel can be processed by a separate invocation of the shader.

Start by copying the `12_oa_Textures` assignment to a new `12_ob_ComputeShader` directory, as described in the
[Preparing the assignments](../README.md#preparing-the-assignments) section.

## Work groups and invocations

A compute shader is executed in _work groups_. The size of a single work group is declared in the shader, e.g.

```glsl
layout(local_size_x = 16, local_size_y = 16) in;
```

declares that each work group consists of 16x16 = 256 invocations. The number of work groups is given when we launch
(_dispatch_) the shader from C++

```c++
OGL_CALL(glDispatchCompute(n_groups_x, n_groups_y, 1));
```

so altogether the shader is invoked `16*n_groups_x` times in the x direction and `16*n_groups_y` times in the y
direction. Each invocation can find out which one it is from the built-in variable `gl_GlobalInvocationID`; for a
two-dimensional dispatch its `x` and `y` components are just the coordinates of the pixel that this invocation should
process.

The number of invocations has to be a multiple of the work group size, but the image size need not be. So we have to
round the number of groups up, and the invocations that fall outside the image must not do anything.

## The output texture

The compute shader will read the original texture and write the result to a second texture. We cannot write the
result back into the original texture, because it has an sRGB internal format (`GL_SRGB8`, or `GL_SRGB8_ALPHA8` for
images with four channels), and sRGB formats cannot be used for writing from shaders.

1. In the `init` method, after creating the original texture, create the output texture
   ```c++
   GLuint processed_tex_handle;
   OGL_CALL(glCreateTextures(GL_TEXTURE_2D, 1, &processed_tex_handle));
   OGL_CALL(glTextureStorage2D(processed_tex_handle, 1, GL_RGBA16F, width, height));
   OGL_CALL(glTextureParameteri(processed_tex_handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
   OGL_CALL(glTextureParameteri(processed_tex_handle, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
   ```
   This is done in the same way as for the original texture, except that we do not load any data with
   `glTextureSubImage2D`: the contents will be written by the compute shader.

   Immutable storage, allocated by `glTextureStorage2D`, is the recommended way to create textures that shaders write
   to: the size and format cannot change while the texture is bound to an image unit, and a texture with all its levels
   allocated is always complete. (Desktop OpenGL also allows writing to textures created with `glTexImage2D`, but
   OpenGL ES requires immutable storage.)

   We choose the `GL_RGBA16F` format (four 16-bit floating point channels) because the compute shader will write
   _linear_ color values. Storing linear values in 8 bits per channel would lose precision in dark colors, which you
   would see as banding. The format has four channels even if the image has only three, because the formats that can
   be used for writing from shaders have one, two or four channels of the same size. The only three-channel exception
   is `GL_R11F_G11F_B10F`, with less precision than `GL_RGBA16F`.

## The compute shader

1. The `12_oa_Textures` assignment uses only the shaders from the `Engine` directory. Create the `shaders` directory in
   your assignment directory and in it the file `example_cs.glsl` with the following content
   ```glsl
   #version 450 core

   layout(local_size_x = 16, local_size_y = 16) in;

   layout(binding = 0) uniform sampler2D input_texture;
   layout(binding = 0, rgba16f) uniform writeonly image2D output_image;

   void main() {
       ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
       ivec2 size = imageSize(output_image);
       if (pixel.x >= size.x || pixel.y >= size.y) {
           return;
       }

       vec4 color = texelFetch(input_texture, pixel, 0);

       imageStore(output_image, pixel, color);
   }
   ```
   The input is read through a sampler, just as in the fragment shader, but using the `texelFetch` function, which
   takes integer pixel coordinates instead of texture coordinates and does no filtering. The last argument is the
   mipmap level. Because the input texture has the sRGB format, the sampler converts the values to the linear color
   space for us.

   The output is an _image_, not a sampler. Images are bound to _image units_, which are different from texture units,
   so both can use binding zero. The `rgba16f` qualifier must match the format given when binding the texture to the
   image unit (see below), which in turn must be compatible with the format of the texture. The `imageStore`
   function writes the value to the given pixel; no conversion is done, so we write linear values.

   The `layout(binding = ...)` qualifier assigns the units directly in the shader, so we do not need the
   `glGetUniformLocation` and `glUniform1i` calls used in the previous assignment.

   For the moment, the shader just copies the input to the output.

2. In the `init` method, create the program from this shader
   ```c++
   auto compute_program = xe::utils::create_program(
           {{GL_COMPUTE_SHADER, std::string(PROJECT_DIR) + "/shaders/example_cs.glsl"}});
   if (!compute_program) {
       SPDLOG_CRITICAL("Invalid compute program");
       exit(-1);
   }
   ```
   A compute program contains only the compute stage; it cannot be combined with any other shader stage, such as a
   vertex or a fragment shader.

## Running the shader

1. Still in the `init` method, run the shader:
    - make `compute_program` the current program using `glUseProgram`,
    - bind the original texture to texture unit zero using `glBindTextureUnit(0, tex_handle)`,
    - bind the output texture to image unit zero
      ```c++
      OGL_CALL(glBindImageTexture(0, processed_tex_handle, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F));
      ```
      The arguments are: the image unit, the texture, the mipmap level, two arguments used only for array textures,
      the access, and the format, which again must match the format of the texture,
    - dispatch the shader with `glDispatchCompute`, computing the number of work groups in each direction as `width`
      (respectively `height`) divided by 16 and rounded __up__,
    - unbind the texture and the image and set the current program to zero.

   As we will not use the compute program anymore, delete it with `glDeleteProgram`. The original texture is not
   needed anymore either, only the processed one, so delete it with `glDeleteTextures`.

2. Commands issued to OpenGL are executed in order, but the writes done by shaders to images are an exception: OpenGL
   does not guarantee that they are visible to the commands issued later. We have to request this explicitly with a
   _memory barrier_. As the output texture will be read through a sampler in the fragment shader, issue
   ```c++
   OGL_CALL(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
   ```
   after the `glDispatchCompute` call. Without it the program may still work on your computer, which is what makes
   such errors hard to find.

3. Finally, pass `processed_tex_handle` instead of `tex_handle` to the `KdMaterial` constructor, and store it in the
   `texture_` field, so that `cleanup` deletes it instead of the original texture. You should see the
   same pyramid as at the end of the previous assignment. If it is black, check that every OpenGL call is wrapped in
   `OGL_CALL`, that the formats in the shader, `glTextureStorage2D` and `glBindImageTexture` agree, and that the units
   passed to `glBindTextureUnit` and `glBindImageTexture` are the same as the `binding` values in the shader. OpenGL
   does not report an error when they differ: the shader then simply reads and writes other units, and the output
   texture is never written.

   Because the shader writes linear values, and the fragment shader converts them to sRGB with
   the `srgb_gamma_correction` function, the colors are correct. If you removed this function, the pyramid would look
   too dark, as at the beginning of the gamma correction section of the previous assignment.

4. To check that the bounds and work groups are right, temporarily change the shader to process only the left half
   of the image: write the input color for `pixel.x < size.x / 2` and, for example, red `vec4(1, 0, 0, 1)` otherwise.
   The boundary should be a straight vertical line through the middle of the texture. Then revert the change.

## Grayscale

The perceived brightness of a color is not the average of its components: we are most sensitive to green and least
to blue. The _relative luminance_ of a color is

```glsl
float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
```

These coefficients apply to linear values, which is what the compute shader gets from the sRGB texture.

1. Modify the compute shader so that it writes `vec4(luminance, luminance, luminance, color.a)`. The pyramid should
   now be gray. Rotate the pyramid to compare the faces: the green face is much lighter than the blue one.

## Negative

We will keep both conversions in the shader and choose between them with the preprocessor, which works in GLSL as in
C++.

1. Add the line
   ```glsl
   //#define NEGATIVE
   ```
   after the `#version` directive and enclose the grayscale code in `#ifdef NEGATIVE ... #else ... #endif`.

   The negative should invert the colors as we see them, like an image editor does, so it has to be computed in sRGB
   space. The sampler returns linear values, and inverting them directly would e.g. turn a mid gray (sRGB 0.5, linear
   0.21) into linear 0.79, which is displayed as a light gray of sRGB 0.9. Add the functions converting between the
   two spaces before `main` (the first one is the `srgb_gamma_correction` from the `Kd` shader, without the `clamp`):
   ```glsl
   vec3 linear_to_srgb(vec3 c) {
       return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
   }

   vec3 srgb_to_linear(vec3 c) {
       return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
   }
   ```
   In the `#ifdef NEGATIVE` branch write the negative of the image, converting the color to sRGB, inverting it and
   converting the result back to linear:
   ```glsl
   #ifdef NEGATIVE
       imageStore(output_image, pixel, vec4(srgb_to_linear(1.0 - linear_to_srgb(color.rgb)), color.a));
   #else
       float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
       imageStore(output_image, pixel, vec4(luminance, luminance, luminance, color.a));
   #endif
   ```

2. Uncomment the `#define NEGATIVE` line and run the program: the colors of the pyramid should be inverted. As the
   shader is read from the file when the program starts, you do not have to rebuild it, just run it again. Then
   comment the line out to get back to the grayscale conversion.
