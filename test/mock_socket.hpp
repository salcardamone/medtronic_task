/**
 * @brief
 */
#ifndef __MEDTRONIC_TASK_TEST_MOCK_SOCKET_HPP
#define __MEDTRONIC_TASK_TEST_MOCK_SOCKET_HPP

// C++ Standard Libraries
//
// Third-Party Libraries
#include <gtest/gtest.h>
#include <gmock/gmock.h>
// Project Inclusions
#include "medtronic_task/socket.hpp"

namespace medtronic {

class MockClientSocket : public ClientSocketInterface {
 public:
  MOCK_METHOD(int, connectx, (), ());
  MOCK_METHOD(int, sendx, (const std::string& data), ());
  MOCK_METHOD(const std::string&, host, (), (const));
};

}  // namespace medtronic

#endif /* #ifndef __MEDTRONIC_TASK_TEST_MOCK_SOCKET_HPP */
