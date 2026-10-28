# 开发环境安装

以下两种方式提供工程所需的编译依赖。获取源码后，在仓库根目录执行。

## 方式一：使用 Docker 或 Podman

Docker：

```bash
docker pull ghcr.io/humpbacklab/tidesense-dev:ubuntu24.04
SDK_ROOT=$(pwd -P)
docker run --rm -it --privileged --network=host \
  -v "$SDK_ROOT:$SDK_ROOT" -w "$SDK_ROOT" \
  ghcr.io/humpbacklab/tidesense-dev:ubuntu24.04 bash
```

Podman：

```bash
podman pull ghcr.io/humpbacklab/tidesense-dev:ubuntu24.04
TIDESENSE_DEV_IMAGE=ghcr.io/humpbacklab/tidesense-dev:ubuntu24.04 ./scripts/dev-container.sh shell
```

仓库挂载进容器。Docker 默认以 root 运行，生成文件可能归 root 所有。

## 方式二：直接安装依赖（Ubuntu 24.04 x86_64）

安装与 [Containerfile.dev](../Containerfile.dev) 相同的系统工具：

```bash
sudo apt update
sudo apt install -y --no-install-recommends \
  autoconf automake bc binutils bison build-essential bzip2 ccache cmake \
  cpio device-tree-compiler dosfstools e2fsprogs file flex gawk gettext \
  git git-lfs libelf-dev libncurses-dev libssl-dev libtool libudev-dev \
  libusb-1.0-0-dev libxml2-dev lz4 mtools ninja-build openssh-client \
  patch pkg-config python3 python3-dev python3-pip python3-setuptools \
  python3-venv python3-yaml python3-jinja2 python3-colorama python3-pycryptodome \
  python-is-python3 rsync sshpass swig tar texinfo unzip wget xz-utils zip \
  zlib1g-dev kmod libpcre3 squashfs-tools podman ca-certificates curl gnupg
```

添加 ROS 2 软件源，再安装所选发行版的开发包；SDK 不限定 ROS 2 发行版：

```bash
sudo install -d -m 0755 /usr/share/keyrings
curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key | \
  sudo tee /usr/share/keyrings/ros-archive-keyring.gpg > /dev/null
echo 'deb [arch=amd64 signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu noble main' | \
  sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
sudo apt update
ROS_DISTRO=jazzy  # 可改为 Ubuntu 24.04 上可用的 ROS 2 发行版
sudo apt install -y --no-install-recommends \
  "ros-$ROS_DISTRO-ros-base" "ros-$ROS_DISTRO-rmw-cyclonedds-cpp" ros-dev-tools
export TIDESENSE_ROS_SETUP="/opt/ros/$ROS_DISTRO/setup.bash"
```

SchurVINS 使用 Boost 1.74 头文件；Ubuntu 24.04 可按镜像的方式单独提取：

```bash
BOOST_DIR="$HOME/.local/share/tidesense-boost-1.74"
mkdir -p "$BOOST_DIR"
curl -fsSL https://archive.ubuntu.com/ubuntu/pool/main/b/boost1.74/libboost1.74-dev_1.74.0-14ubuntu3_amd64.deb \
  -o "$BOOST_DIR/libboost1.74-dev.deb"
dpkg-deb --fsys-tarfile "$BOOST_DIR/libboost1.74-dev.deb" | \
  tar --no-same-owner --no-same-permissions --no-overwrite-dir -xf - -C "$BOOST_DIR"
export BOOST_HEADERS="$BOOST_DIR/usr/include/boost"
```

按镜像使用的版本安装 [Node.js 22](https://nodejs.org/en/download/archive/v22)，再安装 TideConfigurator 的 Python 和前端依赖：

```bash
NODE_VERSION=v22.23.4
NODE_DIR="$HOME/.local/opt/node-$NODE_VERSION"
mkdir -p "$NODE_DIR"
curl -fsSL "https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-linux-x64.tar.xz" | \
  tar -xJ --strip-components=1 -C "$NODE_DIR"
export PATH="$NODE_DIR/bin:$PATH"
python3 -m venv tide_configurator/.venv
tide_configurator/.venv/bin/pip install -r tide_configurator/requirements.txt
npm --prefix tide_configurator/frontend ci
npm --prefix tide_configurator/frontend run build
```

直接安装时，编译前需确保 `TIDESENSE_ROS_SETUP`、`BOOST_HEADERS` 和 Node.js 路径仍在当前 shell 中。完成任一方式后，都按主 README 的[编译章节](../README.md#编译)执行相同的命令；各子项目按其 README 操作。
