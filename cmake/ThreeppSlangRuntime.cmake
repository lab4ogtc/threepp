function(threepp_configure_slang_runtime target)
    if (APPLE AND THREEPP_WITH_SLANG)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@executable_path")

        foreach (slang_target
                slang::slang
                slang::slang-glslang
                slang::slang-glsl-module
                slang::slang-llvm
                slang::slang-rt)
            if (TARGET ${slang_target})
                get_target_property(slang_target_type ${slang_target} TYPE)
                if (NOT slang_target_type STREQUAL "INTERFACE_LIBRARY")
                    add_custom_command(TARGET ${target} POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${slang_target}>
                            $<TARGET_FILE_DIR:${target}>
                            VERBATIM)
                endif ()
            endif ()
        endforeach ()
    endif ()
endfunction()
