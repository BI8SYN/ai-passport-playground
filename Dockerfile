# syntax=docker/dockerfile:1
ARG IDF_IMAGE=espressif/idf:v5.5.3@sha256:8ccd4d2ce413889c6c2bba57e986c670302094efb91c913c6091152e317a7805
FROM ${IDF_IMAGE}

# Upstream installer verifies the pinned actionlint 1.7.12 archive checksum.
COPY vendor/ai-passport/tools/install-actionlint.sh /tmp/install-actionlint.sh
RUN --mount=type=cache,target=/var/cache/passport-tools,sharing=locked \
    bash /tmp/install-actionlint.sh /var/cache/passport-tools/actionlint-1.7.12 \
    && install -m 0755 /var/cache/passport-tools/actionlint-1.7.12/actionlint /usr/local/bin/actionlint \
    && rm /tmp/install-actionlint.sh \
    && ccache --version

WORKDIR /workspace
# Keep Espressif's entrypoint: it activates the pinned IDF Python environment.
