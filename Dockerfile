FROM alpine:3.19 AS build-dev

WORKDIR /app/src
COPY --link . .

RUN apk add --no-cache gcc musl-dev g++ cmake make linux-headers

ENV CC=/usr/bin/gcc \
    CXX=/usr/bin/g++

RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static" && \
    cmake --build build --target example-file

FROM scratch

COPY --from=build-dev /app/src/build/example/file/example-file /app/file

# scratch には /etc/passwd が無いので数値で指定する。これで Kubernetes 側の
# runAsNonRoot: true がイメージ単体でも満たせる。待ち受けは 8080 なので
# CAP_NET_BIND_SERVICE は不要。
USER 65532:65532

ENTRYPOINT ["/app/file"]
