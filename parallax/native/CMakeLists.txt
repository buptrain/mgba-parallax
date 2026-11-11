cmake_minimum_required(VERSION 3.21)
project(mgba_parallax_instrumentation LANGUAGES C)

include(CTest)

# The bridge, audio ring, and generic libretro host use C11 atomics. MSVC's
# atomics implementation remains opt-in, even with /std:c11, so configure the
# documented experimental switch for local Windows contract-test builds.
if(MSVC)
  add_compile_options(/std:c11 /experimental:c11atomics)
endif()

add_library(gba_xr_reference STATIC
  src/reference_compositor.c
)
target_include_directories(gba_xr_reference PUBLIC include)
target_compile_features(gba_xr_reference PUBLIC c_std_11)

add_library(gba_xr_bridge_objects OBJECT
  src/bridge.c
)
target_include_directories(gba_xr_bridge_objects PUBLIC include)
target_compile_features(gba_xr_bridge_objects PUBLIC c_std_11)
set_target_properties(gba_xr_bridge_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(gba_xr_bridge STATIC $<TARGET_OBJECTS:gba_xr_bridge_objects>)
target_include_directories(gba_xr_bridge PUBLIC include)
target_compile_features(gba_xr_bridge PUBLIC c_std_11)

add_library(gba_xr_visible_partition STATIC
  src/visible_partition.c
)
target_include_directories(gba_xr_visible_partition PUBLIC include)
target_compile_features(gba_xr_visible_partition PUBLIC c_std_11)
set_target_properties(gba_xr_visible_partition PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(gba_xr_independent_candidate STATIC
  src/independent_candidate.c
)
target_include_directories(gba_xr_independent_candidate PUBLIC include)
target_compile_features(gba_xr_independent_candidate PUBLIC c_std_11)
set_target_properties(gba_xr_independent_candidate PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(gba_xr_gba_blend STATIC src/gba_blend.c)
target_include_directories(gba_xr_gba_blend PUBLIC include)
target_compile_features(gba_xr_gba_blend PUBLIC c_std_11)

add_library(gba_xr_audio_ring STATIC src/audio_ring.c)
target_include_directories(gba_xr_audio_ring PUBLIC include)
target_compile_features(gba_xr_audio_ring PUBLIC c_std_11)

add_library(emuxr_libretro_host STATIC src/emuxr_libretro_host.c)
target_include_directories(emuxr_libretro_host PUBLIC include)
target_compile_features(emuxr_libretro_host PUBLIC c_std_11)

add_library(emuxr_pcm_resampler STATIC src/emuxr_pcm_resampler.c)
target_include_directories(emuxr_pcm_resampler PUBLIC include)
target_compile_features(emuxr_pcm_resampler PUBLIC c_std_11)

add_library(emuxr_gba_bridge_sink STATIC src/emuxr_gba_bridge_sink.c)
target_include_directories(emuxr_gba_bridge_sink PUBLIC include)
target_compile_features(emuxr_gba_bridge_sink PUBLIC c_std_11)
target_link_libraries(emuxr_gba_bridge_sink PUBLIC gba_xr_bridge)

add_library(emuxr_gba_libretro_worker STATIC src/emuxr_gba_libretro_worker.c)
target_include_directories(emuxr_gba_libretro_worker PUBLIC include)
target_compile_features(emuxr_gba_libretro_worker PUBLIC c_std_11)
target_link_libraries(emuxr_gba_libretro_worker PUBLIC emuxr_libretro_host emuxr_gba_bridge_sink gba_xr_audio_ring emuxr_pcm_resampler)
add_library(emuxr_mgba_parity STATIC src/emuxr_mgba_parity.c)
target_include_directories(emuxr_mgba_parity PUBLIC include)
target_compile_features(emuxr_mgba_parity PUBLIC c_std_11)
set_target_properties(gba_xr_audio_ring emuxr_libretro_host emuxr_pcm_resampler
  emuxr_gba_bridge_sink emuxr_gba_libretro_worker emuxr_mgba_parity
  PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(gba_xr_reference PRIVATE /W4 /WX)
  target_compile_options(gba_xr_bridge_objects PRIVATE /W4 /WX)
  target_compile_options(gba_xr_visible_partition PRIVATE /W4 /WX)
  target_compile_options(gba_xr_independent_candidate PRIVATE /W4 /WX)
  target_compile_options(gba_xr_gba_blend PRIVATE /W4 /WX)
  target_compile_options(gba_xr_audio_ring PRIVATE /W4 /WX)
  target_compile_options(emuxr_libretro_host PRIVATE /W4 /WX)
  target_compile_options(emuxr_pcm_resampler PRIVATE /W4 /WX)
  target_compile_options(emuxr_gba_bridge_sink PRIVATE /W4 /WX)
  target_compile_options(emuxr_gba_libretro_worker PRIVATE /W4 /WX)
  target_compile_options(emuxr_mgba_parity PRIVATE /W4 /WX)
else()
  target_compile_options(gba_xr_reference PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(gba_xr_bridge_objects PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(gba_xr_visible_partition PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(gba_xr_independent_candidate PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(gba_xr_gba_blend PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(gba_xr_audio_ring PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(emuxr_libretro_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(emuxr_pcm_resampler PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(emuxr_gba_bridge_sink PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(emuxr_gba_libretro_worker PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(emuxr_mgba_parity PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(GBAXR_BUILD_MGBA_ADAPTER "Build the mGBA resolved-frame adapter" OFF)
option(GBAXR_BUILD_MGBA_LIBRETRO_CORE "Build the opt-in mGBA libretro core/provider" OFF)
set(MGBA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "mGBA source root (this repository by default)")

if(GBAXR_BUILD_MGBA_ADAPTER OR GBAXR_BUILD_MGBA_LIBRETRO_CORE)
  if(NOT EXISTS "${MGBA_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "mGBA checkout not found at MGBA_ROOT: ${MGBA_ROOT}")
  endif()

  set(MGBA_PROVENANCE_PATCH "${CMAKE_CURRENT_LIST_DIR}/../patches/0001-final-pixel-provenance.patch")
  if(NOT EXISTS "${MGBA_PROVENANCE_PATCH}")
    message(FATAL_ERROR "Required mGBA provenance patch is missing: ${MGBA_PROVENANCE_PATCH}")
  endif()
  execute_process(
    COMMAND git -C "${MGBA_ROOT}" apply --reverse --check "${MGBA_PROVENANCE_PATCH}"
    RESULT_VARIABLE MGBA_PROVENANCE_ALREADY_APPLIED
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(NOT MGBA_PROVENANCE_ALREADY_APPLIED EQUAL 0)
    execute_process(
      COMMAND git -C "${MGBA_ROOT}" apply "${MGBA_PROVENANCE_PATCH}"
      RESULT_VARIABLE MGBA_PROVENANCE_APPLY_RESULT
    )
    if(NOT MGBA_PROVENANCE_APPLY_RESULT EQUAL 0)
      message(FATAL_ERROR "Could not apply the pinned mGBA provenance patch")
    endif()
  endif()

  # mGBA is added from the separately acquired, pinned source checkout. The
  # direct adapter and optional libretro core share that exact source revision.
  set(LIBMGBA_ONLY ON CACHE BOOL "Build mGBA as a static core" FORCE)
  set(M_CORE_GBA ON CACHE BOOL "Build the GBA core" FORCE)
  set(M_CORE_GB OFF CACHE BOOL "Exclude the Game Boy core" FORCE)
  set(BUILD_STATIC ON CACHE BOOL "Build static mGBA" FORCE)
  set(BUILD_SHARED OFF CACHE BOOL "Do not build shared mGBA" FORCE)
  set(BUILD_LIBRETRO ${GBAXR_BUILD_MGBA_LIBRETRO_CORE} CACHE BOOL "Build mGBA libretro core" FORCE)
  add_subdirectory("${MGBA_ROOT}" "${CMAKE_BINARY_DIR}/mgba")

  if(GBAXR_BUILD_MGBA_LIBRETRO_CORE)
    # mCore is a public C struct. The libretro target compiles the core and
    # platform/libretro sources in one shared object, so every one must see
    # the same conditional layout. In particular ENABLE_DIRECTORIES inserts
    # mCore::dirs before the function-pointer table; without it retro_load_game
    # calls the wrong slot at core->init and crashes. Keep this explicit rather
    # than relying on the direct-adapter target's private definitions.
    target_compile_definitions(mgba_libretro PRIVATE ENABLE_DIRECTORIES)
    # ENABLE_DIRECTORIES activates mDirectorySet's VDirOpen/VDirCreate path.
    # The upstream MINIMAL_CORE libretro target omits this source because it
    # normally omits the mCore::dirs field; include the matching implementation
    # here so the ABI repair cannot turn into unresolved directory calls.
    target_sources(mgba_libretro PRIVATE "${MGBA_ROOT}/src/util/vfs/vfs-dirent.c")
    get_target_property(MGBA_LIBRETRO_CORE_DEFINITIONS mgba_libretro COMPILE_DEFINITIONS)
    list(FIND MGBA_LIBRETRO_CORE_DEFINITIONS ENABLE_DIRECTORIES
      MGBA_LIBRETRO_HAS_DIRECTORIES_LAYOUT)
    if(MGBA_LIBRETRO_HAS_DIRECTORIES_LAYOUT EQUAL -1)
      message(FATAL_ERROR "mGBA libretro mCore ABI requires ENABLE_DIRECTORIES")
    endif()

    add_library(emuxr_mgba_libretro_provider STATIC src/emuxr_mgba_libretro_provider.c)
    target_include_directories(emuxr_mgba_libretro_provider
      PUBLIC include
      PRIVATE "${MGBA_ROOT}/src/platform/libretro"
    )
    target_compile_features(emuxr_mgba_libretro_provider PUBLIC c_std_11)
    target_link_libraries(emuxr_mgba_libretro_provider PUBLIC emuxr_libretro_host mgba_libretro)
    set_target_properties(emuxr_mgba_libretro_provider PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # This is deliberately a separate experiment from libgba_xr.so. It owns
    # the generic worker/provider wrapper while linking the opt-in mGBA
    # libretro core; neither library is copied into Unity plug-in assets.
    add_library(emuxr_mgba_libretro SHARED src/emuxr_mgba_libretro_adapter.c)
    target_include_directories(emuxr_mgba_libretro PUBLIC include)
    target_compile_features(emuxr_mgba_libretro PUBLIC c_std_11)
    target_link_libraries(emuxr_mgba_libretro PRIVATE
      emuxr_mgba_libretro_provider
      emuxr_gba_libretro_worker
      mgba_libretro)
    set_target_properties(emuxr_mgba_libretro PROPERTIES
      OUTPUT_NAME "emuxr_mgba_libretro")
    if(ANDROID)
      # Android 15+ ARM64 devices can use 16 KB pages. Apply the alignment to
      # both the mGBA core and the experimental adapter shared object.
      target_link_options(mgba_libretro PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
        # The direct adapter also contains a static mGBA. Bind references from
        # the experimental libretro core to its own copy so a loaded
        # libgba_xr.so cannot preempt mCore factory/config symbols.
        "-Wl,-Bsymbolic"
      )
      target_link_options(emuxr_mgba_libretro PRIVATE
        "-Wl,-z,max-page-size=16384"
        "-Wl,-z,common-page-size=16384"
        # Its private bridge implementation must not bind to gbaxr_* symbols
        # from a simultaneously loaded direct libgba_xr.so. The adapter-only
        # facade is the sole future Unity frame-consumer ABI.
        "-Wl,-Bsymbolic"
        # Hide bridge/provider/worker static-library symbols. Only the
        # explicit emuxr_mgba_libretro_adapter_* wrapper is exported.
        "-Wl,--exclude-libs,ALL"
      )
    endif()
    if(MSVC)
      target_compile_options(emuxr_mgba_libretro_provider PRIVATE /W4 /WX)
      target_compile_options(emuxr_mgba_libretro PRIVATE /W4 /WX)
    else()
      target_compile_options(emuxr_mgba_libretro_provider PRIVATE -Wall -Wextra -Wpedantic -Werror)
      target_compile_options(emuxr_mgba_libretro PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endif()

  if(GBAXR_BUILD_MGBA_ADAPTER)
  add_library(gba_xr_mgba_adapter SHARED
    src/mgba_adapter.c
    $<TARGET_OBJECTS:gba_xr_bridge_objects>
  )
  target_include_directories(gba_xr_mgba_adapter
    PUBLIC include
    PRIVATE "${MGBA_ROOT}/include" "${CMAKE_BINARY_DIR}/mgba/include"
  )
  target_compile_features(gba_xr_mgba_adapter PUBLIC c_std_11)
  # mCore is a public C struct whose layout depends on these mGBA feature
  # macros. They must match the static mgba target's compile definitions.
  # In particular, ENABLE_DIRECTORIES inserts mCore::dirs before its function
  # pointers; omitting it makes core->init call an invalid address on device.
  target_compile_definitions(gba_xr_mgba_adapter PRIVATE
    ENABLE_VFS
    ENABLE_DIRECTORIES
  )
  target_link_libraries(gba_xr_mgba_adapter PRIVATE mgba gba_xr_visible_partition gba_xr_independent_candidate gba_xr_audio_ring)
  set_target_properties(gba_xr_mgba_adapter PROPERTIES OUTPUT_NAME "gba_xr")
  if(ANDROID)
    # Unity 6000.3.12f1 ships NDK r27. Android 15+ 16 KB page-size devices
    # require every arm64 shared-library LOAD segment to use 16 KB alignment.
    # NDK r28 enables this by default; r27 and lower require these linker flags.
    target_link_options(gba_xr_mgba_adapter PRIVATE
      "-Wl,-z,max-page-size=16384"
      "-Wl,-z,common-page-size=16384"
    )
    target_link_libraries(gba_xr_mgba_adapter PRIVATE log)
  endif()
  endif()

  if(GBAXR_BUILD_MGBA_ADAPTER AND GBAXR_BUILD_MGBA_LIBRETRO_CORE)
    # Build-only, fixture-only parity runner. It is never copied into Unity or
    # invoked by Android XR; an authorized adb execution is a later step.
    add_executable(emuxr_mgba_parity_runner tools/emuxr_mgba_parity_runner.c)
    target_include_directories(emuxr_mgba_parity_runner PRIVATE include)
    target_compile_features(emuxr_mgba_parity_runner PRIVATE c_std_11)
    target_link_libraries(emuxr_mgba_parity_runner PRIVATE
      emuxr_mgba_parity
      gba_xr_mgba_adapter
      emuxr_mgba_libretro)
    if(MSVC)
      target_compile_options(emuxr_mgba_parity_runner PRIVATE /W4 /WX)
    else()
      target_compile_options(emuxr_mgba_parity_runner PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endif()
endif()

if(BUILD_TESTING)
  add_executable(gba_xr_reference_tests tests/reference_compositor_test.c)
  target_link_libraries(gba_xr_reference_tests PRIVATE gba_xr_reference)
  add_test(NAME gba_xr_reference_tests COMMAND gba_xr_reference_tests)

  find_package(Threads REQUIRED)
  add_executable(gba_xr_bridge_tests tests/bridge_test.c)
  target_link_libraries(gba_xr_bridge_tests PRIVATE gba_xr_bridge Threads::Threads)
  add_test(NAME gba_xr_bridge_tests COMMAND gba_xr_bridge_tests)

  add_executable(gba_xr_visible_partition_tests tests/visible_partition_test.c)
  target_link_libraries(gba_xr_visible_partition_tests PRIVATE gba_xr_visible_partition)
  add_test(NAME gba_xr_visible_partition_tests COMMAND gba_xr_visible_partition_tests)

  add_executable(gba_xr_independent_candidate_tests tests/independent_candidate_test.c)
  target_link_libraries(gba_xr_independent_candidate_tests PRIVATE gba_xr_independent_candidate)
  add_test(NAME gba_xr_independent_candidate_tests COMMAND gba_xr_independent_candidate_tests)
  add_executable(gba_xr_gba_blend_tests tests/gba_blend_test.c)
  target_link_libraries(gba_xr_gba_blend_tests PRIVATE gba_xr_gba_blend)
  add_test(NAME gba_xr_gba_blend_tests COMMAND gba_xr_gba_blend_tests)
  add_executable(gba_xr_audio_ring_tests tests/audio_ring_test.c)
  target_link_libraries(gba_xr_audio_ring_tests PRIVATE gba_xr_audio_ring)
  add_test(NAME gba_xr_audio_ring_tests COMMAND gba_xr_audio_ring_tests)
  add_executable(gba_xr_mgba_audio_defaults_tests tests/mgba_audio_defaults_test.c)
  target_include_directories(gba_xr_mgba_audio_defaults_tests PRIVATE include)
  add_test(NAME gba_xr_mgba_audio_defaults_tests COMMAND gba_xr_mgba_audio_defaults_tests)
  add_executable(gba_xr_mgba_state_file_policy_tests tests/mgba_state_file_policy_test.c)
  target_include_directories(gba_xr_mgba_state_file_policy_tests PRIVATE include)
  add_test(NAME gba_xr_mgba_state_file_policy_tests COMMAND gba_xr_mgba_state_file_policy_tests)
  add_executable(emuxr_libretro_host_tests tests/emuxr_libretro_host_test.c)
  target_link_libraries(emuxr_libretro_host_tests PRIVATE emuxr_libretro_host)
  add_test(NAME emuxr_libretro_host_tests COMMAND emuxr_libretro_host_tests)
  add_executable(emuxr_pcm_resampler_tests tests/emuxr_pcm_resampler_test.c)
  target_link_libraries(emuxr_pcm_resampler_tests PRIVATE emuxr_pcm_resampler)
  add_test(NAME emuxr_pcm_resampler_tests COMMAND emuxr_pcm_resampler_tests)
  add_executable(emuxr_gba_bridge_sink_tests tests/emuxr_gba_bridge_sink_test.c)
  target_link_libraries(emuxr_gba_bridge_sink_tests PRIVATE emuxr_gba_bridge_sink)
  add_test(NAME emuxr_gba_bridge_sink_tests COMMAND emuxr_gba_bridge_sink_tests)
  add_executable(emuxr_gba_libretro_worker_tests tests/emuxr_gba_libretro_worker_test.c)
  target_link_libraries(emuxr_gba_libretro_worker_tests PRIVATE emuxr_gba_libretro_worker Threads::Threads)
  add_test(NAME emuxr_gba_libretro_worker_tests COMMAND emuxr_gba_libretro_worker_tests)
  add_executable(emuxr_mgba_parity_tests tests/emuxr_mgba_parity_test.c)
  target_link_libraries(emuxr_mgba_parity_tests PRIVATE emuxr_mgba_parity)
  add_test(NAME emuxr_mgba_parity_tests COMMAND emuxr_mgba_parity_tests)
  if(GBAXR_BUILD_MGBA_LIBRETRO_CORE)
    add_executable(emuxr_mgba_libretro_provider_tests tests/emuxr_mgba_libretro_provider_test.c)
    target_link_libraries(emuxr_mgba_libretro_provider_tests PRIVATE emuxr_mgba_libretro_provider)
    add_test(NAME emuxr_mgba_libretro_provider_tests COMMAND emuxr_mgba_libretro_provider_tests)
    add_executable(emuxr_mgba_libretro_adapter_tests tests/emuxr_mgba_libretro_adapter_test.c)
    target_link_libraries(emuxr_mgba_libretro_adapter_tests PRIVATE emuxr_mgba_libretro)
    add_test(NAME emuxr_mgba_libretro_adapter_tests COMMAND emuxr_mgba_libretro_adapter_tests)
  endif()
endif()
