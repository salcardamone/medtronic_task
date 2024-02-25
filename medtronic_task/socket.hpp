/**
 * @brief
 */
#ifndef __MEDTRONIC_TASK_SOCKET_HPP
#define __MEDTRONIC_TASK_SOCKET_HPP

// C++ Standard Libraries
#include <cerrno>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
// Third-Party Libraries
#include "spdlog/spdlog.h"
// Project Inclusions
//

namespace medtronic {

/**
 * @brief Virtual interface for client socket; allows us to inject a mock during
 * testing.
 */
class ClientSocketInterface {
 public:
  /**
   * @brief Virtual destructor.
   */
  virtual ~ClientSocketInterface() {}

  /**
   * @brief Establish a connection with a remote host.
   * @return 0 one success, 1 on failure.
   */
  virtual int connectx() = 0;

  /**
   * @brief Attempt to send data to a remote host.
   * @param data The stringified data to send.
   * @return Number of bytes sent; <= 0 on error.
   */
  virtual int sendx(const std::string& data) = 0;

  /**
   * @brief Getter for the hostname; lets us use it in the HTTP POST header.
   * @return The hostname.
   */
  virtual const std::string& host() const = 0;
};

/**
 * @brief Concrete implementation of ClientSocketInterface.
 */
class ClientSocket : public ClientSocketInterface {
 public:
  /**
   * @brief Class constructor.
   * @param host Hostname we're trying to connect to.
   * @param port Port we're trying to connect to.
   */
  ClientSocket(std::string host, int port) : host_{host}, port_{port} {
    connectx();
  }

  /**
   * @brief Class destructor.
   */
  ~ClientSocket() override { close(sockfd_); }

  /**
   * @brief Getter for the hostname; lets us use it in the HTTP POST header.
   * @return The hostname.
   */
  const std::string& host() const { return host_; };

  /**
   * @brief Establish a connection with a remote host.
   * @return 0 one success, 1 on failure.
   */
  int connectx() override {
    // We want the socket to be non-blocking so that if the socket buffer
    // overflows, then the subsequent send() won't block, but will return an
    // EWOULDBLOCK which we can use to trigger re-connection attempts
    sockfd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd_ == -1) {
      spdlog::error("Error creating socket.");
      //std::cerr << "Error creating socket." << std::endl;
      return -1;
    }

    struct hostent* he = gethostbyname(host_.c_str());
    if (he == nullptr) {
      spdlog::error("Error resolving hostname.");
      close(sockfd_);
      return -1;
    }

    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port_);
    server_addr_.sin_addr = *((struct in_addr*)he->h_addr);

    int connect_status =
        connect(sockfd_, (struct sockaddr*)&server_addr_, sizeof(server_addr_));

    // Note that the socket is in non-blocking mode, so connect() won't block
    // and will return -1. We need to check that the connection is in
    // progress -- if so, we're all good, otherwise we've got a legitimate error
    if (connect_status == -1) {
      if (errno != EINPROGRESS) {
	spdlog::error("Error connecting to server: {}", std::strerror(errno));
        close(sockfd_);
        return -1;
      }
    }
    spdlog::info("Successfully connected to host: {}", host());
    return 0;
  }

  /**
   * @brief Attempt to send data to a remote host.
   * @param data The stringified data to send.
   * @return Number of bytes sent; <= 0 on error.
   */
  int sendx(const std::string& data) override {
    // Send the request
    int num_bytes = send(sockfd_, data.c_str(), data.length(), 0);
    // socket buffer has reached capacity -- need to try and re-establish
    // connection to host
    if (num_bytes == -1) {
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
	spdlog::error("Socket buffer full. Need to re-establish connection.");
      } else {
	spdlog::error("Unknown error on send: {}", std::strerror(errno));
      }
      close(sockfd_);
      return -1;
    }
    return num_bytes;
  }

 private:
  std::string host_;
  int port_;
  int sockfd_;
  struct sockaddr_in server_addr_;
};

}  // namespace medtronic

#endif /* #ifndef __MEDTRONIC_TASK_SOCKET_HPP */
