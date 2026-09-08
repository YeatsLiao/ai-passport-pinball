idf_component_register(
    SRCS "recovery_boot_hook.c"
    PRIV_REQUIRES bootloader_support
)

# 私有头文件在 bootloader_support 的 private_include 里。
# 注意不能用 PRIV_INCLUDE_DIRS 直传 $ENV{IDF_PATH}:Windows 下 IDF_PATH 可能
# 是反斜杠形式(如 D:\Software\...),未加引号传入 CMake 时 "\S" 会被当成
# 非法转义导致 configure 失败。先归一化为正斜杠,再以加引号的方式挂目录。
set(BOOTLOADER_SUPPORT_PRIVATE "$ENV{IDF_PATH}/components/bootloader_support/private_include")
string(REPLACE "\\" "/" BOOTLOADER_SUPPORT_PRIVATE "${BOOTLOADER_SUPPORT_PRIVATE}")
target_include_directories(${COMPONENT_LIB} PRIVATE "${BOOTLOADER_SUPPORT_PRIVATE}")
