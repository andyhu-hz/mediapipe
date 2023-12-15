cmake_minimum_required(VERSION 3.5)
project(glview)

set ( CMAKE_PREFIX_PATH cmake )
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_API_MIN 21)

set ( DRACO_DIR "" CACHE STRING "Path to draco" )
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../app/src/main/jniLibs/arm64-v8a)

find_library(
  GLESv3-lib
  GLESv3
)

find_package (Threads)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

if (DEFINED DRACO_DIR)
  if (DRACO_DIR STREQUAL "")
  else ()
    # TODO(syoyo): better CMake script for draco
    add_definitions(-DTINYGLTF_ENABLE_DRACO)
    include_directories(${DRACO_DIR}/include)

    link_directories(${DRACO_DIR}/lib)
    set(DRACO_LIBRARY draco)
  endif ()
endif()

include_directories(
  ../../
  ../common
  )

add_library(
        glview
        SHARED
        glview.cc
        ../common/trackball.cc
  )

find_library(log-lib log)

target_link_libraries ( glview
        EGL
        GLESv3
        android
        ${log-lib}
  ${DRACO_LIBRARY}
  ${GLFW3_UNIX_LINK_LIBRARIES}
  ${GLESv3-lib}
  ${GLFW3_glfw_LIBRARY}
  ${OPENGL_gl_LIBRARY}
  ${OPENGL_glu_LIBRARY}
  ${COCOA_LIBRARY}
  ${COREVIDEO_LIBRARY}
  ${IOKIT_LIBRARY}
  ${CMAKE_DL_LIBS}
  ${CMAKE_THREAD_LIBS_INIT}
  )

install ( TARGETS
  glview
  DESTINATION
  bin
  )
