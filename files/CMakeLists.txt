add_subdirectory(shaders)
add_subdirectory(data)
add_subdirectory(data-mw)
add_subdirectory(lua_api)

if (NOT DEFINED OPENMW_RESOURCES_ROOT)
    return()
endif()

copy_resource_file("launcher/images/openmw.png" "${OPENMW_RESOURCES_ROOT}" "resources/openmw.png")
copy_resource_file("mygui/RussoOne-Regular.ttf" "${OPENMW_RESOURCES_ROOT}" "resources/vfs/mygui/RussoOne-Regular.ttf")
copy_resource_file("mygui/tes3mp_chat_font.xml" "${OPENMW_RESOURCES_ROOT}" "resources/vfs/mygui/tes3mp_chat_font.xml")
copy_resource_file("mygui/tes3mp_chat.skin.xml" "${OPENMW_RESOURCES_ROOT}" "resources/vfs/mygui/tes3mp_chat.skin.xml")

if (BUILD_TES3MP_CLIENT)
    copy_resource_file("tes3mp/tes3mp_logo.png" "${OPENMW_RESOURCES_ROOT}" "resources/tes3mp_logo.png")

    file(GLOB_RECURSE COMMUNITYMP_MYGUI_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false
        RELATIVE "${OpenMW_SOURCE_DIR}/files/mygui"
        "${OpenMW_SOURCE_DIR}/files/mygui/*")
    copy_all_resource_files("${OpenMW_SOURCE_DIR}/files/mygui"
        "${OPENMW_RESOURCES_ROOT}" "resources/vfs/mygui" "${COMMUNITYMP_MYGUI_FILES}")
endif()
