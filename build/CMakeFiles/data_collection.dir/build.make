# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.24

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
CMAKE_COMMAND = /opt/local/bin/cmake

# The command to remove a file.
RM = /opt/local/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/noahdrakes/Documents/research/dvrkDataCollection

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/noahdrakes/Documents/research/dvrkDataCollection/build

# Include any dependencies generated for this target.
include CMakeFiles/data_collection.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/data_collection.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/data_collection.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/data_collection.dir/flags.make

CMakeFiles/data_collection.dir/data_collection.cpp.o: CMakeFiles/data_collection.dir/flags.make
CMakeFiles/data_collection.dir/data_collection.cpp.o: /Users/noahdrakes/Documents/research/dvrkDataCollection/data_collection.cpp
CMakeFiles/data_collection.dir/data_collection.cpp.o: CMakeFiles/data_collection.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/noahdrakes/Documents/research/dvrkDataCollection/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/data_collection.dir/data_collection.cpp.o"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/data_collection.dir/data_collection.cpp.o -MF CMakeFiles/data_collection.dir/data_collection.cpp.o.d -o CMakeFiles/data_collection.dir/data_collection.cpp.o -c /Users/noahdrakes/Documents/research/dvrkDataCollection/data_collection.cpp

CMakeFiles/data_collection.dir/data_collection.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/data_collection.dir/data_collection.cpp.i"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/noahdrakes/Documents/research/dvrkDataCollection/data_collection.cpp > CMakeFiles/data_collection.dir/data_collection.cpp.i

CMakeFiles/data_collection.dir/data_collection.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/data_collection.dir/data_collection.cpp.s"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/noahdrakes/Documents/research/dvrkDataCollection/data_collection.cpp -o CMakeFiles/data_collection.dir/data_collection.cpp.s

# Object files for target data_collection
data_collection_OBJECTS = \
"CMakeFiles/data_collection.dir/data_collection.cpp.o"

# External object files for target data_collection
data_collection_EXTERNAL_OBJECTS =

data_collection: CMakeFiles/data_collection.dir/data_collection.cpp.o
data_collection: CMakeFiles/data_collection.dir/build.make
data_collection: libudp_lib.a
data_collection: CMakeFiles/data_collection.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Users/noahdrakes/Documents/research/dvrkDataCollection/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable data_collection"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/data_collection.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/data_collection.dir/build: data_collection
.PHONY : CMakeFiles/data_collection.dir/build

CMakeFiles/data_collection.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/data_collection.dir/cmake_clean.cmake
.PHONY : CMakeFiles/data_collection.dir/clean

CMakeFiles/data_collection.dir/depend:
	cd /Users/noahdrakes/Documents/research/dvrkDataCollection/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/noahdrakes/Documents/research/dvrkDataCollection /Users/noahdrakes/Documents/research/dvrkDataCollection /Users/noahdrakes/Documents/research/dvrkDataCollection/build /Users/noahdrakes/Documents/research/dvrkDataCollection/build /Users/noahdrakes/Documents/research/dvrkDataCollection/build/CMakeFiles/data_collection.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/data_collection.dir/depend
