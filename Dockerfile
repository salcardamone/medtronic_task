FROM ubuntu:jammy as builder
LABEL description="Build environment for Medtronic task"

RUN apt update && apt upgrade -y
RUN apt install -y \
    build-essential \
    gcc-11 g++-11 \
    cmake \
    googletest libgmock-dev \
    libspdlog-dev
    
WORKDIR /usr/src/medtronic_task
COPY . .
RUN mkdir build && cd build && cmake .. && make -j 4

ENV GTEST_COLOR=1
CMD ["bash", "docker_helper.sh"]
