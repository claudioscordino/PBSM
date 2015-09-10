#include <stdexcept>
#include <fstream>
#include <iostream>
#include <thread>	// std::this_thread::get_id()

#include "communication_handler.hpp"
#include "logger.hpp"

// Initialization of static attributes:
CommunicationHandler* CommunicationHandler::m_ = nullptr;
std::mutex CommunicationHandler::mutex_;

/**
 * @brief Constructor
 *
 * This constructor is in charge of filling the connections_ data structure that contains
 * information about the peer-to-peer UDP connections to be open by create_connections().
 *
 * Information about nodes IP addresses is stored in file /etc/pbsm/hosts.conf.
 * This file contains a list of IP addresses. The file must be the same on all hosts.
 *
 * Note that object construction does not automatically open the connections.
 * These are open only when create_connections() is explicitly invoked.

 */
CommunicationHandler::CommunicationHandler(): number_of_nodes_(0)
{
	DEBUG("Creating CommunicationHandler...");

	std::ifstream config_file ("/etc/pbsm/hosts.conf", std::ios::in);
	if (!config_file.is_open())
	{
		ERROR("Can't open config file /etc/pbsm/hosts.conf");
		throw std::runtime_error ("Config file missing");
	} else {
		for (int i = 0; ; ++i) {
			config_file >> connections_[i].ip;
			if (config_file.eof())
				break;
			DEBUG("Node " << i << " is " << connections_[i].ip);
			connections_[i].recv_port = network_port_offset + i;
			connections_[i].send_port = network_port_offset + pbsm_tid;
			number_of_nodes_++;
			if (number_of_nodes_ == MAX_NUMBER_OF_NODES) {
				WARNING("Maximum number of nodes in config file reached");
				break;
			}
		}
	}

	// Data structure connections_ is now ready.
	// Connections will be open when create_connections() will be explicitly invoked.
}



CommunicationHandler::~CommunicationHandler()
{
	DEBUG("Destroying CommunicationHandler...");
	std::unique_lock<std::mutex> lock (mutex_);
	for (int i = 0; i < number_of_nodes_; ++i){
		close(connections_[i].recv_fd);
		close(connections_[i].send_fd);
	}
	delete m_;
	DEBUG("CommunicationHandler destroyed");
}

/**
 * @brief Methos to open UDP network connections.
 *
 * This method, explicitly invoked by pbsm_init(),
 * opens the UDP connections according to the information stored
 * by the constructor inside the connections_ data structure.
 *
 * It starts the receive connections (by calling start_recv_server)
 * and then starts the send connections (by calling start_send_client).
 *
 * All connections are started by the same thread.
 */
void CommunicationHandler::create_connections()
{
	DEBUG("CommunicationHandler starting...");
	DEBUG("My entry is " << pbsm_tid);
	DEBUG("Starting connections for receiving data...");

	for (int i = 0; i < number_of_nodes_; ++i){
		DEBUG("Checking entry " << i << "...");
		if (i != pbsm_tid)
			start_recv_server(i);
		else
			DEBUG("Entry " << i << " is me. Skipping.");
	}

	std::this_thread::sleep_for(std::chrono::seconds(5));

	DEBUG("Starting connections for sending data...");
	for (int i = 0; i < number_of_nodes_; ++i){
		if (i != pbsm_tid)
			start_send_client(i);
		else
			DEBUG("Entry " << i << " is me. Skipping.");
	}
}

/**
 * @brief Method to start send connections
 *
 * This is a private method called by CommunicationHandler::create_connections() to start send connections
 * @param entry Entry in the connections_ data structure related to the connection to be open.
 * @throw std::runtime_error in case of error
 */
void CommunicationHandler::start_send_client(int entry)
{
	DEBUG("Starting send client for entry " << entry);
	connection& s = connections_[entry];
	DEBUG("Opening client connection to " << s.ip << ":" << s.send_port);

	// socket()
	s.send_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (s.send_fd < 0) {
		ERROR("Send socket creation");
		throw std::runtime_error ("Send socket error");
	}

	// connect()
	 struct sockaddr_in serv_addr;
	 memset (&serv_addr, 0, sizeof(serv_addr));
	 serv_addr.sin_family = AF_INET;
	 serv_addr.sin_port = htons(s.send_port);
	 struct in_addr addr;
	 inet_aton(s.ip.c_str(), &addr);
	 memcpy(&addr, &serv_addr.sin_addr.s_addr, sizeof(addr));
	 if (connect(s.send_fd, (struct sockaddr *) &serv_addr,
					 sizeof(serv_addr)) < 0) {
		 close(s.send_fd);
		 ERROR("connect()");
		 throw std::runtime_error ("Client socket error");
	 }
}

/**
 * @brief Method to start receive connections
 *
 * This is a private method called by CommunicationHandler::create_connections() to start receive connections
 * @param entry Entry in the connections_ data structure related to the connection to be open.
 * @throw std::runtime_error in case of error
 */
void CommunicationHandler::start_recv_server(int entry)
{
	DEBUG("Starting receive server for entry " << entry);
	connection& s = connections_[entry];
	DEBUG("Opening server connection at " << s.ip << ":" << s.recv_port);


	// socket()
	s.recv_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (s.recv_fd < 0) {
		ERROR("Receive socket creation");
		throw std::runtime_error ("Receive socket error");
	}

	// bind()
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(s.recv_port);
	DEBUG("\t Port for server is " << s.recv_port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(s.recv_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		close(s.recv_fd);
		ERROR("Socket binding");
		throw std::runtime_error ("Bind error");
	}
}


