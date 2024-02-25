/**
 * @brief Main medtronic task application. Have some number of sensors
 * concurrently dumping state to remote host.
 */
// C++ Standard Libraries
//
// Third-Party Libraries
#include "spdlog/spdlog.h"
// Project Inclusions
#include "medtronic_task/sensor.hpp"
#include "medtronic_task/remote_logger.hpp"
#include "medtronic_task/socket.hpp"

using namespace medtronic;

/**
 * @brief Medtronic task entry point.
 * @param argc Number of arguments.
 * @param argv Argument list.
 * @return 0 on success, something else otherwise.
 */
int main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::info);

  int num_sensors = 1;
  if (argc == 2) {
    num_sensors = atoi(argv[1]);
    if (num_sensors > 4 || num_sensors <= 0) {
      spdlog::error("Must choose 1 - 4 sensors.");
      return -1;
    }
  }
  spdlog::info("Running task with {} sensors.", num_sensors);

  auto socket =
      std::make_shared<ClientSocket>("en6msadu8lecg.x.pipedream.net", 80);
  RemoteLogger logger(socket);
  logger.run();

  std::vector<Sensor> sensors;
  for (int isensor = 0; isensor < num_sensors; ++isensor)
    sensors.push_back(Sensor());

  // Sensor loop to dump state indefinitely to remote host
  auto sensor_run = [&logger](Sensor& sensor) {
    while (true) {
      sensor.doWork();
      logger.log_state(sensor.getState());
    }
  };

  // Launch each sensor worker
  std::vector<std::thread> sensor_workers;
  for (int isensor = 0; isensor < sensors.size(); ++isensor) {
    auto sensor_worker = std::thread(
        [sensor_run, &sensors, isensor] { sensor_run(sensors[isensor]); });
    sensor_workers.push_back(std::move(sensor_worker));
  }

  for (auto& worker : sensor_workers) {
    if (worker.joinable()) worker.join();
  }

  return 0;
}
