
add_subdirectory(gl)

if (THREEPP_WITH_METAL)
    add_subdirectory(metal)
endif ()

if (THREEPP_WITH_WGPU AND THREEPP_WITH_GLFW)
    # Helper function to create a cross-renderer test target with Wgpu/WebGPU support.
    function(add_cross_renderer_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE threepp Catch2::Catch2WithMain)
        target_include_directories(${name} PRIVATE
                "${PROJECT_SOURCE_DIR}/src"
                "${WGPU_INCLUDE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        target_link_libraries(${name} PRIVATE ${WGPU_LIBRARY})
        target_compile_definitions(${name} PRIVATE DATA_FOLDER="${THREEPP_DATA_DIR}")

        # These tests need a display server (GLFW for Canvas).
        # Use xvfb-run when available so they work in headless CI.
        find_program(XVFB_RUN xvfb-run)
        if (XVFB_RUN)
            add_test(NAME ${name} COMMAND ${XVFB_RUN} -a $<TARGET_FILE:${name}>)
        else ()
            add_test(NAME ${name} COMMAND ${name})
        endif ()

        # CI runners without a GPU/GL driver cause all tests to skip (or Canvas
        # construction to throw on Windows WGL). Mark as skipped rather than failed.
        set_tests_properties(${name} PROPERTIES
            SKIP_REGULAR_EXPRESSION "No GPU backend available for Wgpu;WGL: The driver does not appear to support OpenGL"
        )
    endfunction()

    add_cross_renderer_test(CrossRenderer_gl_test CrossRenderer_gl_test.cpp)
    add_cross_renderer_test(CrossRenderer_wgpu_test CrossRenderer_wgpu_test.cpp)
    add_cross_renderer_test(CrossRenderer_parity_test CrossRenderer_parity_test.cpp)
    add_cross_renderer_test(CrossRenderer_furnace_test CrossRenderer_furnace_test.cpp)
    add_cross_renderer_test(WgpuRenderer_perf_test WgpuRenderer_perf_test.cpp)

    if (THREEPP_WITH_INFERENCE)
        add_subdirectory(inference)
    endif ()
endif ()

# Vulkan PT/deferred golden-image regression net. Plain exit-code program (not
# Catch2): renders fixed deterministic scenes and compares to committed PPM
# references via examples/vulkan/capture_util.hpp. Needs an RT-capable GPU at
# runtime → exits 42 (CTest "Skipped") where there isn't one. References are
# GPU-specific, so this is a local pre-push check, not a hard cross-hardware gate.
if (THREEPP_WITH_VULKAN AND THREEPP_WITH_SLANG)
    add_executable(VulkanShaderMaterial_test VulkanShaderMaterial_test.cpp)
    target_link_libraries(VulkanShaderMaterial_test PRIVATE threepp Catch2::Catch2WithMain)
    target_include_directories(VulkanShaderMaterial_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    threepp_configure_slang_runtime(VulkanShaderMaterial_test)
    add_test(NAME VulkanShaderMaterial_test COMMAND VulkanShaderMaterial_test)
endif ()

if (THREEPP_WITH_VULKAN)
    add_executable(VulkanWireframeGeometry_test VulkanWireframeGeometry_test.cpp)
    target_link_libraries(VulkanWireframeGeometry_test PRIVATE threepp Catch2::Catch2WithMain)
    target_include_directories(VulkanWireframeGeometry_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    add_test(NAME VulkanWireframeGeometry_test COMMAND VulkanWireframeGeometry_test)
endif ()

if (THREEPP_WITH_VULKAN AND THREEPP_WITH_GLFW)
    add_executable(VulkanRenderTargets_test VulkanRenderTargets_test.cpp)
    target_link_libraries(VulkanRenderTargets_test PRIVATE threepp Catch2::Catch2WithMain)
    target_include_directories(VulkanRenderTargets_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    if (DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vma/vk_mem_alloc.h")
        target_include_directories(VulkanRenderTargets_test PRIVATE "$ENV{VULKAN_SDK}/Include/vma")
    endif ()
    add_test(NAME VulkanRenderTargets_test COMMAND VulkanRenderTargets_test)

    add_executable(VulkanReadback_test VulkanReadback_test.cpp)
    target_link_libraries(VulkanReadback_test PRIVATE threepp Catch2::Catch2WithMain)
    target_include_directories(VulkanReadback_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    add_test(NAME VulkanReadback_test COMMAND VulkanReadback_test)

    add_executable(VulkanRenderTargetRuntime_test VulkanRenderTargetRuntime_test.cpp)
    target_link_libraries(VulkanRenderTargetRuntime_test PRIVATE threepp)
    add_test(NAME VulkanRenderTargetRuntime_test COMMAND VulkanRenderTargetRuntime_test)
    set_tests_properties(VulkanRenderTargetRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanEventCameraRuntime_test VulkanEventCameraRuntime_test.cpp)
    target_link_libraries(VulkanEventCameraRuntime_test PRIVATE threepp)
    add_test(NAME VulkanEventCameraRuntime_test COMMAND VulkanEventCameraRuntime_test)
    set_tests_properties(VulkanEventCameraRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanRenderTargetMipmapRuntime_test VulkanRenderTargetMipmapRuntime_test.cpp)
    target_link_libraries(VulkanRenderTargetMipmapRuntime_test PRIVATE threepp)
    target_include_directories(VulkanRenderTargetMipmapRuntime_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    if (DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Include/vma/vk_mem_alloc.h")
        target_include_directories(VulkanRenderTargetMipmapRuntime_test PRIVATE "$ENV{VULKAN_SDK}/Include/vma")
    endif ()
    add_test(NAME VulkanRenderTargetMipmapRuntime_test COMMAND VulkanRenderTargetMipmapRuntime_test)
    set_tests_properties(VulkanRenderTargetMipmapRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanFramebufferTextureMipmapRuntime_test VulkanFramebufferTextureMipmapRuntime_test.cpp)
    target_link_libraries(VulkanFramebufferTextureMipmapRuntime_test PRIVATE threepp)
    add_test(NAME VulkanFramebufferTextureMipmapRuntime_test COMMAND VulkanFramebufferTextureMipmapRuntime_test)
    set_tests_properties(VulkanFramebufferTextureMipmapRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanDepthTextureRuntime_test VulkanDepthTextureRuntime_test.cpp)
    target_link_libraries(VulkanDepthTextureRuntime_test PRIVATE threepp)
    add_test(NAME VulkanDepthTextureRuntime_test COMMAND VulkanDepthTextureRuntime_test)
    set_tests_properties(VulkanDepthTextureRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanDataTextureRuntime_test VulkanDataTextureRuntime_test.cpp)
    target_link_libraries(VulkanDataTextureRuntime_test PRIVATE threepp)
    add_test(NAME VulkanDataTextureRuntime_test COMMAND VulkanDataTextureRuntime_test)
    set_tests_properties(VulkanDataTextureRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanCubeTextureRuntime_test VulkanCubeTextureRuntime_test.cpp)
    target_link_libraries(VulkanCubeTextureRuntime_test PRIVATE threepp)
    add_test(NAME VulkanCubeTextureRuntime_test COMMAND VulkanCubeTextureRuntime_test)
    set_tests_properties(VulkanCubeTextureRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanMaterialRuntime_test VulkanMaterialRuntime_test.cpp)
    target_link_libraries(VulkanMaterialRuntime_test PRIVATE threepp)
    add_test(NAME VulkanMaterialRuntime_test COMMAND VulkanMaterialRuntime_test)
    set_tests_properties(VulkanMaterialRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanLightMapRuntime_test VulkanLightMapRuntime_test.cpp)
    target_link_libraries(VulkanLightMapRuntime_test PRIVATE threepp)
    add_test(NAME VulkanLightMapRuntime_test COMMAND VulkanLightMapRuntime_test)
    set_tests_properties(VulkanLightMapRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanAoMapRuntime_test VulkanAoMapRuntime_test.cpp)
    target_link_libraries(VulkanAoMapRuntime_test PRIVATE threepp)
    add_test(NAME VulkanAoMapRuntime_test COMMAND VulkanAoMapRuntime_test)
    set_tests_properties(VulkanAoMapRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanPhongNormalMapRuntime_test VulkanPhongNormalMapRuntime_test.cpp)
    target_link_libraries(VulkanPhongNormalMapRuntime_test PRIVATE threepp)
    add_test(NAME VulkanPhongNormalMapRuntime_test COMMAND VulkanPhongNormalMapRuntime_test)
    set_tests_properties(VulkanPhongNormalMapRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanLegacyEmissiveMapRuntime_test VulkanLegacyEmissiveMapRuntime_test.cpp)
    target_link_libraries(VulkanLegacyEmissiveMapRuntime_test PRIVATE threepp)
    add_test(NAME VulkanLegacyEmissiveMapRuntime_test COMMAND VulkanLegacyEmissiveMapRuntime_test)
    set_tests_properties(VulkanLegacyEmissiveMapRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanLightsRuntime_test VulkanLightsRuntime_test.cpp)
    target_link_libraries(VulkanLightsRuntime_test PRIVATE threepp)
    add_test(NAME VulkanLightsRuntime_test COMMAND VulkanLightsRuntime_test)
    set_tests_properties(VulkanLightsRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanPhysicalReferenceRuntime_test VulkanPhysicalReferenceRuntime_test.cpp)
    target_link_libraries(VulkanPhysicalReferenceRuntime_test PRIVATE threepp)
    add_test(NAME VulkanPhysicalReferenceRuntime_test COMMAND VulkanPhysicalReferenceRuntime_test)
    set_tests_properties(VulkanPhysicalReferenceRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanPointsRuntime_test VulkanPointsRuntime_test.cpp)
    target_link_libraries(VulkanPointsRuntime_test PRIVATE threepp)
    add_test(NAME VulkanPointsRuntime_test COMMAND VulkanPointsRuntime_test)
    set_tests_properties(VulkanPointsRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanGeometryRuntime_test VulkanGeometryRuntime_test.cpp)
    target_link_libraries(VulkanGeometryRuntime_test PRIVATE threepp)
    add_test(NAME VulkanGeometryRuntime_test COMMAND VulkanGeometryRuntime_test)
    set_tests_properties(VulkanGeometryRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanInstancingRuntime_test VulkanInstancingRuntime_test.cpp)
    target_link_libraries(VulkanInstancingRuntime_test PRIVATE threepp)
    add_test(NAME VulkanInstancingRuntime_test COMMAND VulkanInstancingRuntime_test)
    set_tests_properties(VulkanInstancingRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanSpriteRuntime_test VulkanSpriteRuntime_test.cpp)
    target_link_libraries(VulkanSpriteRuntime_test PRIVATE threepp)
    add_test(NAME VulkanSpriteRuntime_test COMMAND VulkanSpriteRuntime_test)
    set_tests_properties(VulkanSpriteRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanWireframeRuntime_test VulkanWireframeRuntime_test.cpp)
    target_link_libraries(VulkanWireframeRuntime_test PRIVATE threepp)
    add_test(NAME VulkanWireframeRuntime_test COMMAND VulkanWireframeRuntime_test)
    set_tests_properties(VulkanWireframeRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanTransparentOverlayRuntime_test VulkanTransparentOverlayRuntime_test.cpp)
    target_link_libraries(VulkanTransparentOverlayRuntime_test PRIVATE threepp)
    add_test(NAME VulkanTransparentOverlayRuntime_test COMMAND VulkanTransparentOverlayRuntime_test)
    set_tests_properties(VulkanTransparentOverlayRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanLineDashedRuntime_test VulkanLineDashedRuntime_test.cpp)
    target_link_libraries(VulkanLineDashedRuntime_test PRIVATE threepp)
    add_test(NAME VulkanLineDashedRuntime_test COMMAND VulkanLineDashedRuntime_test)
    set_tests_properties(VulkanLineDashedRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanHelperLinesRuntime_test VulkanHelperLinesRuntime_test.cpp)
    target_link_libraries(VulkanHelperLinesRuntime_test PRIVATE threepp)
    add_test(NAME VulkanHelperLinesRuntime_test COMMAND VulkanHelperLinesRuntime_test)
    set_tests_properties(VulkanHelperLinesRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanParticleSystemRuntime_test VulkanParticleSystemRuntime_test.cpp)
    target_link_libraries(VulkanParticleSystemRuntime_test PRIVATE threepp)
    add_test(NAME VulkanParticleSystemRuntime_test COMMAND VulkanParticleSystemRuntime_test)
    set_tests_properties(VulkanParticleSystemRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanSkinnedMeshRuntime_test VulkanSkinnedMeshRuntime_test.cpp)
    target_link_libraries(VulkanSkinnedMeshRuntime_test PRIVATE threepp)
    add_test(NAME VulkanSkinnedMeshRuntime_test COMMAND VulkanSkinnedMeshRuntime_test)
    set_tests_properties(VulkanSkinnedMeshRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanMorphTargetRuntime_test VulkanMorphTargetRuntime_test.cpp)
    target_link_libraries(VulkanMorphTargetRuntime_test PRIVATE threepp)
    add_test(NAME VulkanMorphTargetRuntime_test COMMAND VulkanMorphTargetRuntime_test)
    set_tests_properties(VulkanMorphTargetRuntime_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")

    add_executable(VulkanGolden_test VulkanGolden_test.cpp)
    target_link_libraries(VulkanGolden_test PRIVATE threepp)
    target_include_directories(VulkanGolden_test PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_SOURCE_DIR}/examples/vulkan")# capture_util.hpp
    target_compile_definitions(VulkanGolden_test PRIVATE
            DATA_FOLDER="${THREEPP_DATA_DIR}"
            PROJECT_FOLDER="${PROJECT_SOURCE_DIR}")
    add_test(NAME VulkanGolden_test COMMAND VulkanGolden_test)
    set_tests_properties(VulkanGolden_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanGoldenPT_test COMMAND VulkanGolden_test --pt)
    set_tests_properties(VulkanGoldenPT_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1Contract_test COMMAND VulkanGolden_test --stage1-contract)
    set_tests_properties(VulkanStage1Contract_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1SplitBackground_test COMMAND VulkanGolden_test --stage1-split-background)
    set_tests_properties(VulkanStage1SplitBackground_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1FramebufferTexture_test COMMAND VulkanGolden_test --stage1-framebuffer-texture)
    set_tests_properties(VulkanStage1FramebufferTexture_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1LineDepth_test COMMAND VulkanGolden_test --stage1-line-depth)
    set_tests_properties(VulkanStage1LineDepth_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1TexturedLineDepth_test COMMAND VulkanGolden_test --stage1-textured-line-depth)
    set_tests_properties(VulkanStage1TexturedLineDepth_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1ColoredLineDepth_test COMMAND VulkanGolden_test --stage1-colored-line-depth)
    set_tests_properties(VulkanStage1ColoredLineDepth_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1DataTextureGridDepth_test COMMAND VulkanGolden_test --stage1-data-texture-grid-depth)
    set_tests_properties(VulkanStage1DataTextureGridDepth_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
    add_test(NAME VulkanStage1MsaaOverlayLine_test COMMAND VulkanGolden_test --stage1-msaa-overlay-line)
    set_tests_properties(VulkanStage1MsaaOverlayLine_test PROPERTIES
            SKIP_RETURN_CODE 42
            FAIL_REGULAR_EXPRESSION "\\[Vulkan\\]")
endif ()
