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
RUN mkdir build && cd build && cmake .. && make

#CMD ["ctest", "-V", "--test-dir", "./build/test"]
CMD ["./build/src/medtronic_task"]