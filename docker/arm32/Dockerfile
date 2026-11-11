# Native aarch64/amd64 image used to cross-compile Phaser for armhf with Bazel.
# Colima on Apple Silicon runs this as linux/arm64; qemu-user-static runs the
# armhf binaries it produces when tests use --config=linux_arm32.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        openjdk-21-jdk-headless \
        python3 \
        unzip \
        zip \
        gcc-arm-linux-gnueabihf \
        g++-arm-linux-gnueabihf \
        libc6-dev-armhf-cross \
        qemu-user-static \
    && rm -rf /var/lib/apt/lists/*

# Bazelisk picks the version from .bazelversion in the mounted repo.
RUN arch="$(uname -m)"; \
    case "${arch}" in \
      aarch64) bazelisk_arch=arm64 ;; \
      x86_64) bazelisk_arch=amd64 ;; \
      *) echo "unsupported docker arch: ${arch}" >&2; exit 1 ;; \
    esac; \
    curl -fsSL -o /usr/local/bin/bazelisk \
      "https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-${bazelisk_arch}" && \
    chmod +x /usr/local/bin/bazelisk && \
    ln -sf /usr/local/bin/bazelisk /usr/local/bin/bazel
