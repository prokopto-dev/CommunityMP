if(WIN32)
    find_program(COMMUNITYMP_NPM_EXECUTABLE NAMES npm.cmd REQUIRED)
else()
    find_program(COMMUNITYMP_NPM_EXECUTABLE NAMES npm REQUIRED)
endif()

set(COMMUNITYMP_HUB_SOURCE_DIR "${CMAKE_SOURCE_DIR}/apps/communitymp-hub")
set(COMMUNITYMP_HUB_TAURI_DIR "${COMMUNITYMP_HUB_SOURCE_DIR}/src-tauri")
set(COMMUNITYMP_HUB_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/communitymp-hub-cargo")
set(COMMUNITYMP_HUB_BUILT_EXE
    "${COMMUNITYMP_HUB_CARGO_TARGET_DIR}/release/communitymp-hub${CMAKE_EXECUTABLE_SUFFIX}")

if(CMAKE_CONFIGURATION_TYPES)
    set(COMMUNITYMP_HUB_RUNTIME_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
    set(COMMUNITYMP_HUB_RUNTIME_DIR "${CMAKE_BINARY_DIR}")
endif()

set(COMMUNITYMP_HUB_RUNTIME_EXE
    "${COMMUNITYMP_HUB_RUNTIME_DIR}/communitymp-hub${CMAKE_EXECUTABLE_SUFFIX}")

add_custom_target(communitymp-hub ALL
    COMMAND ${COMMUNITYMP_NPM_EXECUTABLE} ci
    COMMAND ${CMAKE_COMMAND} -E env
        "CARGO_TARGET_DIR=${COMMUNITYMP_HUB_CARGO_TARGET_DIR}"
        ${COMMUNITYMP_NPM_EXECUTABLE} run build
    COMMAND ${CMAKE_COMMAND} -E make_directory "${COMMUNITYMP_HUB_RUNTIME_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${COMMUNITYMP_HUB_BUILT_EXE}"
        "${COMMUNITYMP_HUB_RUNTIME_EXE}"
    WORKING_DIRECTORY "${COMMUNITYMP_HUB_SOURCE_DIR}"
    COMMENT "Building CommunityMP Hub"
)

if(WIN32)
    install(CODE "
        set(hub \"${CMAKE_BINARY_DIR}/\${CMAKE_INSTALL_CONFIG_NAME}/communitymp-hub${CMAKE_EXECUTABLE_SUFFIX}\")
        if(NOT EXISTS \"\${hub}\")
            set(hub \"${CMAKE_BINARY_DIR}/communitymp-hub${CMAKE_EXECUTABLE_SUFFIX}\")
        endif()
        if(NOT EXISTS \"\${hub}\")
            message(FATAL_ERROR \"CommunityMP Hub executable was not built: \${hub}\")
        endif()
        file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}\" TYPE EXECUTABLE FILES \"\${hub}\")
    ")
endif()
