cmake_minimum_required(VERSION 3.5)
project(glview)

set ( CMAKE_PREFIX_PATH cmake )
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a)
set(CMAKE_ANDROID_API_MIN 21)

set ( DRACO_DIR "" CACHE STRING "Path to draco" )
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../app/src/main/jniLibs/arm64-v8a)
#/Users/andy.d.hu/git/mediapipe/examples/face_landmarker/android/app/src/main/jniLibs/arm64-v8a/libglview.so
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/main/jniLibs/${ANDROID_ABI})

#set(CMAKE_PREFIX_PATH "/usr/local/Cellar/glew/2.2.0_1/lib/cmake/glew/")
#find_path(glew_DIR lib/cmake/glew/glew-config.cmake "$ENV{GLEW_HOME}")
#set(glew_DIR '/usr/local/Cellar/glew/2.2.0_1/lib/cmake/glew/')
#message("glew_DIR = ${glew_DIR})")
#find_package(glew REQUIRED PATHS ${glew_DIR})

#find_package(GLFW3 REQUIRED )
#find_package(OpenGL REQUIRED )

find_library(
  GLESv3-lib
  GLESv3
)

find_package (Threads)
set(CMAKE_CXX_STANDARD 11)

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
  # ${OPENGL_INCLUDE_DIR}
  #${GLFW3_INCLUDE_DIR}
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
