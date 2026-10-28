# syntax=docker/dockerfile:1
ARG IDF_IMAGE=espressif/idf:v5.5.3@sha256:8ccd4d2ce413889c6c2bba57e986c670302094efb91c913c6091152e317a7805
ARG ACTIONLINT_IMAGE=rhysd/actionlint:1.7.12@sha256:b1934ee5f1c509618f2508e6eb47ee0d3520686341fec936f3b79331f9315667
FROM ${ACTIONLINT_IMAGE} AS actionlint
FROM ${IDF_IMAGE}

# Copy the static binary from its publisher's immutable multi-platform image.
# No package-manager or GitHub-release downloads occur in a RUN layer.
COPY --from=actionlint /usr/local/bin/actionlint /usr/local/bin/actionlint
RUN ccache --version && /usr/local/bin/actionlint --version

WORKDIR /workspace
# Keep Espressif's entrypoint: it activates the pinned IDF Python environment.
