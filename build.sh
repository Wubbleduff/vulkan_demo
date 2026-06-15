#!/bin/bash

set -e

echo "Compile"
# VULKAN_SO=/usr/lib64/libvulkan.so
VULKAN_SO=$VULKAN_SDK/lib/VulkanLoader/lib/libvulkan.so
time clang -Wall -Wextra -Werror -std=c11 -g -o vulkan_demo -I $VULKAN_SDK/include -Wl,-rpath,$VULKAN_SDK/lib/VulkanLoader/lib $VULKAN_SO /usr/lib/libglfw.so.3.4 main.c
echo ""
echo "Compile shaders"
time $VULKAN_SDK/bin/slangc shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vert_main -entry frag_main -o slang.spv
