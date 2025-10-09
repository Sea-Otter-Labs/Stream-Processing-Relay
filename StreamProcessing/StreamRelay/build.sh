#!/bin/bash
set -e

# 工程根目录
PROJECT_ROOT=$(cd "$(dirname "$0")"; pwd)

# 依赖库路径
FFMPEG_LIB_DIR="$(dirname "$PROJECT_ROOT")/3rdparty/ffmpeg/lib"
MYSQL_LIB_DIR="$(dirname "$PROJECT_ROOT")/3rdparty/mysql/lib"
SPDLOG_LIB_DIR="$(dirname "$PROJECT_ROOT")/3rdparty/spdlog/lib"
FMT_LIB_DIR="$(dirname "$PROJECT_ROOT")/3rdparty/fmt/lib"


# 默认构建类型（可通过参数指定）
BUILD_TYPE=${1:-Debug}   # 默认 Debug，可传 Release

# 创建并进入 build 目录（按模式分开）
BUILD_DIR="$PROJECT_ROOT/build/$BUILD_TYPE"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 运行 CMake 配置并编译
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE ../..
make -j$(nproc)

# 可执行文件输出目录
BIN_DIR="$BUILD_DIR/bin/$BUILD_TYPE"
mkdir -p "$BIN_DIR"

# 拷贝依赖库
echo "Copying dependent libraries..."
cp -u $FFMPEG_LIB_DIR/*.so* "$BIN_DIR" || true
cp -u $MYSQL_LIB_DIR/*.so* "$BIN_DIR" || true
cp -u $SPDLOG_LIB_DIR/*.so* "$BIN_DIR" || true
cp -u $FMT_LIB_DIR/*.so* "$BIN_DIR" || true

echo "============================"
echo "✅ Build finished successfully! ($BUILD_TYPE)"
echo "👉 Executable is in: $BIN_DIR/"
echo "👉 Libraries copied into: $BIN_DIR/"
echo "============================"
