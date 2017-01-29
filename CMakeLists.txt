cmake_minimum_required(VERSION 2.8)
project(libcoco)

enable_language(C ASM)

set(CMAKE_BUILD_TYPE "Debug")
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -export-dynamic -g -fno-strict-aliasing -ggdb -O0 -std=c99 -Wall -pipe  -D_GNU_SOURCE -D_REENTRANT -fPIC -Wno-deprecated -m64")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(include)

set_source_files_properties(src/cc_ctx_swap.S PROPERTIES COMPILE_FLAGS "-x assembler-with-cpp")
set(DIR_SRCS src/cc_ctx.c src/cc_routine.c src/cc_ctx_swap.S src/cc_epoll.c src/cc_hook_sys_call.c)

set(LINK_LIBS dl)
if(CMAKE_THREAD_LIBS_INIT)
    set(LINK_LIBS ${LINK_LIBS} "${CMAKE_THREAD_LIBS_INIT}")
endif()

add_library(coco ${DIR_SRCS})
if(THREADS_HAVE_PTHREAD_ARG)
    target_compile_options(coco PUBLIC "-pthread")
endif()
target_link_libraries(coco ${LINK_LIBS})

add_executable(test_cond test/test_cond.c)
target_link_libraries(test_cond coco)


