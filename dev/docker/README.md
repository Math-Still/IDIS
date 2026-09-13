# Docker Development Build

The Dockerfile provides a reproducible development build for the Vue application and C++ backend. It is not the final native deployment architecture for a domestic OS target.

Build the image:

```bash
docker build -f dev/docker/Dockerfile -t idis:1.8.0 .
```

Or use the local compose profile:

```bash
docker compose -f deploy/local/docker-compose.yml up -d --build
```

The compose profile exposes the development service on `127.0.0.1:8001`.
