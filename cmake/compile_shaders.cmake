function(target_glsl_shaders TARGET_NAME)
    # require vulkan first, and check that glslc is available
    find_package(Vulkan REQUIRED)
    if (NOT Vulkan_FOUND)
        message(FATAL_ERROR "Vulkan not found; cannot compile shaders")
    endif ()
    if (NOT Vulkan_glslc_FOUND)
        message(FATAL_ERROR "glslc not found; cannot compile shaders")
    endif ()
    message(STATUS "glslc found at ${Vulkan_GLSLC_EXECUTABLE}")

    set(GLSL_COMPILER ${Vulkan_GLSLC_EXECUTABLE})

    set(OPTIONS)
    set(SINGLE_VALUE_KEYWORDS)
    set(MULTI_VALUE_KEYWORDS INTERFACE PUBLIC PRIVATE COMPILE_OPTIONS)
    cmake_parse_arguments(target_shaders "${OPTIONS}" "${SINGLE_VALUE_KEYWORDS}" "${MULTI_VALUE_KEYWORDS}" ${ARGN})

    foreach (GLSL_FILE IN LISTS target_shaders_INTERFACE)
        # get the filename and extension only
        cmake_path(GET GLSL_FILE FILENAME GLSL_FILE_NAME)
        # set the output file name
        set(SPIRV_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GLSL_FILE_NAME}.spv)
        # add the custom command to compile the shader
        add_custom_command(
                OUTPUT ${SPIRV_FILE}
                COMMAND ${GLSL_COMPILER} ${target_shaders_COMPILE_OPTIONS} -o ${SPIRV_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${GLSL_FILE}
                MAIN_DEPENDENCY ${GLSL_FILE}
        )
        target_sources(${TARGET_NAME} INTERFACE ${SPIRV_FILE})
    endforeach ()

    foreach (GLSL_FILE IN LISTS target_shaders_PUBLIC)
        # get the filename and extension only
        cmake_path(GET GLSL_FILE FILENAME GLSL_FILE_NAME)
        # set the output file name
        set(SPIRV_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GLSL_FILE_NAME}.spv)
        # add the custom command to compile the shader
        add_custom_command(
                OUTPUT ${SPIRV_FILE}
                COMMAND ${GLSL_COMPILER} ${target_shaders_COMPILE_OPTIONS} -o ${SPIRV_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${GLSL_FILE}
                MAIN_DEPENDENCY ${GLSL_FILE}
        )
        target_sources(${TARGET_NAME} PUBLIC ${SPIRV_FILE})
    endforeach ()

    foreach (GLSL_FILE IN LISTS target_shaders_PRIVATE)
        # get the filename and extension only
        cmake_path(GET GLSL_FILE FILENAME GLSL_FILE_NAME)
        # set the output file name
        set(SPIRV_FILE ${CMAKE_CURRENT_BINARY_DIR}/${GLSL_FILE_NAME}.spv)
        # add the custom command to compile the shader
        add_custom_command(
                OUTPUT ${SPIRV_FILE}
                COMMAND ${GLSL_COMPILER} ${target_shaders_COMPILE_OPTIONS} -o ${SPIRV_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${GLSL_FILE}
                MAIN_DEPENDENCY ${GLSL_FILE}
        )
        target_sources(${TARGET_NAME} PRIVATE ${SPIRV_FILE})
    endforeach ()
endfunction()