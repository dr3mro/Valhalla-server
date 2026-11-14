# syntax=docker/dockerfile:1

FROM alpine:3.24@sha256:f59abe656b692d901e184be3445e084786602108599d2a51d229c0f5a48c71bf AS builder

RUN apk add --no-cache \
    build-base cmake samurai linux-headers perl bash git \
    autoconf automake libtool make pkgconf bison flex \
    python3 py3-pip

RUN python3 -m venv /opt/conan \
    && /opt/conan/bin/pip install --no-cache-dir 'conan>=2,<3'
ENV PATH="/opt/conan/bin:${PATH}"

WORKDIR /src

# Dependencies get their own layer so they are rebuilt only when conanfile.txt changes.
# ConanCenter binaries target glibc, so build everything from source; its cmake recipe
# only repackages Kitware's glibc binary, so use Alpine's cmake instead.
# CMAKE_POLICY_VERSION_MINIMUM lets CMake 4 configure dependencies that declare < 3.5.
# Old gnulib-based build tools don't compile against Alpine's fortify-headers; they never ship.
ARG BUILD_TOOLS_NO_FORTIFY="m4 bison flex"
COPY conanfile.txt .
RUN conan profile detect --force \
    && printf '\n[platform_tool_requires]\ncmake/%s\n' "$(cmake --version | awk 'NR==1{print $3}')" \
       >> "$(conan profile path default)" \
    && printf '\n[conf]\n' >> "$(conan profile path default)" \
    && for t in ${BUILD_TOOLS_NO_FORTIFY}; do \
         printf '%s/*:tools.build:cflags=["-U_FORTIFY_SOURCE"]\n' "$t" >> "$(conan profile path default)"; \
       done \
    && conan install . --build="*" -s build_type=Release -s compiler.cppstd=gnu20 \
       -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" \
       -c "tools.cmake.cmaketoolchain:extra_variables={'CMAKE_POLICY_VERSION_MINIMUM': '3.5'}" \
    && conan cache clean "*" --source --build --temp

COPY CMakeLists.txt get_tag.sh ./
COPY tools/cmake tools/cmake
COPY tools/conan tools/conan
COPY docs/api.html docs/api.html
COPY src src
COPY tests tests

# The unit tests run against the same toolchain that produces the shipped binary
ARG VALHALLA_VERSION=""
RUN VALHALLA_VERSION="${VALHALLA_VERSION}" cmake -S . -B build/Release -G Ninja \
       -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DVALHALLA_BUILD_DOCS=OFF \
    && cmake --build build/Release -j"$(nproc)" --target server unit_tests \
    && ctest --test-dir build/Release --output-on-failure -j"$(nproc)" \
    && strip build/Release/server


FROM alpine:3.24@sha256:f59abe656b692d901e184be3445e084786602108599d2a51d229c0f5a48c71bf

RUN apk add --no-cache postgresql17 postgresql17-client su-exec tini libstdc++ libgcc \
    && addgroup -S valhalla \
    && adduser -S -D -H -G valhalla -h /app valhalla

WORKDIR /app

COPY --from=builder /src/build/Release/server /app/server
COPY init.sql /app/init.sql
COPY --chmod=755 docker/entrypoint.sh /entrypoint.sh

ENV SERVER_PORT=8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=30s --retries=5 \
    CMD wget -qO /dev/null "http://127.0.0.1:${SERVER_PORT}/api/v2/hello" || exit 1

EXPOSE 8080

ENTRYPOINT ["/sbin/tini", "--", "/entrypoint.sh"]
CMD ["/app/server"]
