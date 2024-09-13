# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.29

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /opt/homebrew/Cellar/cmake/3.29.6/bin/cmake

# The command to remove a file.
RM = /opt/homebrew/Cellar/cmake/3.29.6/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/cobalt/src/vkguide

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/cobalt/src/vkguide

# Utility rule file for Shaders.

# Include any custom commands dependencies for this target.
include CMakeFiles/Shaders.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/Shaders.dir/progress.make

CMakeFiles/Shaders: shaders/bin/colored_triangle.frag.spv
CMakeFiles/Shaders: shaders/bin/colored_triangle.vert.spv
CMakeFiles/Shaders: shaders/bin/combined_mesh.frag.spv
CMakeFiles/Shaders: shaders/bin/combined_mesh.vert.spv
CMakeFiles/Shaders: shaders/bin/default_lit.frag.spv
CMakeFiles/Shaders: shaders/bin/raytrace.comp.spv
CMakeFiles/Shaders: shaders/bin/raytrace.frag.spv
CMakeFiles/Shaders: shaders/bin/raytrace.vert.spv
CMakeFiles/Shaders: shaders/bin/textured_mesh.frag.spv
CMakeFiles/Shaders: shaders/bin/tri_mesh.vert.spv
CMakeFiles/Shaders: shaders/bin/triangle.frag.spv
CMakeFiles/Shaders: shaders/bin/triangle.vert.spv

shaders/bin/colored_triangle.frag.spv: shaders/colored_triangle.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Generating shaders/bin/colored_triangle.frag.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/colored_triangle.frag -o /Users/cobalt/src/vkguide/shaders/bin/colored_triangle.frag.spv

shaders/bin/colored_triangle.vert.spv: shaders/colored_triangle.vert
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Generating shaders/bin/colored_triangle.vert.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/colored_triangle.vert -o /Users/cobalt/src/vkguide/shaders/bin/colored_triangle.vert.spv

shaders/bin/combined_mesh.frag.spv: shaders/combined_mesh.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Generating shaders/bin/combined_mesh.frag.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/combined_mesh.frag -o /Users/cobalt/src/vkguide/shaders/bin/combined_mesh.frag.spv

shaders/bin/combined_mesh.vert.spv: shaders/combined_mesh.vert
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Generating shaders/bin/combined_mesh.vert.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/combined_mesh.vert -o /Users/cobalt/src/vkguide/shaders/bin/combined_mesh.vert.spv

shaders/bin/default_lit.frag.spv: shaders/default_lit.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_5) "Generating shaders/bin/default_lit.frag.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/default_lit.frag -o /Users/cobalt/src/vkguide/shaders/bin/default_lit.frag.spv

shaders/bin/raytrace.comp.spv: shaders/raytrace.comp
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_6) "Generating shaders/bin/raytrace.comp.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/raytrace.comp -o /Users/cobalt/src/vkguide/shaders/bin/raytrace.comp.spv

shaders/bin/raytrace.frag.spv: shaders/raytrace.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_7) "Generating shaders/bin/raytrace.frag.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/raytrace.frag -o /Users/cobalt/src/vkguide/shaders/bin/raytrace.frag.spv

shaders/bin/raytrace.vert.spv: shaders/raytrace.vert
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_8) "Generating shaders/bin/raytrace.vert.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/raytrace.vert -o /Users/cobalt/src/vkguide/shaders/bin/raytrace.vert.spv

shaders/bin/textured_mesh.frag.spv: shaders/textured_mesh.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_9) "Generating shaders/bin/textured_mesh.frag.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/textured_mesh.frag -o /Users/cobalt/src/vkguide/shaders/bin/textured_mesh.frag.spv

shaders/bin/tri_mesh.vert.spv: shaders/tri_mesh.vert
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_10) "Generating shaders/bin/tri_mesh.vert.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/tri_mesh.vert -o /Users/cobalt/src/vkguide/shaders/bin/tri_mesh.vert.spv

shaders/bin/triangle.frag.spv: shaders/triangle.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_11) "Generating shaders/bin/triangle.frag.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/triangle.frag -o /Users/cobalt/src/vkguide/shaders/bin/triangle.frag.spv

shaders/bin/triangle.vert.spv: shaders/triangle.vert
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/vkguide/CMakeFiles --progress-num=$(CMAKE_PROGRESS_12) "Generating shaders/bin/triangle.vert.spv"
	/usr/local/bin/glslangValidator -V /Users/cobalt/src/vkguide/shaders/triangle.vert -o /Users/cobalt/src/vkguide/shaders/bin/triangle.vert.spv

Shaders: CMakeFiles/Shaders
Shaders: shaders/bin/colored_triangle.frag.spv
Shaders: shaders/bin/colored_triangle.vert.spv
Shaders: shaders/bin/combined_mesh.frag.spv
Shaders: shaders/bin/combined_mesh.vert.spv
Shaders: shaders/bin/default_lit.frag.spv
Shaders: shaders/bin/raytrace.comp.spv
Shaders: shaders/bin/raytrace.frag.spv
Shaders: shaders/bin/raytrace.vert.spv
Shaders: shaders/bin/textured_mesh.frag.spv
Shaders: shaders/bin/tri_mesh.vert.spv
Shaders: shaders/bin/triangle.frag.spv
Shaders: shaders/bin/triangle.vert.spv
Shaders: CMakeFiles/Shaders.dir/build.make
.PHONY : Shaders

# Rule to build all files generated by this target.
CMakeFiles/Shaders.dir/build: Shaders
.PHONY : CMakeFiles/Shaders.dir/build

CMakeFiles/Shaders.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/Shaders.dir/cmake_clean.cmake
.PHONY : CMakeFiles/Shaders.dir/clean

CMakeFiles/Shaders.dir/depend:
	cd /Users/cobalt/src/vkguide && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/cobalt/src/vkguide /Users/cobalt/src/vkguide /Users/cobalt/src/vkguide /Users/cobalt/src/vkguide /Users/cobalt/src/vkguide/CMakeFiles/Shaders.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/Shaders.dir/depend

