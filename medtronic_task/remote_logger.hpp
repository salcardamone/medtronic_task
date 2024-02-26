/**
 * @brief Implementation of remote logger; takes states from sensors and dumps
 * them into a socket whose endpoint should be a remote host receiving HTTP POST
 * requests.
 */
#ifndef __MEDTRONIC_TASK_REMOTE_LOGGER_HPP
#define __MEDTRONIC_TASK_REMOTE_LOGGER_HPP

// C++ Standard Libraries
#include <list>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <condition_variable>
// Third-Party Libraries
#include "spdlog/spdlog.h"
// Project Inclusions
#include "medtronic_task/socket.hpp"

namespace medtronic {

/**
 * There are two ways we can log state asynchronously:
 *
 * (1) Producer/ Consumer
 *     log_state() dumps data to a FIFO. Another thread (the consumer)
 *     is waiting for data in the FIFO, at which point it pops any data and
 *     dumps it across the network to the remote host. The FIFO is shared
 *     between producer and consumer threads, and has to be protected for any
 *     data races.
 * (2) Futures
 *     Each time log_state() gets called, it spawns a thread that tries to dump
 *     data across the network to the remote host. The future associated with
 *     that thread is kept in a container, and the container is periodically
 *     purged of futures that are ready (when the data in that thread has been
 *     sent).
 *
 * In the case where we lose network connectivity, (1) is preferable since
 * there's only a single thread trying to reconnect to the remote host, as
 * opposed to (2) where multiple threads are potentially trying to reconnect.
 * Also, if we need to dump to disk because of a poweroff, (1) is preferable
 * since a single thread can just flush the FIFO to disk (and there's no trivial
 * way to extract arguments passed to threads; we'd have to retain additional
 * state).
 *
 * So, this class is architected as follows:
 * - A single injection thread is created, waiting for data to be pushed to an
 *   injection buffer.
 * - When a sensor wants to log state, it calls the log_state() method. This
 *   acquires the mutex guarding the injection buffer and places the state
 *   there, before returning.
 * - The injection thread sees the injection buffer has pending data. It
 *   acquires the mutex guarding the injection buffer and takes any data that
 * was waiting there.
 * - The injection thread releases the mutex guarding the injection buffer,
 *   allowing and sensors to push state to it once again. It attempts to send
 * data to the remote host, and returns to waiting for data to be pushed to the
 *   injection buffer.
 */
class RemoteLogger {
 public:
  /**
   * @brief Class constructor.
   *
   * Attempt to deserialise any states that had been dumped to disk on previous
   * runs.
   * @param socket The socket we'll use to contact the host. We do this by
   * dependency injection so we can mock the socket in unit tests.
   */
  RemoteLogger(std::shared_ptr<ClientSocketInterface> socket)
      : socket_{socket} {
    // Check whether we have any serialised data on disk. If so, deserialise it
    // and stick it in the injection buffer, then remove the file.
    if (std::filesystem::exists(serialised_buffer_filename_)) {
      std::ifstream serialised(serialised_buffer_filename_);

      if (serialised.is_open() && serialised.good()) {
        spdlog::info("Reading serialised injection buffer from disk.");
        std::stringstream data;
        data << serialised.rdbuf();
        deserialise_buffer(data.str());
        if (std::remove(serialised_buffer_filename_.c_str()) != 0) {
          spdlog::error(
              "Couldn't remove serialised injection buffer from disk.");
        }
      } else {
        spdlog::error(
            "Recognised serialised injection buffer on disk, but couldn't "
            "open.");
      }
    }
  }

  /**
   * @brief Class destructor.
   *
   * Make sure we serialise the injection buffer, if there's anything in it,
   * and dump to disk.
   */
  ~RemoteLogger() {
    stop_injection_.store(true);
    spdlog::debug("Notifying injection thread to stop injection.");
    inject_cv_.notify_one();
    if (inject_thread_.joinable()) inject_thread_.join();

    // At this point, the injection thread has completed, so we can play with
    // the injection buffer without acquiring the protection mutex. Deserialise
    // anything still in the injection buffer to disk for next time
    if (!buffer_.empty()) {
      spdlog::info("Serialising injection buffer to disk.");
      auto serialised = serialise_buffer();
      std::ofstream serialised_data(serialised_buffer_filename_);
      if (serialised_data.is_open()) {
        serialised_data << serialised.str();
      } else {
        spdlog::error("Couldn't open file to dump buffer data to disk.");
      }
      serialised_data.close();
    }
  }

  /**
   * @brief Run method to start the injection of state to the remote host.
   *
   * Have factored this out here rather than have it in the constructor since it
   * makes unit testing the serialisation/ deserialisation of the injection
   * buffer a little easier (can make it so that the injection buffer never gets
   * flushed across the socket). Sure there's a cleverer way to accomplish this.
   */
  void run() {
    inject_thread_ = std::thread([this] { injection(); });
  }

  /**
   * @brief Log state from sensor. Package it in a HTTP POST and dump it into
   * the injection buffer.
   * @param state The sensor state.
   */
  void log_state(const std::string& state) {
    auto post_msg = create_post_message(state);

    spdlog::debug("Queueing datum to injection buffer.");
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.push_back(std::move(post_msg));
    inject_cv_.notify_one();
    spdlog::debug("Datum has been pushed to injection buffer.");
  }

 private:
  std::list<std::string> buffer_;
  std::mutex buffer_mutex_;
  std::thread inject_thread_;
  std::condition_variable inject_cv_;
  std::atomic<bool> stop_injection_ = false;
  std::shared_ptr<ClientSocketInterface> socket_;

  const std::string serialise_demarcation_ = "REMOTE_LOGGER_STATE_DEMARCATION";
  const std::string serialised_buffer_filename_ = "remote_logger_data.bin";

  /**
   * @brief Injection method that will continually take states from the
   * injection buffer and try to post them to the remote host.
   *
   * If a send ever fails (because of the socket buffer overflow), will stop
   * trying to take states from the injection buffer, and will instead try to
   * re-establish a connection with the remote host (presumably it hasn't been
   * contactable if the socket buffer has overflowed).
   */
  void injection() {
    while (!stop_injection_.load()) {
      std::unique_lock lock(buffer_mutex_);
      spdlog::debug("Injection thread awaits data.");
      inject_cv_.wait(
          lock, [this] { return !buffer_.empty() || stop_injection_.load(); });

      // Make sure we don't try to do any more injection buffer management if
      // we've been told to stop
      if (stop_injection_.load()) break;

      // Take all the data from the injection buffer. If the injection thread is
      // performing a send and a sensor sticks a state onto the injection buffer
      // while the injection thread isn't waiting at the condition variable
      // above, it'll miss being notified of a sensor state being ready. So once
      // the send is complete, it'll just wait at the condition variable till
      // the next notification by a sensor, at which point there are two sensor
      // states in the injection buffer
      auto data = std::move(buffer_);

      // Unlock here so sensors aren't waiting on the injection thread to send
      // data to the remote host or trying to re-establish a connection
      lock.unlock();

      spdlog::debug(
          "Injection thread recovers data from buffer with {} sensor state/s.",
          data.size());

      // Purge the entirety of the injection buffer
      while (!data.empty()) {
        auto datum = std::move(data.front());
        data.pop_front();

        // If we can't send the data for some unforeseen circumstance, just
        // default to assuming the connection is broken and attempt to
        // re-establish, then try to send again
        while (socket_->sendx(datum) == -1) {
          reestablish_connection();
        }
      }
      spdlog::debug("Injection thread has sent data to host.");
    }
    spdlog::info("Injection thread terminates gracefully.");
  }

  /**
   * @brief Wrap sensor state in a HTTP POST.
   * @param state The sensor state.
   * @return The wrapped sensor state in a HTTP POST.
   */
  std::string create_post_message(const std::string& state) {
    std::ostringstream request;
    request << "POST / HTTP/1.1\r\n";
    request << "Host: " << socket_->host() << "\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << state.length() << "\r\n";
    request << "\r\n";
    request << state;
    return request.str();
  }

  /**
   * @brief Attempt to re-establish a connection with the remote host. If
   * connection attempt fails, will back off for some period of time before
   * retrying so we don't blacklist ourselves.
   */
  void reestablish_connection() {
    spdlog::error("No connection to remote host. Attempting to reconnect.");
    bool connected = false;
    auto backoff = std::chrono::seconds(1);
    do {
      spdlog::info("Backing off for {} seconds until attempting connection.",
                   backoff.count());
      std::this_thread::sleep_for(backoff);
      backoff *= 2;
      connected = socket_->connectx() == 0 ? true : false;
    } while (!connected);
    spdlog::info("Connection to host has been re-established.");
  }

  /**
   * @brief Serialise the injection buffer into a stringstream. Serialising
   * involves taking each state from the injection buffer, concatenating them
   * and inserting a demarcation sequence between each. A demarcation sequence
   * shall be placed at the start and end of the stringstream too.
   * @return Serialised injection buffer.
   */
  std::stringstream serialise_buffer() {
    std::stringstream serialised;
    serialised << serialise_demarcation_;
    for (const auto& datum : buffer_) {
      serialised << datum;
      serialised << serialise_demarcation_;
    }
    return serialised;
  }

  /**
   * @brief Deserialise the injection buffer. Take the serialised injection
   * buffer and parse out the sensor states, dumping them into the injection
   * buffer.
   *
   * Note that we're assuming there's no contention for the injection buffer
   * here; the injection thread shouldn't have started when this is called, nor
   * should sensors be trying to dump state to the buffer.
   * @param serialised The serialised injection buffer.
   */
  void deserialise_buffer(const std::string& serialised) {
    std::size_t pos = serialised.find(serialise_demarcation_, 0);
    while (pos != std::string::npos) {
      std::size_t next_pos = serialised.find(serialise_demarcation_, pos + 1);
      pos += serialise_demarcation_.size();
      if (next_pos != std::string::npos) {
        std::string state = serialised.substr(pos, next_pos - pos);
        spdlog::debug("Found state: {}", state);
        buffer_.push_back(state);
      }
      pos = next_pos;
    }
  }
};

}  // namespace medtronic

#endif /* #ifndef __MEDTRONIC_TASK_REMOTE_LOGGER_HPP */
