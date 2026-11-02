cmake_minimum_required(VERSION 3.24)
project(iCAXSheetNesting LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(ICAX_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CLIPPER_ROOT "${ICAX_SOURCE_ROOT}/third_party/fixture-geometry/Clipper2-Clipper2_1.5.4/CPP/Clipper2Lib")
if(NOT EXISTS "${CLIPPER_ROOT}/include/clipper2/clipper.h")
  message(FATAL_ERROR "Prepare the repository's pinned Clipper2 1.5.4 dependency first.")
endif()
add_library(SheetNestingClipper STATIC
  "${CLIPPER_ROOT}/src/clipper.engine.cpp"
  "${CLIPPER_ROOT}/src/clipper.offset.cpp"
  "${CLIPPER_ROOT}/src/clipper.rectclip.cpp")
target_include_directories(SheetNestingClipper PUBLIC "${CLIPPER_ROOT}/include")
# Execution owns jobs, pipeline guards and selection; it never dispatches on algorithm IDs.
add_library(SheetNesting STATIC
  NestingJobs.cpp
  NestingPipeline.cpp
  NestingRectangleRefill.cpp
  NestingCircleRelocation.cpp
  NestingConservativeRaster.cpp
  NestingEvictionRepair.cpp
  NestingTriangleSeparationRepair.cpp
  NestingStrategies.cpp
  NestingAssessment.cpp
  NestingRanking.cpp
  # Geometry, legality and regular-remnant measurement are shared services.
  NestingGeometry.cpp
  NestingNfp.cpp
  NestingNfpCombinations.cpp
  NestingValidation.cpp
  NestingRemnants.cpp
  # Construction algorithms and their local planning helpers.
  NestingPatterns.cpp
  NestingRectangles.cpp
  NestingRectangleOrdered.cpp
  NestingRectangleCavity.cpp
  NestingRectangleSkyline.cpp
  NestingRectanglePinwheel.cpp
  NestingRectangleBlockLibrary.cpp
  NestingRectangleBlockRollout.cpp
  NestingRectangleResidualCorners.cpp
  NestingRectangleRepairRegions.cpp
  NestingRectangleStripeRepair.cpp
  NestingRectangleStripeRegions.cpp
  NestingRectangleRepeatCell.cpp
  NestingSolver.cpp
  NestingCircles.cpp
  NestingParallelograms.cpp
  NestingContactRules.cpp
  NestingDomainSearch.cpp
  NestingContactPrecision.cpp
  NestingExactFill.cpp
  NestingRectangleSearch.cpp
  NestingRemnantSearch.cpp
  NestingParallelogramSearch.cpp
  NestingComplementChains.cpp
  NestingCircleShelves.cpp
  NestingSeamAssembly.cpp
  NestingRectangleMerge.cpp
  NestingRectangleLookahead.cpp
  NestingMixedAssemblyRules.cpp)

target_include_directories(SheetNesting PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SheetNesting PRIVATE SheetNestingClipper)
if(MSVC)
  target_compile_options(SheetNesting PRIVATE /W4 /utf-8 /permissive-)
endif()
include(CTest)
if(BUILD_TESTING)
  set(TEST_ROOT "${ICAX_SOURCE_ROOT}/tests/icax-engine/foundation/SheetNesting")
  add_executable(NfpCacheTests "${TEST_ROOT}/NfpCacheTests.cpp")
  target_link_libraries(NfpCacheTests PRIVATE SheetNesting)
  add_test(NAME NfpCacheTests COMMAND NfpCacheTests)
  add_executable(SheetNestingTests "${TEST_ROOT}/SheetNestingTests.cpp")
  target_link_libraries(SheetNestingTests PRIVATE SheetNesting)
  add_test(NAME SheetNestingTests COMMAND SheetNestingTests)
  add_executable(ExactFillContactTests "${TEST_ROOT}/ExactFillContactTests.cpp")
  target_link_libraries(ExactFillContactTests PRIVATE SheetNesting)
  add_test(NAME ExactFillContactTests COMMAND ExactFillContactTests)
  add_executable(BoundedContactTests "${TEST_ROOT}/BoundedContactTests.cpp")
  target_link_libraries(BoundedContactTests PRIVATE SheetNesting)
  add_test(NAME BoundedContactTests COMMAND BoundedContactTests)
  add_executable(DomainSearchTests "${TEST_ROOT}/DomainSearchTests.cpp")
  target_link_libraries(DomainSearchTests PRIVATE SheetNesting)
  add_test(NAME DomainSearchTests COMMAND DomainSearchTests)
  add_executable(ContactPrecisionTests "${TEST_ROOT}/ContactPrecisionTests.cpp")
  target_link_libraries(ContactPrecisionTests PRIVATE SheetNesting)
  add_test(NAME ContactPrecisionTests COMMAND ContactPrecisionTests)
  add_executable(CircleRelocationTests "${TEST_ROOT}/CircleRelocationTests.cpp")
  target_link_libraries(CircleRelocationTests PRIVATE SheetNesting)
  add_test(NAME CircleRelocationTests COMMAND CircleRelocationTests)
  add_executable(ConservativeRasterTests "${TEST_ROOT}/ConservativeRasterTests.cpp")
  target_link_libraries(ConservativeRasterTests PRIVATE SheetNesting)
  add_test(NAME ConservativeRasterTests COMMAND ConservativeRasterTests)
  add_executable(EvictionRepairTests "${TEST_ROOT}/EvictionRepairTests.cpp")
  target_link_libraries(EvictionRepairTests PRIVATE SheetNesting)
  add_test(NAME EvictionRepairTests COMMAND EvictionRepairTests)
  add_executable(TriangleSeparationRepairTests "${TEST_ROOT}/TriangleSeparationRepairTests.cpp")
  target_link_libraries(TriangleSeparationRepairTests PRIVATE SheetNesting)
  add_test(NAME TriangleSeparationRepairTests COMMAND TriangleSeparationRepairTests)
  add_executable(NestingRectanglesTests "${TEST_ROOT}/NestingRectanglesTests.cpp")
  target_link_libraries(NestingRectanglesTests PRIVATE SheetNesting)
  add_test(NAME NestingRectanglesTests COMMAND NestingRectanglesTests)
  add_executable(RectangleOrderedTests "${TEST_ROOT}/RectangleOrderedTests.cpp")
  target_link_libraries(RectangleOrderedTests PRIVATE SheetNesting)
  add_test(NAME RectangleOrderedTests COMMAND RectangleOrderedTests)
  add_executable(RectangleSkylineTests "${TEST_ROOT}/RectangleSkylineTests.cpp")
  target_link_libraries(RectangleSkylineTests PRIVATE SheetNesting)
  add_test(NAME RectangleSkylineTests COMMAND RectangleSkylineTests)
  add_executable(RectangleCavityTests "${TEST_ROOT}/RectangleCavityTests.cpp")
  target_link_libraries(RectangleCavityTests PRIVATE SheetNesting)
  add_test(NAME RectangleCavityTests COMMAND RectangleCavityTests)
  add_executable(RectanglePinwheelTests "${TEST_ROOT}/RectanglePinwheelTests.cpp")
  target_link_libraries(RectanglePinwheelTests PRIVATE SheetNesting)
  add_test(NAME RectanglePinwheelTests COMMAND RectanglePinwheelTests)
  add_executable(RectangleBlockLibraryTests "${TEST_ROOT}/RectangleBlockLibraryTests.cpp")
  target_link_libraries(RectangleBlockLibraryTests PRIVATE SheetNesting)
  add_test(NAME RectangleBlockLibraryTests COMMAND RectangleBlockLibraryTests)
  add_executable(RectangleBlockLibraryMrvTests "${TEST_ROOT}/RectangleBlockLibraryMrvTests.cpp")
  target_link_libraries(RectangleBlockLibraryMrvTests PRIVATE SheetNesting)
  add_test(NAME RectangleBlockLibraryMrvTests COMMAND RectangleBlockLibraryMrvTests)
  set_tests_properties(RectangleBlockLibraryMrvTests PROPERTIES TIMEOUT 180)
  add_executable(RectangleBlockRolloutTests "${TEST_ROOT}/RectangleBlockRolloutTests.cpp")
  target_link_libraries(RectangleBlockRolloutTests PRIVATE SheetNesting)
  add_test(NAME RectangleBlockRolloutTests COMMAND RectangleBlockRolloutTests)
  set_tests_properties(RectangleBlockRolloutTests PROPERTIES TIMEOUT 600)
  add_executable(RectangleBlockRolloutDiverseTests "${TEST_ROOT}/RectangleBlockRolloutDiverseTests.cpp")
  target_link_libraries(RectangleBlockRolloutDiverseTests PRIVATE SheetNesting)
  add_test(NAME RectangleBlockRolloutDiverseTests COMMAND RectangleBlockRolloutDiverseTests)
  set_tests_properties(RectangleBlockRolloutDiverseTests PROPERTIES TIMEOUT 600)
  add_executable(RectangleBlockResidualTests "${TEST_ROOT}/RectangleBlockResidualTests.cpp")
  target_link_libraries(RectangleBlockResidualTests PRIVATE SheetNesting)
  add_test(NAME RectangleBlockResidualTests COMMAND RectangleBlockResidualTests)
  set_tests_properties(RectangleBlockResidualTests PROPERTIES TIMEOUT 2000)
  add_executable(RectangleBlockRepairTests "${TEST_ROOT}/RectangleBlockRepairTests.cpp")
  target_link_libraries(RectangleBlockRepairTests PRIVATE SheetNesting)
  add_test(NAME RectangleBlockRepairTests COMMAND RectangleBlockRepairTests)
  set_tests_properties(RectangleBlockRepairTests PROPERTIES TIMEOUT 6000)
  add_executable(RectangleStripeRepairTests "${TEST_ROOT}/RectangleStripeRepairTests.cpp")
  target_link_libraries(RectangleStripeRepairTests PRIVATE SheetNesting)
  add_test(NAME RectangleStripeRepairTests COMMAND RectangleStripeRepairTests)
  set_tests_properties(RectangleStripeRepairTests PROPERTIES TIMEOUT 300)
  add_executable(RectangleExactGridTests "${TEST_ROOT}/RectangleExactGridTests.cpp")
  target_link_libraries(RectangleExactGridTests PRIVATE SheetNesting)
  add_test(NAME RectangleExactGridTests COMMAND RectangleExactGridTests)
  set_tests_properties(RectangleExactGridTests PROPERTIES TIMEOUT 180)
  add_executable(RectangleStripeRegionsTests "${TEST_ROOT}/RectangleStripeRegionsTests.cpp")
  target_link_libraries(RectangleStripeRegionsTests PRIVATE SheetNesting)
  add_test(NAME RectangleStripeRegionsTests COMMAND RectangleStripeRegionsTests)
  set_tests_properties(RectangleStripeRegionsTests PROPERTIES TIMEOUT 300)
  add_executable(RectangleRepeatCellTests "${TEST_ROOT}/RectangleRepeatCellTests.cpp")
  target_link_libraries(RectangleRepeatCellTests PRIVATE SheetNesting)
  add_test(NAME RectangleRepeatCellTests COMMAND RectangleRepeatCellTests)
  set_tests_properties(RectangleRepeatCellTests PROPERTIES TIMEOUT 180)
  add_executable(MixedSpecificationTests "${TEST_ROOT}/MixedSpecificationTests.cpp")
  target_link_libraries(MixedSpecificationTests PRIVATE SheetNesting)
  add_test(NAME MixedSpecificationTests COMMAND MixedSpecificationTests)
  add_executable(ContactRuleTests "${TEST_ROOT}/ContactRuleTests.cpp")
  target_link_libraries(ContactRuleTests PRIVATE SheetNesting)
  add_test(NAME ContactRuleTests COMMAND ContactRuleTests)
  add_executable(RemnantQualityTests "${TEST_ROOT}/RemnantQualityTests.cpp")
  target_link_libraries(RemnantQualityTests PRIVATE SheetNesting)
  add_test(NAME RemnantQualityTests COMMAND RemnantQualityTests)
  add_executable(ParallelogramShelfTests "${TEST_ROOT}/ParallelogramShelfTests.cpp")
  target_link_libraries(ParallelogramShelfTests PRIVATE SheetNesting)
  add_test(NAME ParallelogramShelfTests COMMAND ParallelogramShelfTests)
  add_executable(ScaledRuleTests "${TEST_ROOT}/ScaledRuleTests.cpp")
  target_link_libraries(ScaledRuleTests PRIVATE SheetNesting)
  add_test(NAME ScaledRuleTests COMMAND ScaledRuleTests)
  add_executable(ComplementChainTests "${TEST_ROOT}/ComplementChainTests.cpp")
  target_link_libraries(ComplementChainTests PRIVATE SheetNesting)
  add_test(NAME ComplementChainTests COMMAND ComplementChainTests)
  add_executable(CompositeFitTests "${TEST_ROOT}/CompositeFitTests.cpp")
  target_link_libraries(CompositeFitTests PRIVATE SheetNesting)
  add_test(NAME CompositeFitTests COMMAND CompositeFitTests)
  add_executable(ClosurePatternTests "${TEST_ROOT}/ClosurePatternTests.cpp")
  target_link_libraries(ClosurePatternTests PRIVATE SheetNesting)
  add_test(NAME ClosurePatternTests COMMAND ClosurePatternTests)
  add_executable(SeamAssemblyTests "${TEST_ROOT}/SeamAssemblyTests.cpp")
  target_link_libraries(SeamAssemblyTests PRIVATE SheetNesting)
  add_test(NAME SeamAssemblyTests COMMAND SeamAssemblyTests)
  add_executable(RectangleMergeTests "${TEST_ROOT}/RectangleMergeTests.cpp")
  target_link_libraries(RectangleMergeTests PRIVATE SheetNesting)
  add_test(NAME RectangleMergeTests COMMAND RectangleMergeTests)
  add_executable(RectangleLookaheadTests "${TEST_ROOT}/RectangleLookaheadTests.cpp")
  target_link_libraries(RectangleLookaheadTests PRIVATE SheetNesting)
  add_test(NAME RectangleLookaheadTests COMMAND RectangleLookaheadTests)
  add_executable(MixedAssemblyRulesTests "${TEST_ROOT}/MixedAssemblyRulesTests.cpp")
  target_link_libraries(MixedAssemblyRulesTests PRIVATE SheetNesting)
  add_test(NAME MixedAssemblyRulesTests COMMAND MixedAssemblyRulesTests)
  add_executable(PipelineTests "${TEST_ROOT}/PipelineTests.cpp")
  target_link_libraries(PipelineTests PRIVATE SheetNesting)
  add_test(NAME PipelineTests COMMAND PipelineTests)
  add_executable(RectangleRefillTests "${TEST_ROOT}/RectangleRefillTests.cpp")
  target_link_libraries(RectangleRefillTests PRIVATE SheetNesting)
  add_test(NAME RectangleRefillTests COMMAND RectangleRefillTests)
  add_executable(SpatialIndexTests "${TEST_ROOT}/SpatialIndexTests.cpp")
  target_link_libraries(SpatialIndexTests PRIVATE SheetNesting)
  add_test(NAME SpatialIndexTests COMMAND SpatialIndexTests)
  add_executable(BatchRuleTests "${TEST_ROOT}/BatchRuleTests.cpp")
  target_link_libraries(BatchRuleTests PRIVATE SheetNesting)
  add_test(NAME BatchRuleTests COMMAND BatchRuleTests)
  add_executable(InterlockPatternTests "${TEST_ROOT}/InterlockPatternTests.cpp")
  target_link_libraries(InterlockPatternTests PRIVATE SheetNesting)
  add_test(NAME InterlockPatternTests COMMAND InterlockPatternTests)
  add_executable(CircleShelfTests "${TEST_ROOT}/CircleShelfTests.cpp")
  target_link_libraries(CircleShelfTests PRIVATE SheetNesting)
  add_test(NAME CircleShelfTests COMMAND CircleShelfTests)
  add_executable(MixedSpecificationDemo "${TEST_ROOT}/MixedSpecificationDemo.cpp")
  target_link_libraries(MixedSpecificationDemo PRIVATE SheetNesting)
  add_executable(SheetNestingDemo "${TEST_ROOT}/SheetNestingDemo.cpp")
  target_link_libraries(SheetNestingDemo PRIVATE SheetNesting)
  add_executable(SheetNestingFakeBench "${TEST_ROOT}/SheetNestingFakeBench.cpp")
  target_link_libraries(SheetNestingFakeBench PRIVATE SheetNesting)
  add_executable(SheetNestingReplay "${TEST_ROOT}/SheetNestingReplay.cpp")
  target_link_libraries(SheetNestingReplay PRIVATE SheetNesting)
  if(MSVC)
    target_compile_options(SheetNestingTests PRIVATE /utf-8)
    target_compile_options(NestingRectanglesTests PRIVATE /utf-8)
    target_compile_options(MixedSpecificationTests PRIVATE /utf-8)
    target_compile_options(ContactRuleTests PRIVATE /utf-8)
    target_compile_options(RemnantQualityTests PRIVATE /utf-8)
    target_compile_options(ParallelogramShelfTests PRIVATE /utf-8)
    target_compile_options(ScaledRuleTests PRIVATE /utf-8)
    target_compile_options(ComplementChainTests PRIVATE /utf-8)
    target_compile_options(CircleShelfTests PRIVATE /utf-8)
    target_compile_options(CompositeFitTests PRIVATE /utf-8)
    target_compile_options(ClosurePatternTests PRIVATE /utf-8)
    target_compile_options(SeamAssemblyTests PRIVATE /utf-8)
    target_compile_options(RectangleMergeTests PRIVATE /utf-8)
    target_compile_options(RectangleLookaheadTests PRIVATE /utf-8)
    target_compile_options(MixedAssemblyRulesTests PRIVATE /utf-8)
    target_compile_options(PipelineTests PRIVATE /utf-8)
    target_compile_options(RectangleRefillTests PRIVATE /utf-8)
    target_compile_options(SpatialIndexTests PRIVATE /utf-8)
    target_compile_options(BatchRuleTests PRIVATE /utf-8)
    target_compile_options(InterlockPatternTests PRIVATE /utf-8)
    target_compile_options(MixedSpecificationDemo PRIVATE /utf-8)
    target_compile_options(SheetNestingDemo PRIVATE /utf-8)
    target_compile_options(SheetNestingFakeBench PRIVATE /utf-8)
    target_compile_options(SheetNestingReplay PRIVATE /utf-8)
  endif()
endif()
