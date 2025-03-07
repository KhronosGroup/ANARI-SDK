// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <deque>
#include <set>
#include <thread>
#include <vector>

#include "connection.h"

namespace async {

class connection;
class connection_manager;

using connection_manager_pointer = std::shared_ptr<connection_manager>;

//--------------------------------------------------------------------------------------------------
// connection_manager
//

class connection_manager
    : public std::enable_shared_from_this<connection_manager>
{
  friend class connection;

 public:
  using handler = std::function<bool(
      connection_pointer conn, boost::system::error_code const &e)>;

 public:
  connection_manager();
  explicit connection_manager(unsigned short port);

  ~connection_manager();

  // Starts the message loop
  void run();

  // Starts a new thread which in turn starts the message loop
  void run_in_thread();

  // Wait for the thread to finish
  void wait();

  // Stops the message loop
  void stop();

  // Starts a new accept operation.
  // Use bind_port() to specifiy the port.
  void accept(handler h);

  // Starts a new connect operation
  void connect(std::string const &host, unsigned short port, handler h);

  // Starts a new connect operation and waits until the connection is connected
  connection_pointer connect(std::string const &host, unsigned short port);

  // Returns an existing connection or creates a new one
  connection_pointer get_or_connect(
      std::string const &host, unsigned short port);

  // Close the given connection
  void close(connection_pointer conn);

  // Close all sockets
  void close_all();

  // Search for an existing connection
  connection_pointer find(std::string const &host, unsigned short port);

 private:
  // Start an accept operation
  void do_accept(handler h);

  // Handle completion of a accept operation.
  void handle_accept(
      boost::system::error_code const &e, connection_pointer conn, handler h);

  // Starts a new connect operation.
  void do_connect(std::string const &host, unsigned short port, handler h);

  // Handle completion of a connect operation.
  void handle_connect(
      boost::system::error_code const &e, connection_pointer conn, handler h);

  // Read the next message from the given client.
  void do_read(connection_pointer conn);

  // Called when a message header is read.
  void handle_read_header(boost::system::error_code const &e,
      message_pointer message,
      connection_pointer conn);

  // Called when a complete message is read.
  void handle_read_data(boost::system::error_code const &e,
      message_pointer message,
      connection_pointer conn);

  // Sends a message to the given client
  void write(message_pointer msg, connection_pointer conn);

  // Starts a new write operation.
  void do_write(message_pointer msg, connection_pointer conn);

  // Write the next message
  void do_write_0();

  // Called when a complete message is written.
  void handle_write(boost::system::error_code const &e,
      message_pointer message,
      connection_pointer conn);

  // Add a new connection
  void add_connection(connection_pointer conn);

  // Remove an existing connection
  void remove_connection(connection_pointer conn);

 private:
  using connections = std::set<connection_pointer>;
  using messages = std::deque<std::pair<connection_pointer, message_pointer>>;

  // The IO service
  boost::asio::io_service io_service_;
  // The acceptor object used to accept incoming socket connections.
  boost::asio::ip::tcp::acceptor acceptor_;
  // To protect the list of messages...
  boost::asio::io_service::strand strand_;
  // To keep the io_service running...
  std::shared_ptr<boost::asio::io_service::work> work_;
  // The list of active connections
  connections connections_;
  // List of messages to be written
  messages write_queue_;
  // A thread to process the message queue
  std::thread runner_;
};

inline connection_manager_pointer make_connection_manager()
{
  return std::make_shared<connection_manager>();
}

inline connection_manager_pointer make_connection_manager(unsigned short port)
{
  return std::make_shared<connection_manager>(port);
}

} // namespace async
