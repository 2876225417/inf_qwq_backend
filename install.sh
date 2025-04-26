#!/bin/bash

set -e

BUILD_DIR="build"
PACKAGE_DIR="package"

# 清理并创建目录结构
rm -rf ${PACKAGE_DIR}
mkdir -p ${PACKAGE_DIR}/{bin,lib,3rdparty/onnxruntime,models,plugins,inf_src}

echo "Copying executable file..."
cp -v ${BUILD_DIR}/bin/inf_qwq_backend ${PACKAGE_DIR}/bin/

echo "Collecting dynamic dependencies..."

ldd ${BUILD_DIR}/bin/inf_qwq_backend | awk '/=>/ {print $3}' | xargs -I{} cp -Lv {} ${PACKAGE_DIR}/lib/

# 收集 onnxruntime 依赖
if [ -d "3rdparty/onnxruntime_x86_64" ]; then
    echo "Collecting onnxruntime dependencies..."
    ldd 3rdparty/onnxruntime_x86_64/lib/libonnxruntime.so | awk '/=>/ {print $3}' | xargs -I{} cp -Lv {} ${PACKAGE_DIR}/lib/
fi

echo "Copying 3rdparty libraries..."
cp -rv 3rdparty/onnxruntime_x86_64 ${PACKAGE_DIR}/3rdparty/onnxruntime

echo "Fixing rpath..."
patchelf --set-rpath '$ORIGIN/../lib' ${PACKAGE_DIR}/bin/inf_qwq_backend

echo "Copying model files..."
# 复制模型文件到 inf_src 目录
cp -v onnx/chars/cls/gen/cls_gen.onnx ${PACKAGE_DIR}/inf_src/rec_gen.onnx
cp -v onnx/chars/det/gen/det_gen.onnx ${PACKAGE_DIR}/inf_src/det_gen.onnx
cp -v onnx/chars.txt ${PACKAGE_DIR}/inf_src/chars.txt

# 复制其他模型文件
if [ -d "${BUILD_DIR}/models" ]; then
    cp -rv ${BUILD_DIR}/models/* ${PACKAGE_DIR}/models/
fi

# 复制插件
if [ -d "${BUILD_DIR}/plugins" ]; then
    cp -rv ${BUILD_DIR}/plugins/* ${PACKAGE_DIR}/plugins/
fi

echo "Generating launch script..."
cat > ${PACKAGE_DIR}/run.sh << 'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 设置库路径
export LD_LIBRARY_PATH=${DIR}/lib:${DIR}/3rdparty/onnxruntime/lib:${LD_LIBRARY_PATH}

# 设置模型路径
export MODEL_PATH=${DIR}/inf_src

# 运行程序
exec ${DIR}/bin/inf_qwq_backend "$@"
EOF

chmod +x ${PACKAGE_DIR}/run.sh

echo "Creating version file..."
echo "Version: $(date '+%Y%m%d')" > ${PACKAGE_DIR}/version.txt

echo "Pack up complete! Project tree: "
tree -L 3 ${PACKAGE_DIR}

# 可选：创建压缩包
echo "Creating package archive..."
tar -czf inf_qwq_package_$(date '+%Y%m%d').tar.gz ${PACKAGE_DIR}

