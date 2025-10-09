# ================== 构建阶段 ==================
FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y \
    build-essential cmake pkg-config git \
    libcurl4-openssl-dev \
    libnuma-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

WORKDIR /app/StreamRelay
RUN chmod +x ./build.sh && ./build.sh Release

# ================== 运行阶段 ==================
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libcurl4 \
    libnuma1 \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/StreamRelay/build/Release/bin/Release /app/bin
WORKDIR /app/bin

# 设置运行时 LD_LIBRARY_PATH
ENV LD_LIBRARY_PATH=/app/bin:$LD_LIBRARY_PATH

CMD ["./StreamRelayServer"]
