cmake_minimum_required(VERSION 2.8)
project(dcpu)

set(CMAKE_DEBUG_POSTFIX "_d")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

if(CMAKE_BUILD_TYPE STREQUAL "")
	set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: None (CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

if(USE_CLANG)
	set(CMAKE_C_COMPILER "/usr/bin/clang")
	set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

	set(CMAKE_AR "/usr/bin/llvm-ar")
	set(CMAKE_LINKER "/usr/bin/llvm-ld")
	set(CMAKE_NM "/usr/bin/llvm-nm")
	set(CMAKE_OBJDUMP "/usr/bin/llvm-objdump")
	set(CMAKE_RANLIB "/usr/bin/llvm-ranlib")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	# set(CMAKE_CXX_FLAGS "-O0 -pg")
endif()

find_package(Boost REQUIRED COMPONENTS system filesystem thread chrono program_options)
find_package(Threads REQUIRED)

include_directories(${Boost_INCLUDE_DIR})

set(ASMJIT_SRC src/asmjit/AssemblerX86X64.cpp src/asmjit/CodeGenerator.cpp
	src/asmjit/Compiler.cpp
	src/asmjit/CompilerX86X64.cpp
	src/asmjit/CpuInfo.cpp
	src/asmjit/Defs.cpp
	src/asmjit/DefsX86X64.cpp
	src/asmjit/Logger.cpp
	src/asmjit/MemoryManager.cpp
	src/asmjit/MemoryMarker.cpp
	src/asmjit/OperandX86X64.cpp
	src/asmjit/Platform.cpp
	src/asmjit/Util.cpp)
set(HW_SRC src/hw/clock.cpp)

add_executable(dcpu src/main.cpp src/jit.cpp src/dcpu.cpp ${HW_SRC} ${ASMJIT_SRC})
target_link_libraries(dcpu ${Boost_LIBRARIES})
