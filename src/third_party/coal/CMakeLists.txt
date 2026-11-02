cmake_minimum_required(VERSION 3.22)
project(ICAXCoal LANGUAGES CXX)
# Build the Coal 3.0.4 collision core. iCAX owns model IO and
# persistence, so Assimp, Boost archives, Python and Octomap are not linked.
set(coal_root "${CMAKE_CURRENT_SOURCE_DIR}/coal-3.0.4")
file(GLOB_RECURSE coal_sources CONFIGURE_DEPENDS "${coal_root}/src/*.cpp")
list(FILTER coal_sources EXCLUDE REGEX "/(mesh_loader|serialization)/|/octree.cpp$")
# Coal's specialized Minkowski dispatch selected LargeConvex using only point
# count, although that path requires neighbor topology. Point-support hulls are
# valid without topology and must use the exact linear support scan instead.
# Patch a build-local copy; preserve the downloaded, checksum-verified source.
file(READ "${coal_root}/src/narrowphase/minkowski_difference.cpp" support_dispatch)
foreach(i RANGE 0 1)
  set(before "if (static_cast<size_t>(convex${i}->num_points) >\n          ConvexBase::num_vertices_large_convex_threshold)")
  string(FIND "${support_dispatch}" "${before}" dispatch_match)
  if(dispatch_match EQUAL -1)
    message(FATAL_ERROR "Coal 3.0.4 support dispatch patch no longer matches")
  endif()
  string(REPLACE "${before}" "if (convex${i}->neighbors != nullptr && static_cast<size_t>(convex${i}->num_points) >\n          ConvexBase::num_vertices_large_convex_threshold)" support_dispatch "${support_dispatch}")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/minkowski_difference.cpp" "${support_dispatch}")
list(FILTER coal_sources EXCLUDE REGEX "/minkowski_difference.cpp$")
list(APPEND coal_sources "${CMAKE_CURRENT_BINARY_DIR}/minkowski_difference.cpp")
add_library(icax_coal STATIC ${coal_sources})
target_compile_features(icax_coal PUBLIC cxx_std_17)
target_include_directories(icax_coal PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/config" "${coal_root}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/eigen-3.4.0" "${CMAKE_CURRENT_SOURCE_DIR}/boost_1_87_0")
target_compile_definitions(icax_coal PUBLIC NOMINMAX COAL_TURN_ASSERT_INTO_EXCEPTION BOOST_ALL_NO_LIB _ENABLE_EXTENDED_ALIGNED_STORAGE)
if(MSVC)
  # Keep the caller's CRT/STL ABI in both configurations while optimizing the
  # geometry kernel. Never link a release-CRT binary into the debug product.
  target_compile_options(icax_coal PRIVATE /O2 /Ob2 /bigobj /MP4 /utf-8 /wd4996 /wd4244 /wd4267)
endif()
install(TARGETS icax_coal ARCHIVE DESTINATION lib)
