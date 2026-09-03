FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Combine all package installations in one RUN block
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential coreutils cmake \
        python3-pip python3-dev \
        valgrind lcov doxygen \
        clang clang-tidy \
        software-properties-common \
        libxml2-dev libxslt1-dev \
        libgl1-mesa-dev libglu1-mesa-dev \
        git && \
    add-apt-repository universe && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Install Python packages
RUN pip3 install gcovr pexpect
