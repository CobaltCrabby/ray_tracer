SHADER_SRCS = $(wildcard shaders/*.comp shaders/*.frag shaders/*.vert)
SHADER_BINS = $(subst shaders/,shaders/bin/,$(patsubst %,%.spv,$(SHADER_SRCS)))

all: bin/raytracer $(SHADER_BINS)

bin/raytracer: $(patsubst %.cpp, %.o, $(wildcard src/*.cpp))
	clang++ $^ -o $@ -std=c++17 -arch arm64 -L/opt/homebrew/Cellar/sdl2/2.30.5/lib -lSDL2 -lSDL2main -Wl,-rpath,/opt/homebrew/Cellar/sdl2/2.30.5/lib -Wl,-rpath,/usr/local/lib third_party/libvkbootstrap.a third_party/libtinyobjloader.a third_party/libimgui.a /usr/local/lib/libvulkan.dylib

src/%.o: src/%.cpp src/*.h
	clang++ -c -o $@ -x c++ $< -std=c++17 -I src -I third_party/vma -I third_party/glm -I third_party/imgui -I /opt/homebrew/Cellar/sdl2/2.30.5/include/SDL2 -I third_party/vkbootstrap -I third_party/stb_image -I third_party/tinyobjloader

$(SHADER_BINS): $(SHADER_SRCS)
	glslc $(subst .spv,,$(subst /bin,,$@)) -o $@

run:
	./bin/raytracer
.PHONY: run