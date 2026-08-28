include(FetchContent)

set(FETCHCONTENT_QUIET ON)

if(WIN32)
    set(OVRTX_PACKAGE_URL "https://github.com/NVIDIA-Omniverse/ovrtx/releases/download/v0.3.0/ovrtx@0.3.0.312915.cec773e1.windows-x86_64.zip")
    set(OVRTX_PACKAGE_HASH "SHA256=7fe420790fcd4c0a8609cadba73c7bb03a30fa47cd4ab7f130e3cf92a972063a")
    set(OVRTX_DYNAMIC_LIB_NAME "ovrtx-dynamic.dll")
elseif(UNIX AND NOT APPLE)
    set(OVRTX_PACKAGE_URL "https://github.com/NVIDIA-Omniverse/ovrtx/releases/download/v0.3.0/ovrtx@0.3.0.312915.cec773e1.manylinux_2_35_x86_64.zip")
    set(OVRTX_PACKAGE_HASH "SHA256=5569e44b18d2d39f23f374c9352dac9c87b8115892209c243c98732085f1d5f9")
    set(OVRTX_DYNAMIC_LIB_NAME "libovrtx-dynamic.so")
else()
    message(FATAL_ERROR "ovrtx: no prebuilt package available for this platform (Windows and Linux x86_64 are supported)")
endif()

FetchContent_Declare(
    ovrtx
    URL "${OVRTX_PACKAGE_URL}"
    URL_HASH "${OVRTX_PACKAGE_HASH}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ovrtx)
list(APPEND CMAKE_PREFIX_PATH ${ovrtx_SOURCE_DIR})
find_package(ovrtx REQUIRED)

function(ovrtx_setup_runtime TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OVRTX_BINARY_DIR}/${OVRTX_DYNAMIC_LIB_NAME}"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMENT "Copying ${OVRTX_DYNAMIC_LIB_NAME} next to ${TARGET_NAME}"
    )

    set(OVRTX_RUNTIME_DIRS cache library libs mdl plugins rendering-data usd_plugins)
    foreach(DIR ${OVRTX_RUNTIME_DIRS})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E create_symlink
                "${OVRTX_BINARY_DIR}/${DIR}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${DIR}"
            COMMENT "Linking ${DIR} next to ${TARGET_NAME}"
        )
    endforeach()
endfunction()

# Install rule: copy ovrtx-dynamic + the runtime trees next to the installed
# binary so the install tree is self-contained.
#
#   cache          - PRECOMPILED d3d12/vulkan shader cache (~1GB). NOT
#                    regenerable: the RTX renderer reads shaderFolderHash.bin /
#                    version from here and refuses to initialize if missing
#                    ("Shader caches are missing from the application").
#   library, libs  - MaterialX + MDL/Iray shading libraries
#   mdl            - MDL material modules (UsdPreviewSurface, ...)
#   plugins        - the RTX renderer + carb/USD framework plugins
#   rendering-data - RTX runtime + startup-warmup data
#   usd_plugins    - the OmniRtx* USD schemas the overlay authors
function(ovrtx_install_runtime target_dir)
    install(FILES "${OVRTX_BINARY_DIR}/${OVRTX_DYNAMIC_LIB_NAME}"
            DESTINATION ${target_dir})

    foreach(DIR cache library libs mdl plugins rendering-data usd_plugins)
        if(EXISTS "${OVRTX_BINARY_DIR}/${DIR}")
            install(DIRECTORY "${OVRTX_BINARY_DIR}/${DIR}/"
                    DESTINATION ${target_dir}/${DIR})
        endif()
    endforeach()
endfunction()