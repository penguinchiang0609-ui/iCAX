cmake_minimum_required(VERSION 3.24)
project(iPlaneNesting VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive- /utf-8 /EHsc /fp:precise)
endif()

option(IPLANE_NESTING_BUILD_TESTS "Build the independent native regression tests" ${PROJECT_IS_TOP_LEVEL})
option(IPLANE_NESTING_BUILD_EXAMPLES "Build the public C and C++ examples" ${PROJECT_IS_TOP_LEVEL})

include(cmake/Kernel.cmake)
add_library(iCAX::iPlaneNesting ALIAS iPlaneNesting)
set_target_properties(iPlaneNesting PROPERTIES EXPORT_NAME iPlaneNesting)

if(IPLANE_NESTING_BUILD_TESTS)
  enable_testing()
  include(cmake/Tests.cmake)
elseif(IPLANE_NESTING_BUILD_EXAMPLES)
  include(cmake/Examples.cmake)
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/iPlaneNestingConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/iPlaneNestingConfig.cmake"
  INSTALL_DESTINATION lib/cmake/iPlaneNesting)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iPlaneNestingConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(TARGETS iPlaneNesting EXPORT iPlaneNestingTargets
  RUNTIME DESTINATION bin COMPONENT iPlaneNesting
  LIBRARY DESTINATION lib COMPONENT iPlaneNesting
  ARCHIVE DESTINATION lib COMPONENT iPlaneNesting)
install(FILES include/iPlaneNesting.h include/alma_study.h DESTINATION include COMPONENT iPlaneNesting)
install(EXPORT iPlaneNestingTargets FILE iPlaneNestingTargets.cmake
  NAMESPACE iCAX:: DESTINATION lib/cmake/iPlaneNesting COMPONENT iPlaneNesting)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iPlaneNestingConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/iPlaneNestingConfigVersion.cmake"
  DESTINATION lib/cmake/iPlaneNesting COMPONENT iPlaneNesting)
install(DIRECTORY examples/ DESTINATION examples COMPONENT iPlaneNesting
  FILES_MATCHING PATTERN "*.c" PATTERN "*.cpp")
install(FILES third_party/clipper2/LICENSE DESTINATION licenses/Clipper2 COMPONENT iPlaneNesting)
install(FILES third_party/HiGHS-1.7.2/LICENSE.txt DESTINATION licenses/HiGHS COMPONENT iPlaneNesting)
install(FILES third_party/HiGHS-1.7.2/extern/filereaderlp/LICENSE
  DESTINATION licenses/HiGHS/filereaderlp COMPONENT iPlaneNesting)
install(FILES third_party/HiGHS-1.7.2/extern/LICENCE_1_0.txt
  DESTINATION licenses/HiGHS/extern COMPONENT iPlaneNesting)
install(FILES third_party/boost-1.87.0/LICENSE_1_0.txt DESTINATION licenses/Boost COMPONENT iPlaneNesting)
