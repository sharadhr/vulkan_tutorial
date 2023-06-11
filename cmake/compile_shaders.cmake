function(compile_shaders TARGET_NAME)
    set(OPTIONS HLSL GLSL INTERFACE PUBLIC PRIVATE)
    set(MULTI_VALUE_KEYWORDS SHADER_FILES COMPILE_OPTIONS)
    cmake_parse_arguments(compile_shaders "${OPTIONS}" "${SINGLE_VALUE_KEYWORDS}" "${MULTI_VALUE_KEYWORDS}" ${ARGN})

    find_package(Vulkan REQUIRED)

    foreach(SHADER_FILE IN LISTS compile_shaders_SHADER_FILES)
        # get the filename and extension only
        cmake_path(GET SHADER_FILE FILENAME SHADER_FILE_NAME)
        # set the output file name
        set(SPIRV_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_FILE_NAME}.spv)
        
        # add the custom command to compile the shader
        if (compile_shaders_GLSL)
            if (NOT Vulkan_glslc_FOUND)
                message(FATAL_ERROR "glslc not found.")
            endif()
            set(COMPILE_COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${compile_shaders_COMPILE_OPTIONS} -o ${SPIRV_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE})

        elseif (compile_shaders_HLSL)
            if (NOT Vulkan_dxc_exe_FOUND)
                message(FATAL_ERROR "dxc not found.")
            endif()
        set(COMPILE_COMMAND ${Vulkan_dxc_EXECUTABLE} ${compile_shaders_COMPILE_OPTIONS} /T spirv /Fo ${SPIRV_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE})

        else()
            message(FATAL_ERROR "Unsupported shader language '${SHADER_LANGUAGE}'.")
        endif()
        add_custom_command(
                OUTPUT ${SPIRV_FILE}
                COMMAND ${COMPILE_COMMAND}
                MAIN_DEPENDENCY ${SHADER_FILE}
        )
        
        # add the output file to the target
        if (compile_shaders_INTERFACE)
			target_sources(${TARGET_NAME} INTERFACE ${SPIRV_FILE})
        elseif (compile_shaders_PUBLIC)
            target_sources(${TARGET_NAME} PUBLIC ${SPIRV_FILE})
        elseif (compile_shaders_PRIVATE)
			target_sources(${TARGET_NAME} PRIVATE ${SPIRV_FILE})
        endif()
    endforeach()
endfunction()

function(target_shaders TARGET_NAME)
    set(OPTIONS HLSL GLSL)
    set(SINGLE_VALUE_KEYWORDS)
    set(MULTI_VALUE_KEYWORDS INTERFACE PUBLIC PRIVATE COMPILE_OPTIONS)
    cmake_parse_arguments(target_shaders "${OPTIONS}" "${SINGLE_VALUE_KEYWORDS}" "${MULTI_VALUE_KEYWORDS}" ${ARGN})

    if(target_shaders_HLSL)
        set(SHADER_LANGUAGE HLSL)
    elseif(target_shaders_GLSL)
		set(SHADER_LANGUAGE GLSL)
	else()
		message(FATAL_ERROR "No shader language specified.")
    endif()

    if (target_shaders_INTERFACE)
        compile_shaders(${TARGET_NAME} INTERFACE ${SHADER_LANGUAGE} COMPILE_OPTIONS ${target_shaders_COMPILE_OPTIONS} SHADER_FILES ${target_shaders_INTERFACE})
    elseif(target_shaders_PUBLIC)
        compile_shaders(${TARGET_NAME} PUBLIC ${SHADER_LANGUAGE} COMPILE_OPTIONS ${target_shaders_COMPILE_OPTIONS} SHADER_FILES ${target_shaders_PUBLIC})
    elseif(target_shaders_PRIVATE)
        compile_shaders(${TARGET_NAME} PRIVATE ${SHADER_LANGUAGE} COMPILE_OPTIONS ${target_shaders_COMPILE_OPTIONS} SHADER_FILES ${target_shaders_PRIVATE})
    endif()
endfunction()