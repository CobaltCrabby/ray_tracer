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
CMAKE_BINARY_DIR = /Users/cobalt/src/vkguide/src

# Include any dependencies generated for this target.
include third_party/CMakeFiles/tinyobjloader.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include third_party/CMakeFiles/tinyobjloader.dir/compiler_depend.make

# Include the progress variables for this target.
include third_party/CMakeFiles/tinyobjloader.dir/progress.make

# Include the compile flags for this target's objects.
include third_party/CMakeFiles/tinyobjloader.dir/flags.make

third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o: third_party/CMakeFiles/tinyobjloader.dir/flags.make
third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o: /Users/cobalt/src/vkguide/third_party/tinyobjloader/tiny_obj_loader.cc
third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o: third_party/CMakeFiles/tinyobjloader.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/cobalt/src/vkguide/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o"
	cd /Users/cobalt/src/vkguide/src/third_party && /Library/Developer/CommandLineTools/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o -MF CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o.d -o CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o -c /Users/cobalt/src/vkguide/third_party/tinyobjloader/tiny_obj_loader.cc

third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.i"
	cd /Users/cobalt/src/vkguide/src/third_party && /Library/Developer/CommandLineTools/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/cobalt/src/vkguide/third_party/tinyobjloader/tiny_obj_loader.cc > CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.i

third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.s"
	cd /Users/cobalt/src/vkguide/src/third_party && /Library/Developer/CommandLineTools/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/cobalt/src/vkguide/third_party/tinyobjloader/tiny_obj_loader.cc -o CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.s

# Object files for target tinyobjloader
tinyobjloader_OBJECTS = \
"CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o"

# External object files for target tinyobjloader
tinyobjloader_EXTERNAL_OBJECTS =

third_party/libtinyobjloader.a: third_party/CMakeFiles/tinyobjloader.dir/tinyobjloader/tiny_obj_loader.cc.o
third_party/libtinyobjloader.a: third_party/CMakeFiles/tinyobjloader.dir/build.make
third_party/libtinyobjloader.a: third_party/CMakeFiles/tinyobjloader.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/Users/cobalt/src/vkguide/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX static library libtinyobjloader.a"
	cd /Users/cobalt/src/vkguide/src/third_party && $(CMAKE_COMMAND) -P CMakeFiles/tinyobjloader.dir/cmake_clean_target.cmake
	cd /Users/cobalt/src/vkguide/src/third_party && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/tinyobjloader.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
third_party/CMakeFiles/tinyobjloader.dir/build: third_party/libtinyobjloader.a
.PHONY : third_party/CMakeFiles/tinyobjloader.dir/build

third_party/CMakeFiles/tinyobjloader.dir/clean:
	cd /Users/cobalt/src/vkguide/src/third_party && $(CMAKE_COMMAND) -P CMakeFiles/tinyobjloader.dir/cmake_clean.cmake
.PHONY : third_party/CMakeFiles/tinyobjloader.dir/clean

third_party/CMakeFiles/tinyobjloader.dir/depend:
	cd /Users/cobalt/src/vkguide/src && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/cobalt/src/vkguide /Users/cobalt/src/vkguide/third_party /Users/cobalt/src/vkguide/src /Users/cobalt/src/vkguide/src/third_party /Users/cobalt/src/vkguide/src/third_party/CMakeFiles/tinyobjloader.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : third_party/CMakeFiles/tinyobjloader.dir/depend

