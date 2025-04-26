#!bin/bash
set -e

BUILD_DIR="build"
PACKAGE_DIR="package"

rm -rf ${PACKAGE_DIR}
mkdir -p ${PACKAGE_DIR}/{bin,lib,3rdparty,onnxruntime,models,plugins}

echo "Copying executable file..."
cp -v ${BUILD_DIR}/bin/inf_qwq ${PACKAGE_DIR}/bin

echo "Collecting dynamic dependencies..."
ldd ${BUILD_DIR}/bin/inf_qwq | awk '/=>/ {print $3}' | xagrs -I{} cp -Lv {} ${PACKAGE_DIR}/lib/

echo "Collecting 3rdparty"
cp -rv 3rdparty/onnxruntime ${PACKAGE_DIR}/3rdparty

echo "Fixing rpath..."
patchelf --set-rpath '$ORIGIN/../lib' ${PACKAGE_DIR}/bin/inf_qwq

