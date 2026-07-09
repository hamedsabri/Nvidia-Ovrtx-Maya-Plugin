include(FetchContent)

set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(
    ovrtx
    URL "https://github.com/NVIDIA-Omniverse/ovrtx/releases/download/v0.3.0/ovrtx@0.3.0.312915.cec773e1.windows-x86_64.zip"
    URL_HASH SHA256=7fe420790fcd4c0a8609cadba73c7bb03a30fa47cd4ab7f130e3cf92a972063a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ovrtx)
list(APPEND CMAKE_PREFIX_PATH ${ovrtx_SOURCE_DIR})
find_package(ovrtx REQUIRED)

function(ovrtx_setup_runtime TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OVRTX_BINARY_DIR}/ovrtx-dynamic.dll"
            "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        COMMENT "Copying ovrtx-dynamic.dll next to ${TARGET_NAME}"
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
    install(FILES "${OVRTX_BINARY_DIR}/ovrtx-dynamic.dll"
            DESTINATION ${target_dir})

    foreach(DIR cache library libs mdl plugins rendering-data usd_plugins)
        if(EXISTS "${OVRTX_BINARY_DIR}/${DIR}")
            install(DIRECTORY "${OVRTX_BINARY_DIR}/${DIR}/"
                    DESTINATION ${target_dir}/${DIR})
        endif()
    endforeach()
endfunction()