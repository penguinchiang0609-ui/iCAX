cmake_minimum_required(VERSION 3.24)

project(occ_extrusion_toolpath VERSION 0.1.0 LANGUAGES CXX)

option(ETP_BUILD_TESTS "Build the standalone algorithm tests" ON)

find_package(OpenCASCADE REQUIRED COMPONENTS
  FoundationClasses
  ModelingData
  ModelingAlgorithms
  Visualization
  ApplicationFramework
  DataExchange
)

add_library(etp_core
  src/Analyzer.cxx
  src/CadIO.cxx
  src/GeometryUtils.cxx
  src/JsonWriter.cxx
  src/pipeline/FaceSplitClassifyWork.cxx
  src/pipeline/FeatureGroupingWork.cxx
  src/pipeline/NormalizeWork.cxx
  src/pipeline/Pipeline.cxx
  src/pipeline/PipelineGeometry.cxx
  src/pipeline/PreprocessWork.cxx
  src/pipeline/SectionProfileWork.cxx
  src/pipeline/SweepFaceDecomposeWork.cxx
  src/pipeline/ToolpathWork.cxx
  src/RulingToolpath.cxx
  src/SectionRegion.cxx
  src/SweepCompiler.cxx
)

target_compile_features(etp_core PUBLIC cxx_std_20)
target_include_directories(etp_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(etp_core
  PUBLIC
    TKernel
    TKMath
    TKG2d
    TKG3d
    TKGeomBase
    TKBRep
    TKGeomAlgo
    TKTopAlgo
    TKPrim
    TKFeat
    TKShHealing
    TKDE
    TKXSBase
    TKDESTEP
    TKDEIGES
    TKDECascade
)

if(MSVC)
  target_compile_options(etp_core PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /FS)
else()
  target_compile_options(etp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(etp_cli app/main.cxx)
target_link_libraries(etp_cli PRIVATE etp_core)

# OCCT is normally built as shared libraries. Copy the matching runtime next to
# the standalone executables when the package exposes a conventional install.
get_filename_component(_etp_occ_prefix "${OpenCASCADE_INCLUDE_DIR}" DIRECTORY)
set(_etp_occ_runtime "${_etp_occ_prefix}/win64/vc14/bin")
if(EXISTS "${_etp_occ_prefix}/win64/vc14/bind")
  set(_etp_occ_runtime "${_etp_occ_prefix}/win64/vc14/bind")
endif()

function(etp_copy_occ_runtime target_name)
  if(EXISTS "${_etp_occ_runtime}")
    file(GLOB _etp_occ_dlls CONFIGURE_DEPENDS "${_etp_occ_runtime}/*.dll")
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${_etp_occ_dlls}
              $<TARGET_FILE_DIR:${target_name}>
      COMMAND_EXPAND_LISTS
    )
  endif()
endfunction()

etp_copy_occ_runtime(etp_cli)

if(ETP_BUILD_TESTS)
  enable_testing()
  add_executable(etp_tests
    tests/AnalyzerTests.cxx
    tests/TestShapes.cxx
  )
  target_link_libraries(etp_tests PRIVATE etp_core)
  etp_copy_occ_runtime(etp_tests)
  add_test(NAME etp_tests COMMAND etp_tests)
endif()
