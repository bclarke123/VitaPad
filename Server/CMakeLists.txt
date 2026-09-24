cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(VitaPad C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaPad")
set(VITA_TITLEID "VPAD00001")
set(VITA_VERSION "01.60")

# Kernel module: reads the PS button (loaded at runtime, needs "Enable Unsafe Homebrew")
add_executable(vitapad_kernel kernel/main.c)
target_link_libraries(vitapad_kernel
  SceCtrlForDriver_stub
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
)
set_target_properties(vitapad_kernel PROPERTIES LINK_FLAGS "-nostdlib")
target_compile_options(vitapad_kernel PRIVATE -Wall -O2)
vita_create_self(vitapad_kernel.skprx vitapad_kernel CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/kernel/exports.yml UNSAFE)
vita_create_stubs(vitapad_kernel_stubs vitapad_kernel ${CMAKE_CURRENT_SOURCE_DIR}/kernel/exports.yml KERNEL)

# App
add_executable(VitaPad
  source/main.c
  source/remap.c
  source/psbutton.c
)
target_include_directories(VitaPad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Common)
target_compile_options(VitaPad PRIVATE -Wall -O3)
set_target_properties(VitaPad PROPERTIES LINK_FLAGS "-Wl,-q")
add_dependencies(VitaPad vitapad_kernel_stubs)
target_link_libraries(VitaPad
  # Weak: the app still starts when the kernel module isn't loaded
  ${CMAKE_CURRENT_BINARY_DIR}/libVitaPadKernel_stub_weak.a
  vita2d
  SceLibKernel_stub SceSysmodule_stub SceCtrl_stub SceTouch_stub SceMotion_stub
  m SceNet_stub SceNetCtl_stub SceAppUtil_stub ScePgf_stub
  jpeg freetype c ScePower_stub SceCommonDialog_stub png16 z
  SceGxm_stub SceDisplay_stub SceAppMgr_stub
  SceShellSvc_stub SceVshBridge_stub taihen_stub
)

# Unsafe: needed to load the kernel module
vita_create_self(eboot.bin VitaPad UNSAFE)
vita_create_vpk(VitaPad.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE build/sce_sys/icon0.png sce_sys/icon0.png
  FILE build/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE build/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE build/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_CURRENT_BINARY_DIR}/vitapad_kernel.skprx module/vitapad_kernel.skprx
)
# The kernel module must be built before it's packed (target names differ before/after CMake 3.20)
if(TARGET VitaPad.vpk-vpk)
  add_dependencies(VitaPad.vpk-vpk vitapad_kernel.skprx-self)
else()
  add_dependencies(VitaPad.vpk vitapad_kernel.skprx)
endif()
