set(TARGET_NAME ovrtxMayaPlugin)

add_library(${TARGET_NAME} SHARED
    mayaSceneBridge.cpp
    ovrtxRenderCommand.cpp
    ovrtxRendererSession.cpp
    ovrtxRenderWindow.cpp
    Plugin.cpp
    rtxOverlay.cpp
)

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ovrtx::ovrtx
        Maya::Maya
        MayaUsd::mayaUsd
        MayaUsd::mayaUsdAPI
        UFE::ufe
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        usd
        usdGeom
        usdLux
        usdUtils
        sdf
        tf
        gf
        vt
)

target_compile_definitions(${TARGET_NAME}
    PRIVATE 
        NOMINMAX
        TBB_SUPPRESS_DEPRECATED_MESSAGES
)

target_compile_features(${TARGET_NAME} 
    PRIVATE
        cxx_std_20
)

ovrtx_setup_runtime(${TARGET_NAME})

# -----------------------------------------------------------------------------
# properties
# -----------------------------------------------------------------------------
set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".mll")

# -----------------------------------------------------------------------------
# Maya module file — points the .mod at the installed plug-ins_<ver>/ dir.
# -----------------------------------------------------------------------------
set(INSTALL_PREFIX_NORMALIZED ${CMAKE_INSTALL_PREFIX})
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ovrtxMayaPlugin.mod.template
    ${CMAKE_CURRENT_BINARY_DIR}/ovrtxMayaPlugin.mod
    @ONLY
)

# -----------------------------------------------------------------------------
# Install
# -----------------------------------------------------------------------------
install(TARGETS ${TARGET_NAME}
    RUNTIME DESTINATION plug-ins_${Maya_VERSION}
    LIBRARY DESTINATION plug-ins_${Maya_VERSION}
    ARCHIVE DESTINATION plug-ins_${Maya_VERSION}
)

install(FILES $<TARGET_PDB_FILE:${TARGET_NAME}>
    DESTINATION plug-ins_${Maya_VERSION}
    OPTIONAL
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ovrtxMayaPlugin.mod
    DESTINATION ${CMAKE_INSTALL_PREFIX}
)

# ovrtx
ovrtx_install_runtime(${CMAKE_INSTALL_PREFIX}/plug-ins_${Maya_VERSION})