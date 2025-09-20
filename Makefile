SHADER_SRCS = $(wildcard shaders/*.comp shaders/*.frag shaders/*.vert)
SHADER_BINS = $(subst shaders/,shaders/bin/,$(patsubst %,%.spv,$(SHADER_SRCS)))

VULKAN_SDK = /Users/cobalt/VulkanSDK/1.4.321.0/macOS
CC_FLAGS = -std=c++17 -arch arm64 -Wno-everything
LD_FLAGS = -L $(VULKAN_SDK)/lib -L third_party -Wl,-rpath,$(VULKAN_SDK)/lib -limgui -lvulkan -lSDL2-2.0
HEADERS = -include $(VULKAN_SDK)/include/vulkan/vulkan.h -I $(VULKAN_SDK)/include -I $(VULKAN_SDK)/include/glm -I $(VULKAN_SDK)/include/SDL2 -I $(VULKAN_SDK)/include/vma -I third_party/imgui -I third_party/vkbootstrap -I headers

all: bin/raytracer $(SHADER_BINS)

bin/raytracer: $(patsubst %.cpp, %.o, $(wildcard src/*.cpp))
	clang++ $^ third_party/vkbootstrap/VkBootstrap.cpp -o $@ $(CC_FLAGS) $(LD_FLAGS)

src/%.o: src/%.cpp headers/*.hpp
	clang++ -c -o $@ -x c++ $< $(CC_FLAGS) $(HEADERS)

shaders/bin/%.spv: shaders/%
	glslc $(subst .spv,,$(subst /bin,,$@)) -o $@ --target-env=vulkan1.4

run:
	./bin/raytracer
.PHONY: run

clean: src/*.o ./bin/raytracer
	rm $^
.PHONY: clean