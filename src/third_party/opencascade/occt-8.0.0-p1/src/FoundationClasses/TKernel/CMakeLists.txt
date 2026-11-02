project(TKernel)

OCCT_INCLUDE_CMAKE_FILE (adm/cmake/occt_toolkit)

# Set desired Memory Manager
if     ("${USE_MMGR_TYPE}" STREQUAL "FLEXIBLE")
  # Recycle small geometry allocations instead of serializing each one through
  # the Debug CRT heap. MMGT_OPT can still select another built-in manager.
  target_compile_definitions("TKernel" PRIVATE "OCCT_MMGT_OPT_FLEXIBLE" "OCCT_MMGT_OPT_DEFAULT=1")
elseif ("${USE_MMGR_TYPE}" STREQUAL "TBB")
  target_compile_definitions("TKernel" PRIVATE "OCCT_MMGT_OPT_TBB")
elseif ("${USE_MMGR_TYPE}" STREQUAL "JEMALLOC")
  target_compile_definitions("TKernel" PRIVATE "OCCT_MMGT_OPT_JEMALLOC")
else ()
  message (STATUS "Info: Used native memory manager")
endif()

# Add the precompiled header
ADD_PRECOMPILED_HEADER(TKernel "TKernel_pch.hxx" FALSE)
