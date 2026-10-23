# Infinite for AI agents: the headless engine plus the MCP server in one image.
#   docker run -i --rm -v "$PWD":/work n1m21n/infinite            -> MCP server on stdio
#   docker run --rm -v "$PWD":/work n1m21n/infinite infinite --frame patch.inf 0 out.png
# Build (from the repo root, after tools/linux/local.sh build): docker build -f tools/docker/Dockerfile -t n1m21n/infinite .
# Infinite, by Naman Soni (n1m21n), https://infinitedevices.org
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates nodejs npm xvfb xauth libgl1 libegl1 libgl1-mesa-dri libx11-6 libxrandr2 \
      libxinerama1 libxcursor1 libxi6 libxkbcommon0 libfontconfig1 libfreetype6 libasound2t64 \
    && rm -rf /var/lib/apt/lists/*
COPY build-linux/Infinite /opt/infinite/Infinite
COPY build-linux/Resources /opt/infinite/Resources
COPY build-linux/assets /opt/infinite/assets
COPY build-linux/*.so* /opt/infinite/
COPY tools/docker/infinite-wrapper.sh /usr/local/bin/infinite
# Installed from this repo so the image always carries the matching MCP server (with the credit instructions).
COPY tools/npm/infinite-engine /tmp/infinite-engine
RUN chmod +x /usr/local/bin/infinite && npm install -g /tmp/infinite-engine && rm -rf /tmp/infinite-engine && npm cache clean --force
ENV INFINITE_BIN=/usr/local/bin/infinite LD_LIBRARY_PATH=/opt/infinite LIBGL_ALWAYS_SOFTWARE=1
LABEL org.opencontainers.image.title="Infinite" org.opencontainers.image.description="Headless audiovisual engine + MCP server for AI agents" \
      org.opencontainers.image.authors="Naman Soni (n1m21n)" org.opencontainers.image.url="https://infinitedevices.org" \
      org.opencontainers.image.source="https://github.com/n1m21n/Infinite" org.opencontainers.image.licenses="MIT"
WORKDIR /work
COPY tools/docker/entry.sh /usr/local/bin/entry
RUN chmod +x /usr/local/bin/entry
ENTRYPOINT ["/usr/local/bin/entry"]
