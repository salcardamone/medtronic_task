/**
 * @brief Sensor implementation; this was provided, mostly, by Medtronic.
 */
#ifndef __MEDTRONIC_TASK_SENSOR_HPP
#define __MEDTRONIC_TASK_SENSOR_HPP

// C++ Standard Libraries
#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
// Third-Party Libraries
#include "spdlog/spdlog.h"
// Project Inclusions
//

namespace medtronic {

/**
 * @brief Sensor class.
 */
class Sensor {
 public:
  /**
   * @brief Class constructor. Generate a random 32-character long UUID for the
   * sensor.
   */
  Sensor() : sensor_id(generateUUID()) {
    spdlog::info("Created sensor: {}", sensor_id);
  }

  /**
   * @brief Dummy work function. Timeout for some random period of time between
   * 100ms and 1500ms.
   */
  void doWork() {
    std::this_thread::sleep_for(
        std::chrono::milliseconds((int)(100 + (std::rand() % 1400))));
  }

  /**
   * @brief Get the current system time and return it.
   * @return The stringified current timestamp, in YYYY-mm-dd HH:MM:SS format.
   */
  std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timestamp;
    timestamp << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    return timestamp.str();
  }

  /**
   * @brief Serialise the current system state as a JSON string.
   * @return Stringified JSON representing current sensor state.
   */
  std::string getState() {
    // clang-format off
    return
      "{\n"
        "  \"id\": \"" + sensor_id + "\",\n"
        "  \"event\": {\n"
          "    \"type\": \"" + generateEventType() + "\",\n"
          "    \"readings\": [" +
            std::to_string(std::rand() % 101) + ", " +
            std::to_string(std::rand() % 101) + ", " +
            std::to_string(std::rand() % 101) +
          "]\n"
        "  },\n"
        "  \"timestamp\": \"" + getCurrentTimestamp() + "\"\n"
      "}";
    // clang-format on
  }

 private:
  std::string sensor_id;

  /**
   * @brief Generate a random 32-character UUID. Note that this is only ever
   * called on construction.
   * @return Random 32-character UUID (hex-characters only).
   */
  std::string generateUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char hex_chars[] = "0123456789ABCDEF";

    std::ostringstream uuid;
    for (int i = 0; i < 32; ++i) {
      uuid << hex_chars[dis(gen)];
    }

    return uuid.str();
  }

  /**
   * @brief Generate some random event.
   * @return The random event type.
   */
  std::string generateEventType() {
    std::vector<std::string> event_types = {"nominal", "info", "warning",
                                            "error", "critical"};

    std::random_device rd;
    std::mt19937 gen(rd());
    std::discrete_distribution<> dis({60, 24, 10, 5, 1});

    return event_types[dis(gen)];
  }
};

}  // namespace medtronic

#endif /* #ifndef __MEDTRONIC_TASK_SENSOR_HPP */
