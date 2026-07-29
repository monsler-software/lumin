# The bgfx libraries, built on their own for hosts whose build system is not
# CMake.
#
# Windows builds the simulator with MSBuild and macOS with Xcode. Neither can
# be handed bgfx directly: bgfx, bx, bimg and the dozen projects shaderc needs
# (glslang, spirv-tools, spirv-cross, glsl-optimizer, fcpp, tint, webgpu) all
# describe themselves in CMake, and re-describing them in two more dialects is
# how build definitions drift apart. So those hosts build this project first
# and link what it produces; the rules themselves are ../bgfx.cmake, the same
# ones the Linux build uses, so there is one description of how bgfx is built.
#
# What the hosts do NOT get from here is Corona's own bgfx sources -- the
# Rtt_Bgfx* renderer and librtt/UI. Those are compiled by the host project
# itself, because they are engine code and need the engine's own configuration
# (Rtt_WIN_ENV, Rtt_MAC_ENV, the debug/release macros) to mean the same thing
# they do in every other Corona translation unit. They only need bgfx's headers
# and these libraries at the link line.
#
# Building it:
#
#   cmake -S cmake/lumin-bgfx -B build-bgfx-libs -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-bgfx-libs --config Release
#
# On Windows add the generator and architecture the simulator is built with, so
# the runtime libraries match:
#
#   cmake -S cmake/lumin-bgfx -B build-bgfx-libs -G "Visual Studio 17 2022" -A Win32
#
# The static libraries land in build-bgfx-libs. Then, to build a host against
# them:
#
#   Windows   msbuild platform\windows\Corona.Simulator.sln /p:LuminBgfx=true
#             (paths and the library list are platform/windows/Bgfx.Definitions.props)
#
#   macOS     xcodebuild -project platform/mac/ratatouille.xcodeproj \
#                 LUMIN_BGFX_EXCLUDED_SOURCES="" \
#                 LUMIN_BGFX_DEFINES=Rtt_USE_BGFX \
#                 LUMIN_BGFX_LIB_DIR=<build-bgfx-libs> \
#                 LUMIN_BGFX_LDFLAGS="-lbgfx -lbimg -lbx -lshaderc-lib -lfcpp \
#                     -lglslang -lglsl-optimizer -lspirv-cross -lspirv-opt -ltint"
#
#             Xcode cannot include a file conditionally, so the bgfx sources are
#             always in libtachyon and excluded from the build by default;
#             clearing LUMIN_BGFX_EXCLUDED_SOURCES is what puts them back in.

cmake_minimum_required(VERSION 3.15)

project(lumin-bgfx CXX C)

# Where the engine's tree starts, two directories up from here. bgfx.cmake
# reaches into it for external/bgfx and for Rtt_BgfxShaderCompiler.cpp.
get_filename_component(CORONA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The option bgfx.cmake guards itself with. It is off by default there so that
# the Linux build is unchanged unless asked; here it is the entire point.
set(LUMIN_BGFX ON CACHE BOOL "" FORCE)

include("${CORONA_ROOT}/cmake/bgfx.cmake")

# bgfx's own subdirectory is added EXCLUDE_FROM_ALL, since the Linux build
# reaches its targets through target_link_libraries rather than building them
# for their own sake. Here they are what is being asked for, so name them.
add_custom_target(lumin-bgfx-all ALL)
add_dependencies(lumin-bgfx-all bgfx shaderc-lib)
