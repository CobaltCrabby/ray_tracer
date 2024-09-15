
# Add source to this project's executable.
add_executable(raytracer
    main.cpp
    vk_engine.cpp
    vk_engine.h
    vk_types.h
    vk_mesh.cpp
    vk_mesh.h
    vk_initializers.cpp
    vk_initializers.h
    vk_textures.cpp
    vk_textures.h)

set_property(TARGET raytracer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:raytracer>")

target_include_directories(raytracer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(raytracer vkbootstrap vma glm tinyobjloader imgui stb_image)

target_link_libraries(raytracer Vulkan::Vulkan sdl2)

add_dependencies(raytracer Shaders)