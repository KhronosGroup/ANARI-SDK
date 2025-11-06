// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Boost.ASIO needs _WIN32_WINNT
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501 // Require Windows XP or later
#endif
#endif

#include <system_error>
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <boost/signals2/connection.hpp>
#include <boost/signals2/signal.hpp>

#include "message.h"

namespace async {

class connection;
class connection_manager;

using connection_pointer = std::shared_ptr<connection>;

//--------------------------------------------------------------------------------------------------
// connection
//

class connection : public std::enable_shared_from_this<connection>
{
  friend class connection_manager;

 public:
  enum reason
  {
    Read,
    Write
  };

  using signal_type = boost::signals2::signal<void(
      reason r, message_pointer message, std::error_code const &e)>;

 public:
  // Constructor.
  connection(connection_manager &manager);

  // Destructor.
  ~connection();

  // Start reading from the socket
  void start();

  // Stop/Close the connection
  void stop();

  // Sets the handler for this connection
  // Thread-safe.
  void set_handler(signal_type::slot_function_type handler);

  // Removes the handler for this connection.
  // Thread-safe.
  void remove_handler();

  // Close the connection
  void close();

  // Sends a message to the other side.
  void write(message_pointer message);

  // Sends a message to the other side.
  template <typename It>
  void write(unsigned type, It first, It last)
  {
    write(make_message(type, first, last));
  }

  // Sends a message to the other side.
  template <typename Cont>
  void write(unsigned type, Cont const &cont)
  {
    write(make_message(type, std::begin(cont), std::end(cont)));
  }

 private:
  // The manager for this connection
  connection_manager &manager_;
  // The underlying socket.
  boost::asio::ip::tcp::socket socket_;
  // Signal (called from connection_manager if anything happens)
  signal_type signal_;
  // Slot
  boost::signals2::connection slot_;
};

} // namespace async
