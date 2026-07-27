# bgfx rendering backend, off by default.
#
# Enable with -DLUMIN_BGFX=ON. When off, nothing here runs and the build is
# byte-for-byte what it was before bgfx entered the tree.
#
#   cmake .. -DLUMIN_BGFX=ON
#
# Two things get built:
#
#   bgfx        - the renderer itself, from the external/bgfx submodule.
#   shaderc-lib - bgfx's shader compiler, as a library rather than the tool it
#                 ships as, so kernels can be compiled at runtime. Solar2D
#                 supports graphics.defineEffect, i.e. shaders that only exist
#                 once the app is running, and bgfx accepts no source at draw
#                 time -- only its own compiled blobs. Compiling in-process is
#                 the only option that also works on iOS and Android, where
#                 spawning the shaderc executable is not permitted.

option(LUMIN_BGFX "Build the bgfx rendering backend" OFF)

if(NOT LUMIN_BGFX)
	return()
endif()

set(BGFX_ROOT "${CORONA_ROOT}/external/bgfx")

if(NOT EXISTS "${BGFX_ROOT}/bgfx/src/bgfx.cpp")
	message(FATAL_ERROR
		"LUMIN_BGFX is ON but external/bgfx is empty. Run:\n"
		"    git submodule update --init --recursive external/bgfx"
	)
endif()

# The examples pull in a window and input layer we have no use for.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)

# The shader tools option has to stay on, even though we build the compiler
# ourselves: bgfx only declares glslang, spirv-cross, fcpp and the rest of the
# compiler's dependencies when it is set. The shaderc executable it also
# declares costs nothing, since the subdirectory is EXCLUDE_FROM_ALL and
# nothing depends on it.
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)

add_subdirectory("${BGFX_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/bgfx" EXCLUDE_FROM_ALL)

# ----------------------------------------------------------------------------
# shaderc as a library

file(GLOB SHADERC_SOURCES "${BGFX_ROOT}/bgfx/tools/shaderc/*.cpp")

# Everything shaderc needs, compiled with the bgfx namespace renamed. shaderc
# is a standalone tool upstream and defines bgfx::g_allocator, bgfx::trace,
# bgfx::s_uniformTypeName and more for itself; linking it beside the real bgfx
# without this collides on every one of them. Rtt_BgfxShaderCompiler.cpp is
# built the same way and is the only bridge across, which is why its interface
# exposes no bgfx types.
add_library(shaderc-lib STATIC
	${SHADERC_SOURCES}
	"${BGFX_ROOT}/bgfx/src/shader.cpp"
	"${BGFX_ROOT}/bgfx/src/vertexlayout.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxShaderCompiler.cpp"
)

target_compile_definitions(shaderc-lib PRIVATE bgfx=bgfx_shaderc)

# shaderc.cpp holds both the entry point we want, bgfx::compileShader, and the
# tool's main(). Renaming main() at compile time keeps it from colliding with
# ours at link time, and avoids patching the submodule to achieve the same.
set_source_files_properties(
	"${BGFX_ROOT}/bgfx/tools/shaderc/shaderc.cpp"
	PROPERTIES COMPILE_DEFINITIONS "main=bgfx_shaderc_unused_main"
)

target_compile_definitions(shaderc-lib PRIVATE
	__STDC_CONSTANT_MACROS
	__STDC_FORMAT_MACROS
	__STDC_LIMIT_MACROS
)

# The engine builds as C++17; bx and shaderc need C++20. The engine's flags
# reach here through CMAKE_CXX_FLAGS, so override rather than rely on defaults.
target_compile_options(shaderc-lib PRIVATE -std=c++20)

target_include_directories(shaderc-lib PRIVATE
	"${BGFX_ROOT}/bgfx/include"
	"${BGFX_ROOT}/bgfx/src"
	"${BGFX_ROOT}/bgfx/3rdparty/dawn"
	"${BGFX_ROOT}/bgfx/3rdparty/dawn/src"
	"${BGFX_ROOT}/bgfx/3rdparty/dawn/src/tint"
	"${BGFX_ROOT}/bgfx/3rdparty/directx-headers/include/directx"
	"${BGFX_ROOT}/bgfx/3rdparty/directx-headers/include"
	"${BGFX_ROOT}/bgfx/3rdparty/directx-headers/include/wsl/stubs"
	"${BGFX_ROOT}/bgfx/3rdparty/fcpp"
	"${BGFX_ROOT}/bgfx/3rdparty/glslang"
	"${BGFX_ROOT}/bgfx/3rdparty/glslang/glslang/Public"
	"${BGFX_ROOT}/bgfx/3rdparty/glslang/glslang/Include"
	"${BGFX_ROOT}/bgfx/3rdparty/glsl-optimizer/include"
	"${BGFX_ROOT}/bgfx/3rdparty/glsl-optimizer/src/glsl"
	"${BGFX_ROOT}/bgfx/3rdparty/spirv-tools/include"
	"${BGFX_ROOT}/bgfx/3rdparty/spirv-cross"
	"${BGFX_ROOT}/bgfx/3rdparty/webgpu/include"
)

# Consumers only ever include shaderc.h, so keep that one public.
target_include_directories(shaderc-lib PUBLIC
	"${BGFX_ROOT}/bgfx/tools/shaderc"
)

target_include_directories(shaderc-lib PRIVATE
	"${CORONA_ROOT}/librtt"
	"${CORONA_ROOT}/librtt/Core"
)

target_link_libraries(shaderc-lib PUBLIC
	bx
	bimg
	fcpp
	glslang
	glsl-optimizer
	spirv-opt
	spirv-cross
	webgpu
	tint
)

# ----------------------------------------------------------------------------
# Where the compiler looks for bgfx_shader.sh and friends when it resolves a
# kernel's #include directives at runtime.

set(LUMIN_BGFX_SHADER_INCLUDE_DIR "${BGFX_ROOT}/bgfx/src")

# ----------------------------------------------------------------------------
# Backend sources, appended to whichever Solar2D target includes this.

set(LUMIN_BGFX_SOURCES
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxCommandBuffer.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxDrawState.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxFrameBufferObject.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxGeometry.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxProgram.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxRenderer.cpp"
	"${CORONA_ROOT}/librtt/Renderer/Rtt_BgfxTexture.cpp"
)

# Note: Rtt_BgfxShaderCompiler.cpp is deliberately not here. It belongs to
# shaderc-lib, which builds under the renamed namespace.

# Call once per Solar2D target to give it the backend.
function(lumin_target_enable_bgfx TARGET_NAME)
	target_sources(${TARGET_NAME} PRIVATE ${LUMIN_BGFX_SOURCES})

	# bx's headers require C++20, while the engine builds as C++17. Raising it
	# for these files alone keeps the change off every other translation unit.
	set_source_files_properties(
		${LUMIN_BGFX_SOURCES}
		TARGET_DIRECTORY ${TARGET_NAME}
		PROPERTIES COMPILE_OPTIONS "-std=c++20"
	)

	target_compile_definitions(${TARGET_NAME} PUBLIC
		Rtt_USE_BGFX
		LUMIN_BGFX_SHADER_INCLUDE_DIR="${LUMIN_BGFX_SHADER_INCLUDE_DIR}"
	)

	target_include_directories(${TARGET_NAME} PRIVATE
		"${BGFX_ROOT}/bgfx/include"
		"${BGFX_ROOT}/bx/include"
		"${BGFX_ROOT}/bimg/include"
	)

	target_link_libraries(${TARGET_NAME} bgfx shaderc-lib)
endfunction()
