# Mesh

So far, we did everything on the most basic level of OpenGL calls, but this would quickly become unwieldy if we would
like to add more models.
In this assignment, you will refactor the code by adding some layers of abstraction.
We will add a `Mesh` class that will encapsulate all these low-level calls concerned with vertex and index buffer
manipulations.
This class is already provided for you in the `src/Engine/` directory.

Start by copying the `09_CameraMovement` assignment to `10_Mesh`, as described in the
[Preparing the assignments](../README.md#preparing-the-assignments) section.

But first we will do another refactorisation that will consist of moving the `Camera` and `CameraController` classes
to the `src/Engine` directory, so
they can be accessible to all further assignments without a need to copy them:

1. Move the files `camera.h` and `camera_controller.h` from `src/Assignments/10_Mesh` to the
   directory `src/Engine`.

2. Put the `Camera` and `CameraController` classes into the `xe` namespace, by surrounding their code by the namespace
   declaration:

   ```c++
   namespace xe {

   }
   ```
   Only the classes go into the `xe` namespace; the helper functions `logistic`, `logit` and `rotation` can stay in
   their anonymous namespace at the top of `camera.h`.
   Modify all the references to those classes by prefixing their names with `xe::`, e.g. `xe::Camera`.

3. Now change the `#include` directives that include those files so as to reflect the new path, e.g. `#include "Engine/camera.h"`.
   If you use `CLion` or another IDE this may have been done for you already.
   Remove `camera.h` and `camera_controller.h` from the `add_executable` list in `CMakeLists.txt`, where you added them
   in the previous assignments; they now belong to the `Engine` library, and CMake would fail with "Cannot find source
   file" otherwise.

4. Finally, in the `CMakeLists.txt` file in `src/Assignments/10_Mesh` directory modify `target_link_libraries` function to link
   the `Engine` library

    ```cmake
    target_link_libraries(${PROJECT_NAME} PUBLIC Engine spdlog::spdlog)
    ```
   Everything should still work as before.

## Mesh class

`Mesh` class corresponds to a single `Vertex Array Object` (VAO) containing one vertex buffer and one index buffer.
Every object of this class must contain at least one object of `Mesh::SubMesh` class.
`SubMesh` class corresponds to a single call to `glDrawElements` function and contains the range of the indices to be
submitted to this call. `SubMesh` also contains a pointer to the `Material` that will be used in next assignments to
specify the kind of material used by this submesh which in turn will determine the color of the pixels. By default,
this pointer is initialized to the material returned by `NullMaterial::null_material()`, a single shared
`NullMaterial` that does nothing.
Submeshes are added to mesh using the `add_submesh` method.

Mesh class also assumes using a predefined set of vertex attributes that are specified by the `xe::AttributeType` enum.
The values of the enum correspond to the location value of the corresponding attribute in vertex shader.
Adding the attribute to the mesh is done using the `add_attribute` method.
This method makes the same three calls you wrote for each attribute in the
[`06_Pyramid`](../06_Pyramid/README.md#direct-state-access-for-the-vertex-array) assignment: `glEnableVertexArrayAttrib`,
`glVertexArrayAttribFormat` and `glVertexArrayAttribBinding`. It uses the `AttributeType` enum instead of the attribute
index, and it does not require the `stride` argument: the constructor (see description below) attaches the vertex
buffer with the given stride to the binding point `0`, and all the attributes are read from it.

1. We will store the meshes in the vector of meshes in the `SimpleShapeApplication` class, so please include the `Engine/Mesh.h` header in `app.h` and add the field
   ```c++
   std::vector<xe::Mesh*> meshes_;
   ```
   to this class in `app.h` file and a corresponding `add_mesh` method:

   ```c++
   void add_mesh(xe::Mesh *mesh) {
       meshes_.push_back(mesh);
   }
   ```
2. In the vertex shader change the location of the `a_vertex_color` to five (5). Change the corresponding attribute
   index, i.e. the value of the `COLOR_LOCATION` constant used in the calls to `glEnableVertexArrayAttrib`,
   `glVertexArrayAttribFormat` and `glVertexArrayAttribBinding` (but not the binding point, `VERTEX_BUFFER_BINDING`,
   which stays `0`). Location 5 is the one the `Mesh` class uses for the color attribute, `xe::AttributeType::COLOR_0`
   (see `Engine/Mesh.h`). Check that the pyramid still has its colors before going further.

3. Next we have to create the mesh for our pyramid in the `init` method. So start with creating a new `Mesh`
   ```c++
   auto pyramid = new xe::Mesh(6 * sizeof(float), vertices.size() * sizeof(float), GL_STATIC_DRAW,
                                indices.size() * sizeof(GLubyte), GL_UNSIGNED_BYTE, GL_STATIC_DRAW);
   ```
   The constructor of the `Mesh` takes as arguments:
    1. The stride in the vertex buffer, i.e., the size of the single vertex data (attributes) in bytes.
    2. The size of the vertex buffer in bytes.
    3. The usage hint of the vertex buffer.
    4. The size of the index buffer in bytes.
    5. The type of the indices stored in the index buffer.
    6. The usage hint of the index buffer.

   Those values cannot be changed after the creation of the mesh.

4. Load the vertices using the `load_vertices` method and set the layouts for attributes using `add_attribute`
   method:
   ```c++
   pyramid->load_vertices(0, vertices.size() * sizeof(GLfloat), vertices.data());
   pyramid->add_attribute(xe::AttributeType::POSITION, 3, GL_FLOAT, 0);
   ```
   The arguments of `load_vertices` are the offset __in bytes__ into the vertex buffer at which to start writing, the
   number of bytes to write, and a pointer to the data. The arguments of `add_attribute(type, size, gl_type, offset)` are the attribute
   type (e.g. `xe::AttributeType::POSITION`), the number of its components, the type of the components (e.g.
   `GL_FLOAT`), and the offset __in bytes__ of the attribute from the beginning of the vertex. Add the color attribute,
   `xe::AttributeType::COLOR_0`, in the same way.

5. Similarly, load data into the index buffer using `load_indices` method. It takes the same arguments as
   `load_vertices`.

6. Add a submesh to this mesh that will contain all the indices and schedule mesh for drawing using
   the `SimpleShapeApplication::add_mesh` method. The arguments of `add_submesh(start, end)` are the positions in the
   index buffer of the first index of the submesh and of the index __after__ the last one, so for all the indices:
   ```c++
   pyramid->add_submesh(0, indices.size());
   ```

   You do not have to delete the meshes yourself. `Mesh` is derived from `RegisteredObject`, so all meshes are deleted
   automatically when the application finishes. This is why they have to be created with `new`.

7. Finally, in the method frame change the call to `glDrawElements` to a loop that will call `draw` method on each mesh
   in `meshes_`

   ```c++
    for (auto m: meshes_)
        m->draw();
   ```
   Remove the bind and unbind `vao_` calls from this method.

8. Please delete all unnecessary code: the creation of the vertex and index buffers and of the vertex array object in
   the `init` method, the `POSITION_LOCATION`, `COLOR_LOCATION` and `VERTEX_BUFFER_BINDING` constants, and the `vao_`
   field.
