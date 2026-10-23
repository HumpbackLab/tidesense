# ROS 2 Humble supplies the host ament/rosidl generators used by micro-ROS.
FROM ros:humble-ros-base-jammy

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake bc bison build-essential ca-certificates cmake cpio curl \
    device-tree-compiler dosfstools e2fsprogs file flex gawk gettext git \
    kmod libboost1.74-dev libc6-i386 libncurses-dev libpcre3 libssl-dev libtool \
    mtools ninja-build patch pkg-config python-is-python3 python3-dev \
    python3-pip python3-pycryptodome python3-venv python3-yaml rsync \
    texinfo unzip wget xz-utils zip zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /sdk
