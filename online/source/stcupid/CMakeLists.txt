cmake_minimum_required(VERSION 3.5)
project(rtp_fe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#
# Find MIDAS (same logic as your example)
#
if (NO_MIDAS)
  message(STATUS "${PROJECT_NAME}: MIDAS support is disabled via NO_MIDAS")
  unset(MIDAS_FOUND)
elseif(TARGET midas::mfe)
  message(STATUS "${PROJECT_NAME}: Building as a subproject of MIDAS")
  set(MIDAS_FOUND true)
  set(MIDASSYS ${CMAKE_HOME_DIRECTORY})
elseif(DEFINED ENV{MIDASSYS})
  include($ENV{MIDASSYS}/lib/midas-targets.cmake)
  # manalyzer not needed for a frontend, keep only if you use it
  # include($ENV{MIDASSYS}/lib/manalyzer-targets.cmake)
  message(STATUS "${PROJECT_NAME}: Building with MIDAS from $ENV{MIDASSYS}/lib/midas-targets.cmake")
  set(MIDAS_FOUND TRUE)
  set(MIDASSYS $ENV{MIDASSYS})
else()
  message(STATUS "${PROJECT_NAME}: Building without MIDAS (set MIDASSYS env var)")
  unset(MIDAS_FOUND)
endif()

message(STATUS "${PROJECT_NAME}: MIDASSYS: ${MIDASSYS}")

if (NOT MIDAS_FOUND)
  message(FATAL_ERROR "MIDAS not found. Set MIDASSYS or build as a subproject of MIDAS.")
endif()

#
# Build the frontend
#
add_executable(fe_rtp
  fe_rtp_multistream.cxx
)

# Some MIDAS builds require these defines; harmless if not used.
target_compile_definitions(fe_rtp PRIVATE
  HAVE_MIDAS
)
target_compile_options(fe_rtp PRIVATE -Wno-missing-field-initializers)


# Warnings (tune as you like)
target_compile_options(fe_rtp PRIVATE
  -Wall -Wextra -Wpedantic
)

# Link MIDAS frontend framework
target_link_libraries(fe_rtp
  PRIVATE
    midas::mfe
)

# If your MIDAS setup requires explicit midas core as well, uncomment:
# target_link_libraries(fe_rtp.exe PRIVATE midas::midas)

# For Linux sockets/pthreads: usually pulled in, but safe:
find_package(Threads REQUIRED)
target_link_libraries(fe_rtp PRIVATE Threads::Threads)
