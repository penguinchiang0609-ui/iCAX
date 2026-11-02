cmake_minimum_required(VERSION 3.22)
project(ICAXCollisionMeshVolumesTests LANGUAGES CXX)
get_filename_component(ICAX_REPOSITORY "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_executable(CollisionMeshVolumesTests ../CollisionMeshVolumesTests.cpp)
target_compile_features(CollisionMeshVolumesTests PRIVATE cxx_std_20)
target_include_directories(CollisionMeshVolumesTests PRIVATE
    "${ICAX_REPOSITORY}/src/iCAX-Engine/framework"
    "${ICAX_REPOSITORY}/src/iCAX-Engine/foundation")
target_compile_definitions(CollisionMeshVolumesTests PRIVATE NOMINMAX)
if(MSVC)
    target_compile_options(CollisionMeshVolumesTests PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME CollisionMeshVolumesTests COMMAND CollisionMeshVolumesTests "${ICAX_REPOSITORY}")
