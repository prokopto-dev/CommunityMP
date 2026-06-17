project(masterserver)

include_directories("./")

set(SOURCE_FILES main.cpp MasterServer.cpp MasterServer.hpp RestServer.cpp RestServer.hpp)

add_executable(masterserver ${SOURCE_FILES})
set_target_properties(masterserver PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_link_libraries(masterserver components)
tes3mp_copy_runtime_dlls(masterserver)

install(TARGETS masterserver RUNTIME DESTINATION ".")

option(BUILD_MASTER_TEST "build master server test program" OFF)

if(BUILD_MASTER_TEST)
    add_executable(ServerTest ServerTest.cpp)
    set_target_properties(ServerTest PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_link_libraries(ServerTest components)
endif()

if (UNIX)
    # Fix for not visible pthreads functions for linker with glibc 2.15
    if(NOT APPLE)
        target_link_libraries(masterserver ${CMAKE_THREAD_LIBS_INIT})
        if(BUILD_MASTER_TEST)
            target_link_libraries(ServerTest ${CMAKE_THREAD_LIBS_INIT})
        endif()
    endif(NOT APPLE)
endif(UNIX)

if(WIN32)
    target_link_libraries(masterserver ws2_32)
endif(WIN32)
