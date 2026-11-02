cmake_minimum_required(VERSION 3.22)
project(ICAXOmpl VERSION 2.0.2 LANGUAGES CXX)
set(deps "${CMAKE_CURRENT_SOURCE_DIR}/../coal")
set(source "${CMAKE_CURRENT_SOURCE_DIR}/ompl-2.0.2")
set(OMPL_HAVE_FLANN 0)
set(OMPL_HAVE_SPOT 0)
set(OMPL_HAVE_NUMPY 0)
configure_file("${source}/src/ompl/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/include/ompl/config.h")
# Same unmodified core directories as upstream src/ompl/CMakeLists.txt.
# iCAX supplies collision geometry; VAMP, demos and Python are not needed.
file(GLOB_RECURSE ompl_sources CONFIGURE_DEPENDS
  "${source}/src/ompl/base/*.cpp" "${source}/src/ompl/util/*.cpp"
  "${source}/src/ompl/datastructures/*.cpp" "${source}/src/ompl/geometric/*.cpp"
  "${source}/src/ompl/control/*.cpp" "${source}/src/ompl/multilevel/*.cpp"
  "${source}/src/ompl/tools/*.cpp")
file(GLOB serialization_sources "${deps}/boost_1_87_0/libs/serialization/src/*.cpp")
add_library(icax_ompl_serialization STATIC ${serialization_sources})
add_library(icax_ompl STATIC ${ompl_sources})
foreach(target icax_ompl icax_ompl_serialization)
  target_compile_features(${target} PUBLIC cxx_std_17)
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include"
    "${source}/src" "${deps}/boost_1_87_0" "${deps}/eigen-3.4.0")
  target_compile_definitions(${target} PUBLIC NOMINMAX BOOST_ALL_NO_LIB _ENABLE_EXTENDED_ALIGNED_STORAGE _HAS_AUTO_PTR_ETC=0)
  if(MSVC)
    target_compile_options(${target} PRIVATE /O2 /Ob2 /MP4 /bigobj /utf-8 /wd4996 /wd4244 /wd4267)
  endif()
endforeach()
target_link_libraries(icax_ompl PUBLIC icax_ompl_serialization)
install(TARGETS icax_ompl icax_ompl_serialization ARCHIVE DESTINATION lib)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/ompl/config.h" DESTINATION include/ompl)
