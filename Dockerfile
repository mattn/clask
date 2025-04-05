FROM debian:buster-slim AS build-dev

WORKDIR /app/src
COPY --link . .

RUN apt-get update && \
    apt-get install -y gcc g++ cmake

ENV CC=/usr/bin/gcc \
    CXX=/usr/bin/g++

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target example-file

FROM bitnami/minideb:bookworm

RUN mkdir -p /app/public
COPY --from=build-dev /app/src/build/example/file/example-file /app/file
ENTRYPOINT ["/app/file"]
