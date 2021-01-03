FROM ubuntu:rolling

LABEL description="A docker image for linting hgdb"
LABEL maintainer="keyi@cs.stanford.edu"
LABEL url="https://github.com/Kuree/hgdb"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential cmake clang-tidy clang-format  && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

