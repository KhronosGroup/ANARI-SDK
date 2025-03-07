// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/bind/bind.hpp>
#include <thread>

namespace async {

class work_queue
{
  // The io_service object
  boost::asio::io_service io_service_;
  // Prevents the io_service from exiting if it has nothing to do
  boost::asio::io_service::work work_;
  // Provides serialised handler execution
  boost::asio::io_service::strand strand_;
  // The worker thread
  std::thread thread_;

 public:
  // Constructor.
  work_queue()
      : io_service_(), work_(io_service_), strand_(io_service_), thread_()
  {}

  // Destructor.
  // Stops the worker thread.
  ~work_queue()
  {
    stop();
  }

  // Start working
  void run()
  {
    io_service_.run();
  }

  // Start working
  void run_in_thread()
  {
    thread_ = std::thread(boost::bind(&work_queue::run, this));
  }

  // Stop working and wait for the thread to finish
  void stop()
  {
    // Stop the io_service
    io_service_.stop();
    // Wait for the worker thread
    thread_.join();
  }

  // Schedule some work to be executed
  template <class Function>
  void post(Function func)
  {
    strand_.post(func);
  }
};

} // namespace async
