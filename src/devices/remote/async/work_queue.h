// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/bind/bind.hpp>
#include <thread>

namespace async {

class work_queue
{
  // The io_service object
  boost::asio::io_context io_context_;
  // Prevents the io_context from exiting if it has nothing to do
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
  // Provides serialised handler execution
  boost::asio::io_context::strand strand_;
  // The worker thread
  std::thread thread_;

 public:
  // Constructor.
  work_queue()
      : io_context_(),
        work_(boost::asio::make_work_guard(io_context_)),
        strand_(io_context_),
        thread_()
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
    io_context_.run();
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
    io_context_.stop();
    // Wait for the worker thread
    thread_.join();
  }

  // Schedule some work to be executed
  template <class Function>
  void post(Function func)
  {
    boost::asio::post(io_context_, func);
  }
};

} // namespace async
