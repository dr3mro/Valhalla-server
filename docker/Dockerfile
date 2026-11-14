# syntax=docker/dockerfile:1

# ---------------------------------------------------------------------------
# Stage 1: local-deps
# Used only when DEPS_IMAGE is not overridden (local `docker build` without
# --build-arg). CI passes DEPS_IMAGE=dr3mro/valhalla-server-deps:<arch> so
# this stage is never referenced and BuildKit skips it entirely.
# ---------------------------------------------------------------------------
FROM alpine:3.24@sha256:f59abe656b692d901e184be3445e084786602108599d2a51d229c0f5a48c71bf AS local-deps

RUN apk add --no-cache \
    build-base cmake samurai linux-headers perl bash git \
    autoconf automake libtool make pkgconf bison flex \
    python3 py3-pip ccache

RUN python3 -m venv /opt/conan \
    && /opt/conan/bin/pip install --no-cache-dir 'conan==2.33.0'
ENV PATH="/opt/conan/bin:${PATH}"

WORKDIR /src

ARG BUILD_TOOLS_NO_FORTIFY="m4 bison flex"
COPY conanfile.txt conan.lock ./
RUN conan profile detect --force \
    && printf '\n[platform_tool_requires]\ncmake/%s\n' "$(cmake --version | awk 'NR==1{print $3}')" \
       >> "$(conan profile path default)" \
    && printf '\n[conf]\n' >> "$(conan profile path default)" \
    && for t in ${BUILD_TOOLS_NO_FORTIFY}; do \
         printf '%s/*:tools.build:cflags=["-U_FORTIFY_SOURCE"]\n' "$t" >> "$(conan profile path default)"; \
       done \
    && conan install . --build="*" --lockfile=conan.lock --lockfile-partial \
       -s build_type=Release -s compiler.cppstd=gnu20 \
       -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]" \
       -c "tools.cmake.cmaketoolchain:extra_variables={'CMAKE_POLICY_VERSION_MINIMUM': '3.5'}" \
    && conan cache clean "*" --source --build --temp

# ---------------------------------------------------------------------------
# Stage 2: builder
# In CI: FROM dr3mro/valhalla-server-deps:<arch> (Conan already installed).
# Locally: FROM local-deps (built above).
# ccache persists across builds via a per-arch BuildKit cache mount stored
# in the GHA layer cache.
# ---------------------------------------------------------------------------
ARG DEPS_IMAGE=local-deps
FROM ${DEPS_IMAGE} AS builder

WORKDIR /src

COPY conanfile.txt conan.lock ./
COPY CMakeLists.txt get_tag.sh ./
COPY tools/cmake tools/cmake
COPY tools/conan tools/conan
COPY docs/api.html docs/api.html
COPY src src
COPY tests tests

ARG VALHALLA_VERSION=""
RUN --mount=type=cache,id=ccache-$TARGETARCH,target=/root/.ccache \
    export CCACHE_DIR=/root/.ccache \
    && export PATH="/usr/lib/ccache/bin:$PATH" \
    && VALHALLA_VERSION="${VALHALLA_VERSION}" cmake -S . -B build/Release -G Ninja \
       -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DVALHALLA_BUILD_DOCS=OFF \
       -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    && cmake --build build/Release -j"$(nproc)" --target server unit_tests \
    && ctest --test-dir build/Release --output-on-failure -j"$(nproc)" \
    && strip build/Release/server

# ---------------------------------------------------------------------------
# Stage 3: runtime
# ---------------------------------------------------------------------------
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
