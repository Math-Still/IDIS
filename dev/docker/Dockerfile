# Windows/Docker development image. Final delivery is native on the target domestic OS.
FROM node:22-bookworm AS trusted-base

FROM trusted-base AS web-build
ARG NPM_REGISTRY=https://registry.npmjs.org
WORKDIR /workspace
RUN npm install --global npm@10.9.2 \
    && npm config set registry "${NPM_REGISTRY}"
COPY . .
RUN npm ci --no-audit --no-fund
RUN npm run build

FROM debian:bookworm AS cpp-build
# Minimal Debian images lack CA roots; bootstrap from the trusted Node base.
COPY --from=trusted-base /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
RUN sed -i 's#http://deb.debian.org#https://deb.debian.org#g' /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get install -y --no-install-recommends \
    g++ cmake ninja-build libboost-json1.81-dev libsqlite3-dev libssl-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
COPY backend-cpp ./backend-cpp
RUN cmake -S backend-cpp -B build/backend -G Ninja -DSMART_FACTORY_BUILD_TESTS=ON \
    && cmake --build build/backend -j2 \
    && ctest --test-dir build/backend --output-on-failure

FROM debian:bookworm-slim AS runtime
COPY --from=trusted-base /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
RUN sed -i 's#http://deb.debian.org#https://deb.debian.org#g' /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get install -y --no-install-recommends libstdc++6 libsqlite3-0 libssl3 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /opt/smart-factory
COPY --from=cpp-build /workspace/build/backend/smart-factory-backend ./smart-factory-backend
COPY --from=web-build /workspace/apps/web/dist ./web
COPY backend-cpp/config/backend.development.conf ./backend.conf
COPY backend-cpp/config ./backend-cpp/config
RUN sed -i 's#web_root=apps/web/dist#web_root=/opt/smart-factory/web#' ./backend.conf
EXPOSE 8000
CMD ["./smart-factory-backend", "--config", "./backend.conf"]
