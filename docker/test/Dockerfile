FROM ubuntu:latest

LABEL description="A docker image for testing hgdb"
LABEL maintainer="keyi@cs.stanford.edu"
LABEL url="https://github.com/Kuree/hgdb"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential cmake verilator iverilog \
        python3 python3-dev python3-pip python3-wheel && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# install python packages
RUN update-alternatives --install /usr/bin/python python /usr/bin/python3 10
RUN update-alternatives --install /usr/bin/pip pip /usr/bin/pip3 10

RUN pip install -U  --no-cache-dir pytest pytest-cpp websockets
