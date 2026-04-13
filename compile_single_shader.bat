@echo off
setlocal

:: 这里的 glslc 路径通常在环境变量中，如果没有，请替换为 Vulkan SDK Bin 目录的绝对路径
set GLSLC=glslc.exe

echo Compiling Shaders for Vulkan...

:: 编译 PostProcess Shader
%GLSLC% assets/Editor/shaders/PostProcess/postprocess.vulkan.vert -o assets/Editor/shaders/PostProcess/postprocess.vert.spv
if %ERRORLEVEL% NEQ 0 ( echo Vertex Shader failed! & pause & exit /b %ERRORLEVEL% )

%GLSLC% assets/Editor/shaders/PostProcess/postprocess.vulkan.frag -o assets/Editor/shaders/PostProcess/postprocess.frag.spv
if %ERRORLEVEL% NEQ 0 ( echo Fragment Shader failed! & pause & exit /b %ERRORLEVEL% )

echo Shaders compiled successfully!
pause