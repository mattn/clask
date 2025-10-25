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
ENTRYPOINT ["/app/file"]
