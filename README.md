# Vulkan Path Tracer
A Vulkan-based compute path tracer written in C++. Code skeleton from *[VkGuide](https://vkguide.dev/)*.

## Features
- Sphere and triangle primitives
- Diffuse, metallic, and dielectric materials
- Wavefront .obj and .mtl loading
- ImGui implementation
- Bounding volume hierarchies

## Planned Features
- Dynamic camera system
- Importance sampling
- Disney's BRDF
- Wavefront path tracing

## Renders
![](renders/sponza.png)
<sup>Crytek Sponza from *[McGuire Computer Graphics Archive](https://casual-effects.com/data/)*</sup>
![](renders/dragon_gold.png)
<sup>Dragon from *[The Stanford 3D Scanning Repository](http://graphics.stanford.edu/data/3Dscanrep/)*</sup>
![](renders/squeezer_mtlmap.png)
<sup>Model and textures from *[The Model's Resource](https://www.models-resource.com/nintendo_switch/splatoon3/model/59382/)*</sup>
![](renders/bunny_dielectric.png)
<sup>Bunny from *[The Stanford 3D Scanning Repository](http://graphics.stanford.edu/data/3Dscanrep/)*</sup>