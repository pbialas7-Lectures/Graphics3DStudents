# Post-processing

So far everything we drew went straight to the window. Many effects, however, need the whole rendered image before
they can be applied: blurring, edge detection, color grading, tone mapping and so on. Each output pixel of such an
effect depends on several pixels of the image, and a fragment shader drawing the scene cannot read the pixels
written by the other fragments. The solution is to render the scene into a texture first and then process this
texture in a second pass. This is called _render-to-texture_, and the second pass _post-processing_.

In this assignment we will render the pyramid into a texture, detect the edges in the rendered image and display the
result. We will do the processing in two ways: with a fragment shader, and with a compute shader as in the previous
assignment.

Start by copying the `12_ob_ComputeShader` assignment to a new `12_oc_PostProcessing` directory, as described in the
[Preparing the assignments](../README.md#preparing-the-assignments) section.

## Simulating color blindness

Before we start, we will give the pyramid its colors back. Instead of the grayscale conversion, the compute shader from
the previous assignment will show the texture as seen by a person with _deuteranopia_, the most common form of
red-green color blindness. People with deuteranopia lack the cones sensitive to medium wavelengths (green), so they
cannot tell apart colors that differ only in their red and green components.

[Machado, Oliveira and Fernandes (2009)](https://doi.org/10.1109/TVCG.2009.113)
derived a 3x3 matrix that simulates it. It is multiplied with the _linear_ RGB color, which is exactly what our compute
shader reads from the sRGB texture. GLSL matrices are constructed column by column, so the columns of the matrix are
written one per line:

```glsl
const mat3 DEUTERANOPIA = mat3(
     0.367322,  0.280085, -0.011820,
     0.860646,  0.672501,  0.042940,
    -0.227968,  0.047413,  0.968881);
```

1. Add this constant to `example_cs.glsl`.

2. Keep the previous conversions, but make the simulation the default:
   ```glsl
   #if defined(NEGATIVE)
       imageStore(output_image, pixel, vec4(srgb_to_linear(1.0 - linear_to_srgb(color.rgb)), color.a));
   #elif defined(GRAYSCALE)
       float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
       imageStore(output_image, pixel, vec4(luminance, luminance, luminance, color.a));
   #else
       imageStore(output_image, pixel, vec4(clamp(DEUTERANOPIA * color.rgb, 0.0, 1.0), color.a));
   #endif
   ```
   and add a commented-out `//#define GRAYSCALE` line next to `//#define NEGATIVE`. The result can fall slightly
   outside of [0, 1], e.g. pure red gets a small negative blue component, hence the `clamp`.

3. Run the program and rotate the pyramid. The red side becomes dark olive and the green side light yellow: both now
   have the same hue and differ mainly in brightness. The orange side is yellow too, while the blue side and the gray
   base hardly change. Each row of the matrix sums to one, so grays stay gray.

4. To see why the matrix must be applied to linear values, temporarily apply it to the gamma corrected color: convert
   `color.rgb` to sRGB first, multiply, and convert back, using the `linear_to_srgb` and `srgb_to_linear` functions
   that are already in the shader
   ```glsl
   vec3 srgb = clamp(DEUTERANOPIA * linear_to_srgb(color.rgb), 0.0, 1.0);
   imageStore(output_image, pixel, vec4(srgb_to_linear(srgb), color.a));
   ```
   The `clamp` is needed here before the conversion, as `srgb_to_linear` raises its argument to a power, which is
   undefined for negative values.
   The colors change noticeably: the red side becomes much darker and the green side more orange. A matrix is a
   linear operation, so it models the physics correctly only on values proportional to the light intensity. Then
   revert the change.

## Framebuffer objects

All drawing goes to the currently bound _framebuffer_. Until now this was the _default framebuffer_, the one created
together with the window. We can also create our own _framebuffer objects_ (FBOs). A framebuffer object has no memory
of its own: it is a set of _attachments_, the images that the color and depth values are written to. An attachment can
be

- a _texture_, which can later be read in shaders like any other texture, or
- a _renderbuffer_, an image that can only be used as an attachment. Renderbuffers are meant for the data that we need
  while rendering but never read in shaders.

We will read the colors in the post-processing pass, so we attach a texture as the color buffer. The depth buffer is
needed for the depth test while drawing the scene, but never read afterwards, so a renderbuffer is enough.

1. Add the fields `GLuint fbo_`, `GLuint scene_color_` and `GLuint scene_depth_` to `SimpleShapeApplication`, all
   initialized to zero, and a method `void create_framebuffer(int w, int h)`. In it create the color texture
   ```c++
   OGL_CALL(glCreateTextures(GL_TEXTURE_2D, 1, &scene_color_));
   OGL_CALL(glTextureStorage2D(scene_color_, 1, GL_SRGB8_ALPHA8, w, h));
   OGL_CALL(glTextureParameteri(scene_color_, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
   OGL_CALL(glTextureParameteri(scene_color_, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
   ```
   the depth renderbuffer
   ```c++
   OGL_CALL(glCreateRenderbuffers(1, &scene_depth_));
   OGL_CALL(glNamedRenderbufferStorage(scene_depth_, GL_DEPTH_COMPONENT24, w, h));
   ```
   and the framebuffer object with both attachments
   ```c++
   OGL_CALL(glCreateFramebuffers(1, &fbo_));
   OGL_CALL(glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, scene_color_, 0));
   OGL_CALL(glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, scene_depth_));
   ```
   The last argument of `glNamedFramebufferTexture` is the mipmap level we render into.

   The texture has the same size as the window, so each pixel of the window has exactly one texel. That is why we use
   `GL_NEAREST` filtering: there is nothing to interpolate.

2. Not every combination of attachments can be rendered into. Check it after creating the framebuffer:
   ```c++
   GLenum status;
   OGL_CALL(status = glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER));
   if (status != GL_FRAMEBUFFER_COMPLETE) {
       SPDLOG_CRITICAL("Framebuffer is not complete: {:#x}", status);
       exit(-1);
   }
   ```

3. Call `create_framebuffer` in `init` with the size returned by `frame_buffer_size()`.

### Why an sRGB texture?

The `KdMaterial` fragment shader ends with the `srgb_gamma_correction` function, so it writes colors that are already
gamma corrected, ready to be displayed. The post-processing, on the other hand, should work on _linear_ values, like the
compute shader in the previous assignment. We get both by storing the image in a texture with the `GL_SRGB8_ALPHA8`
format:

- when writing, the values are stored unchanged. OpenGL would convert linear values to sRGB only if we enabled
  `GL_FRAMEBUFFER_SRGB`, which we do not,
- when reading through a sampler, the values are converted from sRGB to linear, as for any sRGB texture.

So the post-processing shaders get linear colors without any change to the `Engine` shaders. They must apply the gamma
correction again before writing to the window. Storing gamma corrected values in eight bits also keeps the dark colors
precise, which is the reason for using sRGB in the first place.

## Rendering into the texture

1. In `frame`, bind the framebuffer object before drawing the meshes, and clear it:
   ```c++
   OGL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbo_));
   OGL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
   ```
   `Application` clears only the default framebuffer before calling `frame`; our framebuffer has to be cleared by us.
   After drawing the meshes bind the default framebuffer again with `glBindFramebuffer(GL_FRAMEBUFFER, 0)`.

   The window should now show only the clear color: everything was drawn into the texture.

2. To check that the texture contains the scene, copy it to the window:
   ```c++
   auto [w, h] = frame_buffer_size();
   OGL_CALL(glBlitNamedFramebuffer(fbo_, 0, 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST));
   ```
   The second argument, zero, is the default framebuffer. You should see the same pyramid as in the previous
   assignment. A blit copies the values unchanged (again, because `GL_FRAMEBUFFER_SRGB` is disabled), so the colors are
   the same too. (Some older drivers do convert the sRGB values during a blit anyway; if the image looks too dark, do
   not worry, the next section replaces the blit.)

   This is also a good moment to look at the frame in RenderDoc (Ctrl-F): the texture viewer shows the color
   attachment, and the pipeline state of the draw calls shows which framebuffer they wrote to.

## Window resize

The attachments have the size of the window, so they must change together with it. Textures created with
`glTextureStorage2D` have immutable storage: their size cannot change, so we have to create new ones.

1. Add a method `void delete_framebuffer()` that deletes the framebuffer, the texture and the renderbuffer with
   `glDeleteFramebuffers`, `glDeleteTextures` and `glDeleteRenderbuffers` and sets the handles back to zero.

2. In `framebuffer_resize_callback`, after the check for the zero size, call `delete_framebuffer()` and
   `create_framebuffer(w, h)`. Resize the window and check that the image is not stretched or cut.

3. In the `cleanup` method call `delete_framebuffer()`, before `Application::cleanup()`: OpenGL objects must be
   deleted while the context still exists.

## Displaying the texture

Instead of the blit we will now draw the texture with our own shaders. To run a fragment shader for every pixel of
the window, it is enough to draw a single triangle that covers the whole screen. We do not even need a vertex buffer:
the vertex shader can compute the positions from the index of the vertex.

1. Create `shaders/fullscreen_vs.glsl`:
   ```glsl
   #version 450 core

   void main() {
       // The vertices (-1,-1), (3,-1) and (-1,3) make a triangle covering the whole [-1,1]x[-1,1] square.
       vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
       gl_Position = vec4(position, 0.0, 1.0);
   }
   ```
   Check on paper that `gl_VertexID` equal to 0, 1 and 2 gives the three vertices from the comment. The triangle is
   counterclockwise, so back-face culling does not remove it.

2. Create `shaders/display_fs.glsl`:
   ```glsl
   #version 450 core

   layout(location = 0) out vec4 vFragColor;

   layout(binding = 0) uniform sampler2D image;

   vec3 srgb_gamma_correction(vec3 color) {
       color = clamp(color, 0.0, 1.0);
       color = mix(color * 12.92, (1.055 * pow(color, vec3(1.0 / 2.4))) - 0.055, step(0.0031308, color));
       return color;
   }

   void main() {
       vec4 color = texelFetch(image, ivec2(gl_FragCoord.xy), 0);
       vFragColor = vec4(srgb_gamma_correction(color.rgb), color.a);
   }
   ```
   `gl_FragCoord.xy` are the window coordinates of the pixel center, e.g. `(0.5, 0.5)` for the lower-left pixel, so
   converting them to `ivec2` gives the texel of the same pixel.

3. In `init` create the program from these two shaders with `xe::utils::create_program`, storing it in a field
   `GLuint display_program_ = 0u;`, and an empty vertex array object with `glCreateVertexArrays`, in a field
   `GLuint display_vao_ = 0u;`. The triangle has no attributes, but drawing still requires a bound VAO.

4. In `frame`, replace the blit with drawing the triangle: use the program, bind `scene_color_` to texture unit 0 with
   `glBindTextureUnit`, bind the empty VAO and call `glDrawArrays(GL_TRIANGLES, 0, 3)`. Unbind everything afterwards.
   The image should look exactly as before. Delete the program and the VAO in `cleanup`.
   Keep the `display_fs.glsl` shader: the compute path will need it again.

## Edge detection in the fragment shader

An _edge_ in an image is a place where the brightness changes abruptly, e.g. at the border of the pyramid against the
background or between two differently colored parts of the texture. To find edges we compute, for every pixel, how
fast the brightness changes in the horizontal and in the vertical direction, i.e. the _gradient_ of the brightness.
Where the gradient is large, there is an edge.

This is exactly the kind of operation that cannot be done while drawing the scene: each output pixel needs the values
of its neighbours.

### Why luminance?

A gradient is defined for a function with a single value at each point, but a pixel has three: red, green and blue. We
could compute the gradient of each channel separately and combine the three results, but it is simpler and cheaper to
first reduce each color to a single number describing its brightness. We use the relative luminance from the previous
assignment,

```glsl
float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
```

because it measures brightness the way we perceive it. The price is that an edge between two colors of the same
luminance, e.g. a particular red and green, is not detected at all.

### The Sobel operator

The simplest estimate of the horizontal gradient is the difference between the right and the left neighbour of the
pixel. The [Sobel operator](https://en.wikipedia.org/wiki/Sobel_operator) also uses the pixels just above and below
them, with half the weight, which makes the result less sensitive to noise. It is usually written as two 3x3 tables of
weights, one for each direction, laid over the 3x3 neighbourhood of the pixel:

```
       horizontal (gx)            vertical (gy)

     -1     0    +1             +1    +2    +1       row y+1 (above)
     -2     0    +2              0     0     0       row y
     -1     0    +1             -1    -2    -1       row y-1 (below)

    x-1     x   x+1            x-1     x   x+1
```

Each weight multiplies the luminance of the pixel under it, and the products are summed. Writing `l(i,j)` for the
luminance of the pixel shifted by `i` columns and `j` rows from the current pixel `(x, y)`, i.e. of the pixel
`(x+i, y+j)`, the tables give

```
gx = (l(1,-1) + 2 l(1,0) + l(1,1)) - (l(-1,-1) + 2 l(-1,0) + l(-1,1))
gy = (l(-1,1) + 2 l(0,1) + l(1,1)) - (l(-1,-1) + 2 l(0,-1) + l(1,-1))
```

`gx` is the right column minus the left column, `gy` the upper row minus the lower row. The window coordinates grow to
the right and up, with the origin in the lower-left corner. In a uniform region both are zero, at a vertical edge `gx`
is large, at a horizontal edge `gy`, and the length of the vector `(gx, gy)` measures the strength of the edge in any
direction. As the luminance is between 0 and 1, `gx` and `gy` are between -4 and 4.

1. Create `shaders/sobel_fs.glsl`, a copy of `display_fs.glsl` with the function
   ```glsl
   // The luminance of the pixel with the window coordinates `pixel`.
   float l(ivec2 pixel) {
       ivec2 size = textureSize(image, 0);
       vec3 color = texelFetch(image, clamp(pixel, ivec2(0), size - 1), 0).rgb;
       return dot(color, vec3(0.2126, 0.7152, 0.0722));
   }
   ```
   `texelFetch` outside of the texture returns undefined values, hence the `clamp`: at the border of the window the
   border pixels are repeated.

2. In `main` compute the gradient. With `ivec2 p = ivec2(gl_FragCoord.xy);` being the current pixel, the two
   components are
   ```glsl
   // Right column minus left column.
   float gx = (l(p + ivec2(1, -1)) + 2.0 * l(p + ivec2(1, 0)) + l(p + ivec2(1, 1)))
            - (l(p + ivec2(-1, -1)) + 2.0 * l(p + ivec2(-1, 0)) + l(p + ivec2(-1, 1)));
   // Upper row minus lower row.
   float gy = (l(p + ivec2(-1, 1)) + 2.0 * l(p + ivec2(0, 1)) + l(p + ivec2(1, 1)))
            - (l(p + ivec2(-1, -1)) + 2.0 * l(p + ivec2(0, -1)) + l(p + ivec2(1, -1)));
   ```
   Compare each term with the tables above.

3. Darken the pixel according to the strength of the edge:
   ```glsl
   float edge = clamp(length(vec2(gx, gy)), 0.0, 1.0);
   vFragColor = vec4(srgb_gamma_correction((1.0 - edge) * color.rgb), color.a);
   ```
   where `color` is the color of the pixel itself, read with `texelFetch` as in `display_fs.glsl`. Thanks to the
   `clamp`, a step in luminance of about 0.25 or more already gives a fully black outline.

   Use this shader instead of `display_fs.glsl`: create the program stored in `display_program_` from
   `fullscreen_vs.glsl` and `sobel_fs.glsl` (the field will be renamed in the compute shader section). The pyramid
   should get dark outlines along its silhouette, along the edges between its faces, and around the colored triangles
   and black lines of the texture.

   The result should look like this:

   <p align="center"><img alt="Pyramid with dark outlines from the Sobel edge detection" src="edges.png" width="50%"></p>

4. To see the gradient itself, temporarily replace the output with `vFragColor = vec4(edge, edge, edge, 1.0);`. The
   edge strength is not a color, so it is written without the gamma correction. The edges are white on a black
   background. On the blue side the lines are only gray: blue has a low luminance, so the step to the black lines is
   smaller than 0.25. Then try `clamp(abs(gx), 0.0, 1.0)` and `clamp(abs(gy), 0.0, 1.0)` instead of `edge`: the first
   shows only the edges that are closer to vertical, the second only those closer to horizontal. Then revert the
   change.

## Edge detection in a compute shader

The same edge detection can be done by a compute shader. Instead of drawing to the window, it writes the result into a
second texture, `processed_`. This texture is then displayed with `display_fs.glsl`, which only applies the gamma
correction. So the compute path consists of two steps: the dispatch of the compute shader, and the full-screen
triangle drawn with `display_fs.glsl`. We will keep both paths and switch between them with a key.

### The output texture

1. Add a field `GLuint processed_ = 0u;` and create the texture at the end of `create_framebuffer`, so that it has the
   size of the window and is recreated when the window is resized:
   ```c++
   OGL_CALL(glCreateTextures(GL_TEXTURE_2D, 1, &processed_));
   OGL_CALL(glTextureStorage2D(processed_, 1, GL_RGBA16F, w, h));
   OGL_CALL(glTextureParameteri(processed_, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
   OGL_CALL(glTextureParameteri(processed_, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
   ```
   Delete it in `delete_framebuffer` and set the handle back to zero.

   The format is different from `scene_color_` for two reasons explained in the previous assignment: shaders cannot
   write to textures with an sRGB format, and storing linear values in 8 bits would cause banding in dark colors. As
   with `scene_color_`, each texel corresponds to one pixel of the window, hence `GL_NEAREST`.

### The compute shader

The computation itself is the same as in `sobel_fs.glsl`, but a compute shader is not part of the rendering pipeline,
which changes how it gets its pixel and where it puts the result:

| | `sobel_fs.glsl` | `sobel_cs.glsl` |
|---|---|---|
| current pixel | `ivec2(gl_FragCoord.xy)` | `ivec2(gl_GlobalInvocationID.xy)` |
| pixels processed | exactly those covered by the triangle | whole work groups, possibly outside the image |
| result | written to `vFragColor` | written with `imageStore` to `output_image` |
| gamma correction | in the shader | none, done later by `display_fs.glsl` |

1. Create `shaders/sobel_cs.glsl`:
   ```glsl
   #version 450 core

   layout(local_size_x = 16, local_size_y = 16) in;

   layout(binding = 0) uniform sampler2D image;
   layout(binding = 0, rgba16f) uniform writeonly image2D output_image;

   // The l function from sobel_fs.glsl.

   void main() {
       ivec2 p = ivec2(gl_GlobalInvocationID.xy);
       ivec2 size = imageSize(output_image);
       if (p.x >= size.x || p.y >= size.y) {
           return;
       }

       vec4 color = texelFetch(image, p, 0);

       // gx, gy and edge computed exactly as in sobel_fs.glsl.

       imageStore(output_image, p, vec4((1.0 - edge) * color.rgb, color.a));
   }
   ```
   Copy the `l` function and the computation of `gx`, `gy` and `edge` from `sobel_fs.glsl`. Do not copy the
   `srgb_gamma_correction` function: the stored values must stay linear.

   The input is read through a sampler on texture unit 0, as in the fragment shader, and the output is written to
   image unit 0. Texture units and image units are separate, so both can use binding 0.

2. In `init` create the program, in a new field `GLuint sobel_cs_program_ = 0u;`:
   ```c++
   sobel_cs_program_ = xe::utils::create_program(
           {{GL_COMPUTE_SHADER, std::string(PROJECT_DIR) + "/shaders/sobel_cs.glsl"}});
   if (!sobel_cs_program_) {
       SPDLOG_CRITICAL("Invalid Sobel compute program");
       exit(-1);
   }
   ```
   Unlike in the previous assignment, we run it every frame, so do not delete it in `init` but in `cleanup`.

### Displaying the result

The program created in the fragment shader section now uses `sobel_fs.glsl`. For the compute path we need one more
program, made of `fullscreen_vs.glsl` and `display_fs.glsl`.

1. Rename the field `display_program_`, which now holds the program using `sobel_fs.glsl`, to `sobel_fs_program_`.
   Then add a new field `display_program_` and create in it the second program, from `fullscreen_vs.glsl` and
   `display_fs.glsl`. Delete both in `cleanup`. The empty VAO is shared by both.

2. Both paths draw the full-screen triangle, only with a different program and texture. Move the drawing code from
   `frame` to a method
   ```c++
   void SimpleShapeApplication::draw_full_screen(GLuint program, GLuint texture) {
       OGL_CALL(glUseProgram(program));
       OGL_CALL(glBindTextureUnit(0, texture));
       OGL_CALL(glBindVertexArray(display_vao_));
       OGL_CALL(glDrawArrays(GL_TRIANGLES, 0, 3));
       OGL_CALL(glBindVertexArray(0));
       OGL_CALL(glBindTextureUnit(0, 0));
       OGL_CALL(glUseProgram(0));
   }
   ```

### Running the compute path

1. Add a field `bool use_compute_ = false;`. In `frame`, after rendering the scene into the framebuffer object and
   binding the default framebuffer again, replace the drawing with
   ```c++
   if (use_compute_) {
       auto [w, h] = frame_buffer_size();
       OGL_CALL(glUseProgram(sobel_cs_program_));
       OGL_CALL(glBindTextureUnit(0, scene_color_));
       OGL_CALL(glBindImageTexture(0, processed_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F));
       OGL_CALL(glDispatchCompute((w + 15) / 16, (h + 15) / 16, 1));
       OGL_CALL(glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT));
       OGL_CALL(glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F));
       OGL_CALL(glBindTextureUnit(0, 0));
       OGL_CALL(glUseProgram(0));

       draw_full_screen(display_program_, processed_);
   } else {
       draw_full_screen(sobel_fs_program_, scene_color_);
   }
   ```
   The steps are the same as in the previous assignment, only now they run every frame and the image has the size of
   the window:
   - the number of work groups is rounded up, so that the whole window is covered when its size is not a multiple
     of 16. The invocations outside the window return immediately thanks to the check in the shader,
   - the memory barrier makes the values written with `imageStore` visible to the texture reads in `display_fs.glsl`,
   - we do not need a barrier _before_ the dispatch: writes done by drawing into a framebuffer are visible to the
     commands issued later. Barriers are needed only for writes done by shaders with `imageStore`, to shader storage buffers or with atomic
     counters.

2. Override `key_callback` to toggle the path with the `C` key:
   ```c++
   void SimpleShapeApplication::key_callback(int key, int scancode, int action, int mods) {
       Application::key_callback(key, scancode, action, mods);
       if (key == GLFW_KEY_C && action == GLFW_PRESS) {
           use_compute_ = !use_compute_;
           SPDLOG_INFO("Edge detection in the {} shader", use_compute_ ? "compute" : "fragment");
       }
   }
   ```
   The built-in shortcuts (Ctrl-Q, Ctrl-S, Ctrl-F) are handled before your method is called, so do not use them.

3. Run the program and press `C` a few times. Switching between the two paths should not visibly change the image.
   The colors may differ by one level out of 255 at most, because the compute path stores the intermediate result as
   16-bit floats, which are rounded. If you see a difference, compare the two shaders, and the formats and units in
   `glBindImageTexture` and in the shader. Resize the window in the compute mode too: the image must not be cut,
   which checks that `processed_` is recreated and that the number of work groups is computed from the new size.

## Checks

1. In RenderDoc, look at a frame of each path: not counting the draws of the ImGui "Info" window, the fragment path
   has two draw calls, the compute path a draw, a dispatch and a draw. Inspect `processed_` after the dispatch.

2. Temporarily remove the `clamp` from the `l` function. Depending on the driver the border of the window may
   show a dark or noisy frame. The result is undefined, so it can also look fine on your computer.

3. Temporarily remove the gamma correction from `display_fs.glsl` and switch to the compute path with `C`
   (`display_fs.glsl` is not used in the fragment path). The image becomes too dark, which shows that the processing
   works on linear values.

## Extension (optional)

1. Add other effects, e.g. a box or Gaussian blur, a vignette, or the grayscale and negative from the previous
   assignment, and switch between them with keys.

2. A blur reads the same pixels many times. In the compute shader, each work group can first load its tile of the
   image, together with a border of the blur radius, into `shared` memory, synchronize with `barrier()`, and then
   compute the blur from the shared memory. Compare the frame times of both versions for a large blur radius.
