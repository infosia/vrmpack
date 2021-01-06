cmake_minimum_required( VERSION 3.0 )
project(vrmpack VERSION 0.1 LANGUAGES CXX)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(meshoptimizer
  meshoptimizer/src/meshoptimizer.h
  meshoptimizer/src/allocator.cpp
  meshoptimizer/src/clusterizer.cpp
  meshoptimizer/src/indexcodec.cpp
  meshoptimizer/src/indexgenerator.cpp
  meshoptimizer/src/overdrawanalyzer.cpp
  meshoptimizer/src/overdrawoptimizer.cpp
  meshoptimizer/src/simplifier.cpp
  meshoptimizer/src/spatialorder.cpp
  meshoptimizer/src/stripifier.cpp
  meshoptimizer/src/vcacheanalyzer.cpp
  meshoptimizer/src/vcacheoptimizer.cpp
  meshoptimizer/src/vertexcodec.cpp
  meshoptimizer/src/vertexfilter.cpp
  meshoptimizer/src/vfetchanalyzer.cpp
  meshoptimizer/src/vfetchoptimizer.cpp
)
set_target_properties(meshoptimizer PROPERTIES LINKER_LANGUAGE CXX)

set(vrmpack_FILES
  cgltf/cgltf.h
  cgltf/cgltf_write.h
  cgltf/vrm/vrm_types.v0_0.h
  cgltf/vrm/vrm_types.v0_0.inl
  cgltf/vrm/vrm_write.v0_0.inl
  src/vrmpack.cpp
  src/vrmpack.hpp
)

set(EXE_NAME vrmpack)
add_executable(${EXE_NAME} ${vrmpack_FILES})
set_property(TARGET ${EXE_NAME} PROPERTY CXX_STANDARD 11)

target_link_libraries(${EXE_NAME} meshoptimizer ${LIBS})

if(MSVC)
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-long-long -pedantic")
endif()
