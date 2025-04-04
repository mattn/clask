FROM debian:buster-slim AS build-dev

WORKDIR /app/src
COPY --link . .

RUN apt-get update && \
    apt-get install -y gcc g++ cmake

ENV CC=/usr/bin/gcc \
    CXX=/usr/bin/g++

RUN mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON -DCMAKE_CXX_FLAGS="-Werror" .. && make example-file

FROM scratch

COPY --from=build-dev /app/src/build/example/file/example-file /app/file
ENTRYPOINT ["/app/file"]
