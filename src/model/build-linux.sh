#!/bin/bash
GCC_COMPILER=/opt/atk-dlrk356x-toolchain/usr/bin/aarch64-buildroot-linux-gnu
set -e

echo "$0 $@"
while getopts ":t:a:d:b:m:r:j" opt; do
  case $opt in
    t)
    # 目标SOC型号
      TARGET_SOC=$OPTARG
      ;;
    a)
    #  SOC架构
      TARGET_ARCH=$OPTARG
      ;;
    b)
    # 编译类型
      BUILD_TYPE=$OPTARG
      ;;
    m)
      ENABLE_ASAN=ON
      export ENABLE_ASAN=TRUE
      ;;
    d)
    # 模型类型
      BUILD_DEMO_NAME=$OPTARG
      ;;
    r)
    # 禁用RGA
      DISABLE_RGA=ON
      ;;
    j)
    # 禁用libjpeg
      DISABLE_LIBJPEG=ON
      ;;
    :)
      echo "Option -$OPTARG requires an argument." 
      exit 1
      ;;
    ?)
      echo "Invalid option: -$OPTARG index:$OPTIND"
      ;;
  esac
done

# 用法:./build-linux.sh -t <target> -a <arch> -d <build_demo_name> [-b <build_type>] [-m]
# -t : target (rk356x/rk3588) # 平台类型，rk3568/rk3566 都统一为rk356x
# -a : arch (aarch64/armhf) # 板端系统架构
# -d : demo name # 对应 examples 目录下子文件夹的名称，如yolov5、mobilenet
# -b : build_type(Debug/Release)
# -m : enable address sanitizer, build_type need set to Debug
# ./build-linux.sh -t rk356x -a aarch64 -d yolov5

# 参数检查
if [ -z ${TARGET_SOC} ] || [ -z ${BUILD_DEMO_NAME} ]; then
  echo "$0 -t <target> -a <arch> -d <build_demo_name> [-b <build_type>] [-m] [-r] [-j]"
  echo ""
  echo "    -t : target (rk356x/rk3588/rk3576/rv1126b/rv1106/rk1808/rv1126)"
  echo "    -a : arch (aarch64/armhf)"
  echo "    -d : demo name"
  echo "    -b : build_type(Debug/Release)"
  echo "    -m : enable address sanitizer, build_type need set to Debug"
  echo "    -r : disable rga, use cpu resize image"
  echo "    -j : disable libjpeg to avoid conflicts between libjpeg and opencv"
  echo "such as: $0 -t rk3588 -a aarch64 -d mobilenet"
  echo "Note: 'rk356x' represents rk3562/rk3566/rk3568, 'rv1106' represents rv1103/rv1106, 'rv1126' represents rv1109/rv1126, 'rv1126b' is different from 'rv1126'"
  echo "Note: 'disable rga option is invalid for rv1103/rv1103b/rv1106"
  echo "Note: 'if you want to use opencv to read or save jpg files, use the '-j' option to disable libjpeg"
  echo ""
  exit -1
fi

# 设置默认编译器
if [[ -z ${GCC_COMPILER} ]];then
    if [[ ${TARGET_SOC} = "rv1106"  || ${TARGET_SOC} = "rv1103" ]];then
        echo "Please set GCC_COMPILER for $TARGET_SOC"
        echo "such as export GCC_COMPILER=~/opt/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf"
        exit
    elif [[ ${TARGET_SOC} = "rv1109" || ${TARGET_SOC} = "rv1126" ]];then
        GCC_COMPILER=arm-linux-gnueabihf
    else
        GCC_COMPILER=aarch64-linux-gnu
    fi
fi
echo "$GCC_COMPILER"
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

# 检查编译器是否可用
if command -v ${CC} >/dev/null 2>&1; then
    :
else
    echo "${CC} is not available"
    echo "Please set GCC_COMPILER for $TARGET_SOC"
    echo "such as export GCC_COMPILER=~/opt/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf"
    exit
fi

# Debug / Release
if [[ -z ${BUILD_TYPE} ]];then
    BUILD_TYPE=Release
fi

# Build with Address Sanitizer for memory check, BUILD_TYPE need set to Debug
if [[ -z ${ENABLE_ASAN} ]];then
    ENABLE_ASAN=OFF
fi

if [[ -z ${DISABLE_RGA} ]];then
    DISABLE_RGA=OFF
fi

if [[ -z ${DISABLE_LIBJPEG} ]];then
    DISABLE_LIBJPEG=OFF
fi

# 从examples目录下查找demo路径
for demo_path in `find examples -name ${BUILD_DEMO_NAME}`
do
    if [ -d "$demo_path/cpp" ]
    then
        # 形如 examples/yolox/cpp 
        BUILD_DEMO_PATH="$demo_path/cpp"
        break;
    fi
done

# 检查模型路径是否有效 (为空进入if)
if [[ -z "${BUILD_DEMO_PATH}" ]]
then
    echo "Cannot find demo: ${BUILD_DEMO_NAME}, only support:"
    # 列出所有支持CPP转换的模型类型 examples/yolox/cpp
    for demo_path in `find examples -name cpp`
    do
        if [ -d "$demo_path" ]
        then
            # 输出模型类型
            dname=`dirname "$demo_path"`    # examples/yolox
            name=`basename $dname`          # yolox
            echo "$name"
        fi
    done
    echo "rv1106_rv1103 only support: mobilenet and yolov5/6/7/8/x"
    exit
fi

# 规范化target_soc
case ${TARGET_SOC} in
    rk356x)
        ;;
    rk3588)
        ;;
    rv1106)
        ;;
    rv1103)
        TARGET_SOC="rv1106"
        ;;
    rk3566)
        TARGET_SOC="rk356x"
        ;;
    rk3568)
        TARGET_SOC="rk356x"
        ;;
    rk3562)
        TARGET_SOC="rk356x"
        ;;
    rk3576)
        TARGET_SOC="rk3576"
        ;;
    rk1808):
        TARGET_SOC="rk1808"
        ;;
    rv1109)
        ;;
    rv1126)
        TARGET_SOC="rv1126"
        ;;
    rv1126b)
        TARGET_SOC="rv1126b"
        ;;
    *)
        echo "Invalid target: ${TARGET_SOC}"
        echo "Valid target: rk3562,rk3566,rk3568,rk3588,rk3576,rv1106,rv1103,rk1808,rv1109,rv1126,rv1126b"
        exit -1
        ;;
esac

# install 路径构建
TARGET_SDK="rknn_${BUILD_DEMO_NAME}_demo"

TARGET_PLATFORM=${TARGET_SOC}_linux
if [[ -n ${TARGET_ARCH} ]];then
TARGET_PLATFORM=${TARGET_PLATFORM}_${TARGET_ARCH}
fi
# 先切换到脚本所在目录 再使用 -P 切换到实际物理路径(避免软链接问题)
ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )
# 基于脚本路径的上级目录创建 install 和 build 目录
INSTALL_DIR=${ROOT_PWD}/install/${TARGET_PLATFORM}/${TARGET_SDK}
BUILD_DIR=${ROOT_PWD}/build/build_${TARGET_SDK}_${TARGET_PLATFORM}_${BUILD_TYPE}
# 输出相关信息
echo "==================================="
echo "BUILD_DEMO_NAME=${BUILD_DEMO_NAME}"
echo "BUILD_DEMO_PATH=${BUILD_DEMO_PATH}"
echo "TARGET_SOC=${TARGET_SOC}"
echo "TARGET_ARCH=${TARGET_ARCH}"
echo "BUILD_TYPE=${BUILD_TYPE}"
echo "ENABLE_ASAN=${ENABLE_ASAN}"
echo "DISABLE_RGA=${DISABLE_RGA}"
echo "DISABLE_LIBJPEG=${DISABLE_LIBJPEG}"
echo "INSTALL_DIR=${INSTALL_DIR}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "CC=${CC}"
echo "CXX=${CXX}"
echo "==================================="
# 对文件夹初始化
if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

if [[ -d "${INSTALL_DIR}" ]]; then
  rm -rf ${INSTALL_DIR}
fi

cd ${BUILD_DIR}
# 编译可执行文件
cmake ../../${BUILD_DEMO_PATH} \
    -DTARGET_SOC=${TARGET_SOC} \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=${TARGET_ARCH} \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DDISABLE_RGA=${DISABLE_RGA} \
    -DDISABLE_LIBJPEG=${DISABLE_LIBJPEG} \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}
make -j4        # 多线程编译
make install    # 安装到指定目录

# 检查模型文件和install目录是否存在
suffix=".rknn"
shopt -s nullglob # Enable nullglob to handle no matches
if [ -d "$INSTALL_DIR" ]; then
    files=("$INSTALL_DIR/model/"/*"$suffix")
    shopt -u nullglob

    if [ ${#files[@]} -le 0 ]; then
        # No rknn model found
        echo -e "\e[91mThe RKNN model can not be found in \"$INSTALL_DIR/model\", please check!\e[0m"
    fi
else
    # INSTALL_DIR does not exist
    echo -e "\e[91mInstall directory \"$INSTALL_DIR\" does not exist, please check!\e[0m"
fi
