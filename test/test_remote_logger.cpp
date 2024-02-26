/**
 * @brief Testing of the RemoteLogger class. Mix of unit and integration tests.
 */
// C++ Standard Libraries
#include <memory>
#include <thread>
#include <fstream>
// Third-Party Libraries
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "spdlog/spdlog.h"
// Project Inclusions
#include "mock_socket.hpp"
#include "medtronic_task/remote_logger.hpp"
#include "medtronic_task/sensor.hpp"

using namespace medtronic;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

static const std::string hostname("testhost");

/**
 * @brief Test fixture for remote logger tests; let's us create serialised
 * state on disk within the test, rather than having to manage and version
 * control text files.
 */
class RemoteLoggerTestFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    std::ofstream serialised_data(filename);
    // Note the addition of the "For Removal"; we want to make sure anything not
    // sandwiched between demarcation lines is ignored
    if (serialised_data.is_open()) {
      serialised_data << "For Removal";
      serialised_data << dummy_serialised_buffer;
      serialised_data << "For Removal";
      serialised_data.close();
    } else {
      spdlog::error("Couldn't open file: {}", filename);
    }
  }

  void TearDown() override {
    if (remove(filename.c_str()) != 0) {
      spdlog::error("Couldn't remove file: {}", filename);
    }
  }

  const std::string filename = "remote_logger_data.bin";
  const std::string dummy_serialised_buffer =
      "REMOTE_LOGGER_STATE_DEMARCATION"
      "Hello, world!"
      "REMOTE_LOGGER_STATE_DEMARCATION"
      "Goodbye, world!"
      "REMOTE_LOGGER_STATE_DEMARCATION";
};

// ========================================================================== //

/**
 * @brief Mimic socket buffer overflow. In that case, send() will fail and
 * initiate a reconnection attempt. Fail to reconnect twice (allows us to check
 * the back-off) then succeed. Data that wasn't sent should then be sent.
 */
TEST(RemoteLoggerTest, DroppedConnection) {
  spdlog::set_level(spdlog::level::debug);
  auto socket = std::make_shared<MockClientSocket>();

  EXPECT_CALL(*socket, connectx())
      .Times(3)
      .WillOnce(Return(-1))
      .WillOnce(Return(-1))
      .WillOnce(Return(0));

  EXPECT_CALL(*socket, sendx(_))
      .Times(2)
      .WillOnce(Return(-1))
      .WillOnce(Return(0));

  EXPECT_CALL(*socket, host()).WillRepeatedly(ReturnRef(hostname));

  Sensor sensor;
  RemoteLogger logger(socket);
  logger.run();

  sensor.doWork();
  logger.log_state(sensor.getState());

  // This is a bit of a bodge. The issue is that the ~RemoteLogger is going
  // to be called straight after RemoteLogger::log_state, setting
  // stop_injection.
  //
  // RemoteLogger::log_state pushes state to the injection buffer which the
  // injection thread is monitoring (it sleeps until the buffer isn't empty).
  // At that point, the injection thread checks whether stop_injection == true,
  // and ends if true.
  //
  // If the logger destructor is called before the injection thread wakes up
  // after satisfying the condition variable, then the injection thread
  // terminates before it can send anything, or try to re-establish a connection
  // to the remote host as we're trying to check in this test.

  // So, we'll just artificially sleep to allow the injection thread to progress
  // before the destructor forces the injection thread to terminate prematurely
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

// ========================================================================== //

/**
 * @brief Make sure we can have multiple sensors concurrently dumping state
 * across the socket.
 *
 * We don't really have anything in particular to check or assert here
 * (technically guess this isn't a test then...), just want to make sure this
 * all works roughly as expected. Turning spdlog level to debug shows events
 * happen as expected.
 */
TEST(RemoteLoggerTest, MultipleSensors) {
  spdlog::set_level(spdlog::level::debug);
  auto socket = std::make_shared<MockClientSocket>();

  EXPECT_CALL(*socket, sendx(_)).Times(12).WillRepeatedly(Return(0));
  EXPECT_CALL(*socket, host()).WillRepeatedly(ReturnRef(hostname));

  std::vector<Sensor> sensors{Sensor(), Sensor(), Sensor()};
  RemoteLogger logger(socket);
  logger.run();

  // Each sensor will dump state four times to the mocked socket.
  auto sensor_run = [&logger](Sensor& sensor) {
    for (std::size_t istate = 0; istate < 4; ++istate) {
      sensor.doWork();
      logger.log_state(sensor.getState());
    }
  };

  std::vector<std::thread> sensor_workers;
  for (std::size_t isensor = 0; isensor < sensors.size(); ++isensor) {
    auto sensor_worker = std::thread(
        [sensor_run, &sensors, isensor] { sensor_run(sensors[isensor]); });
    sensor_workers.push_back(std::move(sensor_worker));
  }

  for (auto& worker : sensor_workers) {
    if (worker.joinable()) worker.join();
  }
}

// ========================================================================== //

/**
 * @brief Check that we can deserialise states from disk to the state injection
 * buffer and then serialise them back to disk.
 */
TEST_F(RemoteLoggerTestFixture, SerialiseStates) {
  spdlog::set_level(spdlog::level::debug);

  auto socket = std::make_shared<MockClientSocket>();

  EXPECT_CALL(*socket, host()).WillRepeatedly(ReturnRef(hostname));

  // Force the RemoteLogger destructor to dump injection buffer to disk
  {
    Sensor sensor;
    RemoteLogger logger(socket);
  }

  std::ifstream serialised(filename);
  std::stringstream data;
  data << serialised.rdbuf();
  // Should have removed those "For Removal" strings since they weren't
  // sandwiched with demarcator strings
  EXPECT_EQ(data.str(), dummy_serialised_buffer);
}

// ========================================================================== //
