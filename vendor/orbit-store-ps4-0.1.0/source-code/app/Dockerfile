# Build image for the native TV app: the PS4 app, PC previews and packaging.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      bash ca-certificates curl wget git unzip tar xz-utils zstd make ninja-build ccache pkg-config \
      clang clang-18 lld-18 llvm-18 clang-format-18 clang-tidy-18 \
      golang \
      python3 python3-venv python3-pil \
      libegl-dev libgles-dev mesa-common-dev libegl1 libegl-mesa0 libgl1-mesa-dri libwebp-dev \
      dotnet-sdk-8.0 \
    && rm -rf /var/lib/apt/lists/*
# Sanitizer runtimes for `make test` (aarch64 needs libclang-rt-18-dev; the
# clang-18 metapackage does not pull it).
RUN apt-get update && apt-get install -y --no-install-recommends libclang-rt-18-dev \
    && rm -rf /var/lib/apt/lists/*
ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
WORKDIR /work/app
