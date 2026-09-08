#!/usr/bin/env bash
# tools/host_test.sh —— 在主机(PC)上编译并运行物理单元测试,无需 ESP-IDF。
# 用法: ./tools/host_test.sh   (需要 cc/gcc/clang 与 make)
set -e
cd "$(dirname "$0")/.."

CC="${CC:-cc}"
command -v "$CC" >/dev/null 2>&1 || CC=gcc
mkdir -p build/host
"$CC" -Wall -Wextra -I main -o build/host/test_pb_physics \
    tests/test_pb_physics.c main/pb_physics.c main/pb_table.c -lm
./build/host/test_pb_physics
