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
CMAKE_SOURCE_DIR = /Users/cobalt/src/ray_tracer

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/cobalt/src/ray_tracer

# Utility rule file for Shaders.

# Include any custom commands dependencies for this target.
include CMakeFiles/Shaders.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/Shaders.dir/progress.make

CMakeFiles/Shaders: shaders/bin/raytrace.comp.spv
CMakeFiles/Shaders: shaders/bin/raytrace.frag.spv
CMakeFiles/Shaders: shaders/bin/raytrace.vert.spv

shaders/bin/raytrace.comp.spv: shaders/raytrace.comp
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/ray_tracer/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Generating shaders/bin/raytrace.comp.spv"
	/usr/local/bin/glslangValidator --target-env vulkan1.1 -V /Users/cobalt/src/ray_tracer/shaders/raytrace.comp -o /Users/cobalt/src/ray_tracer/shaders/bin/raytrace.comp.spv

shaders/bin/raytrace.frag.spv: shaders/raytrace.frag
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/ray_tracer/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Generating shaders/bin/raytrace.frag.spv"
	/usr/local/bin/glslangValidator --target-env vulkan1.1 -V /Users/cobalt/src/ray_tracer/shaders/raytrace.frag -o /Users/cobalt/src/ray_tracer/shaders/bin/raytrace.frag.spv

shaders/bin/raytrace.vert.spv: shaders/raytrace.vert
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/Users/cobalt/src/ray_tracer/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Generating shaders/bin/raytrace.vert.spv"
	/usr/local/bin/glslangValidator --target-env vulkan1.1 -V /Users/cobalt/src/ray_tracer/shaders/raytrace.vert -o /Users/cobalt/src/ray_tracer/shaders/bin/raytrace.vert.spv

Shaders: CMakeFiles/Shaders
Shaders: shaders/bin/raytrace.comp.spv
Shaders: shaders/bin/raytrace.frag.spv
Shaders: shaders/bin/raytrace.vert.spv
Shaders: CMakeFiles/Shaders.dir/build.make
.PHONY : Shaders

# Rule to build all files generated by this target.
CMakeFiles/Shaders.dir/build: Shaders
.PHONY : CMakeFiles/Shaders.dir/build

CMakeFiles/Shaders.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/Shaders.dir/cmake_clean.cmake
.PHONY : CMakeFiles/Shaders.dir/clean

CMakeFiles/Shaders.dir/depend:
	cd /Users/cobalt/src/ray_tracer && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/cobalt/src/ray_tracer /Users/cobalt/src/ray_tracer /Users/cobalt/src/ray_tracer /Users/cobalt/src/ray_tracer /Users/cobalt/src/ray_tracer/CMakeFiles/Shaders.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/Shaders.dir/depend

