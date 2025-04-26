#!/bin/bash

set -e

BUILD_DIR="build"
PACKAGE_DIR="package"
QT_PLUGINS_SRC="/usr/lib/qt6/plugins"
X11_LIB_DIR="/usr/lib/x86_64-linux-gnu"

rm -rf ${PACKAGE_DIR}
mkdir -p ${PACKAGE_DIR}/{bin,lib,3rdparty/onnxruntime,models,plugins}

echo "Copying executable file..."
cp -v ${BUILD_DIR}/bin/inf_qwq ${PACKAGE_DIR}/bin/

echo "Collecting dynamic dependencies..."
ldd ${BUILD_DIR}/bin/inf_qwq | awk '/=>/ {print $3}' | xargs -I{} cp -Lv {} ${PACKAGE_DIR}/lib/

echo "Collecting 3rdparty"
cp -rv 3rdparty/onnxruntime ${PACKAGE_DIR}/3rdparty

echo "Fixing rpath..."
patchelf --set-rpath '$ORIGIN/../lib' ${PACKAGE_DIR}/bin/inf_qwq

echo "Configuring Qt plugins"
mkdir -p ${PACKAGE_DIR}/lib/qt6/{plugins,qml}

cp -rv ${QT_PLUGINS_SRC}/platforms ${PACKAGE_DIR}/lib/qt6/plugins/
cp -rv ${QT_PLUGINS_SRC}/imageformats ${PACKAGE_DIR}/lib/qt6/plugins/
cp -rv ${QT_PLUGINS_SRC}/sqldrivers ${PACKAGE_DIR}/lib/qt6/plugins/

find ${PACKAGE_DIR}/lib/qt6/plugins -name "*.so" -exec patchelf --set-rpath '$ORIGIN/../../..' {} \;

# echo "Copying X11 relative libs"
# cp -v ${X11_LIB_DIR}/libxcb*.so* ${PACKAGE_DIR}/lib/
# cp -v ${X11_LIB_DIR}/libX11*.so* ${PACKAGE_DIR}/lib/

echo "Configuring SQL drivers..."
cp -v /usr/lib/libpq.so.5 ${PACKAGE_DIR}/lib/
cp -v /usr/lib/libQt6Sql.so.6 ${PACKAGE_DIR}/lib/

echo "Moving inferring models..."
cp ${BUILD_DIR}/*.onnx ${PACKAGE_DIR}
cp -r ${BUILD_DIR}/inf_src ${PACKAGE_DIR}

echo "Generating launching..."
cat > ${PACKAGE_DIR}/run.sh << 'EOF'
#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export LD_LIBRARY_PATH=${DIR}/lib:${DIR}/3rdparty/onnxruntime/lib
export QT_QPA_PLATFORM_PLUGIN_PATH=${DIR}/lib/qt6/plugins/platforms
export QT_PLUGIN_PATH=${DIR}/lib/qt6/plugins

exec ${DIR}/bin/inf_qwq "$@"
EOF

chmod +x ${PACKAGE_DIR}/run.sh
echo "Pack up! Project tree: "
tree -L 3 ${PACKAGE_DIR}
