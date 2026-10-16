# Editor-only SDK: source builds additionally require ISPC/TBB and the full Metal toolchain.
# Pin the upstream release and archive digest; no denoiser is linked into engine or App.
set(COMET_OIDN_VERSION 2.5.1)
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(COMET_OIDN_ARCHIVE arm64.macos.tar.gz)
    set(COMET_OIDN_SHA256 98e0aca8e7ab69e9f4f0191582a500fd8b0d9085d68662f18ab6469e934a6efd)
elseif(APPLE)
    set(COMET_OIDN_ARCHIVE x86_64.macos.tar.gz)
    set(COMET_OIDN_SHA256 a4341639005a33ce0d944bd7c771ce34de1465be7eb3189b7c3b5b0064429832)
elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
    set(COMET_OIDN_ARCHIVE x64.windows.zip)
    set(COMET_OIDN_SHA256 f11f91bc072a5e3a564515724cb72ab8fcfbc445c84a84c197ff9b16cc01396f)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(COMET_OIDN_ARCHIVE x86_64.linux.tar.gz)
    set(COMET_OIDN_SHA256 743c3e2aff8c220d5d70fe6cb970fb3d36f2702d2693c61d1d148e404cf37cd6)
else()
    message(FATAL_ERROR "OIDN SDK is unavailable for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
endif()

include(FetchContent)
FetchContent_Declare(comet_oidn
    URL "https://github.com/RenderKit/oidn/releases/download/v${COMET_OIDN_VERSION}/oidn-${COMET_OIDN_VERSION}.${COMET_OIDN_ARCHIVE}"
    URL_HASH "SHA256=${COMET_OIDN_SHA256}"
    SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(comet_oidn)
find_package(OpenImageDenoise ${COMET_OIDN_VERSION} EXACT REQUIRED CONFIG GLOBAL
    PATHS "${comet_oidn_SOURCE_DIR}/lib/cmake/OpenImageDenoise-${COMET_OIDN_VERSION}"
    NO_DEFAULT_PATH)

# Backend modules are loaded by OIDN, so TARGET_RUNTIME_DLLS cannot discover them.
function(comet_copy_oidn_runtime executable)
    if(WIN32)
        get_target_property(oidn_library OpenImageDenoise IMPORTED_LOCATION_RELEASE)
        get_filename_component(oidn_bin "${oidn_library}" DIRECTORY)
        file(GLOB oidn_dlls "${oidn_bin}/*.dll")
        add_custom_command(TARGET ${executable} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${oidn_dlls} $<TARGET_FILE_DIR:${executable}>
            COMMAND_EXPAND_LISTS)
    endif()
endfunction()
