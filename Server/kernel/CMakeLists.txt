cmake_minimum_required(VERSION 3.5)

# Kernel module: lets the app read the PS button (loaded at runtime, needs "Enable Unsafe Homebrew")
add_executable(vitapad_kernel main.c)
target_link_libraries(vitapad_kernel
  SceCtrlForDriver_stub
  SceSysclibForDriver_stub
  SceSysmemForDriver_stub
)
set_target_properties(vitapad_kernel PROPERTIES LINK_FLAGS "-nostdlib")
target_compile_options(vitapad_kernel PRIVATE -Wall -O2)
vita_create_self(vitapad_kernel.skprx vitapad_kernel CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml UNSAFE)
vita_create_stubs(vitapad_kernel_stubs vitapad_kernel ${CMAKE_CURRENT_SOURCE_DIR}/exports.yml KERNEL)
