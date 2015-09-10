#ifndef COMMUNICATION_HANDLER_HPP_
#define COMMUNICATION_HANDLER_HPP_

#include <vector>
#include <mutex>
#include <array>
#include <unistd.h>	// close()
#include <cstring>	// memset()
#include <arpa/inet.h>
#include <sys/types.h>	// recv(), send(), socket(), connect()
#include <sys/socket.h>	// recv(), send(), socket(), connect()

#include "abstract_shared.hpp"
#include "logger.hpp"

extern int pbsm_tid;

const int MAX_NUMBER_OF_NODES = 100;

/**
 * @brief Class for network communications
 *
 * This class opens a set of peer to peer UDP connections among the nodes.
 * Every pair of nodes have a pair of dedicated UDP connection for sending/receiving messages.
 * The class is implemented as a Singleton for a deterministic initialization order of objects.
 * Note that connections are created only when explicitly invoking method create_connections().
 */
class CommunicationHandler {
public:
	static CommunicationHandler& getInstance() {
		// Double-checked locking pattern for performance issues
		if (m_ == nullptr) {
			std::unique_lock<std::mutex> lock (mutex_);
			if (m_ == nullptr)
				m_ = new CommunicationHandler();
		}
		return *m_;
	}

	/**
	 * @brief Total number of hosts (included the current node)
	 *
	 * This variable is set by CommunicationHandler::CommunicationHandler()
	 * and made available to both the user application and the rest of the run-time.
	 */
	int get_number_of_nodes() const {
		return number_of_nodes_;
	}

	/**
	 * @brief Sends a message to all nodes excpect the node itself
	 * @param msg_data Buffer containing the raw data to be sent
	 * @param msg_size Size of data to be sent
	 * @return true in case of success; false in case of error
	 */
	bool send_to_all(void* msg_data, int msg_size) {
		bool ret = true;
		for (int i = 0; i < number_of_nodes_; ++i){
			if (i != pbsm_tid) {
				DEBUG("Sending to entry " << i << " related to " << connections_[i].ip << ":" << connections_[i].send_port << "...");
				lock_send_channel(i);
				if (send(connections_[i].send_fd, msg_data, msg_size, 0) != msg_size) {
					ERROR("ERROR: Sending data to " << connections_[i].ip << ":" << connections_[i].send_port);
					ret = false;
				}
				unlock_send_channel(i);
			}
		}
		return ret;
	}

	/**
	 * @brief Sends a message to all nodes excpect the node itself
	 * @param msg_data Buffer containing the raw data to be sent
	 * @param msg_size Size of data to be sent
	 * @return true in case of success; false in case of error
	 */
	bool send_two_messages_to_all(void* msg1_data, int msg1_size, void* msg2_data, int msg2_size) {
		bool ret = true;
		for (int i = 0; i < number_of_nodes_; ++i){
			if (i != pbsm_tid) {
				DEBUG("Sending to entry " << i << " related to " << connections_[i].ip << ":" << connections_[i].send_port << "...");
				lock_send_channel(i);
				if (send(connections_[i].send_fd, msg1_data, msg1_size, 0) != msg1_size) {
					ERROR("ERROR: Sending data to " << connections_[i].ip << ":" << connections_[i].send_port);
					ret = false;
				}
				if (send(connections_[i].send_fd, msg2_data, msg2_size, 0) != msg2_size) {
					ERROR("ERROR: Sending data to " << connections_[i].ip << ":" << connections_[i].send_port);
					ret = false;
				}
				unlock_send_channel(i);
			}
		}
		return ret;
	}


	bool send_two_messages_to(void* msg1_data, int msg1_size, void* msg2_data, int msg2_size, unsigned long int rem_node_id) {
		bool ret = true;
		if (rem_node_id == pbsm_tid) {
			ERROR("Trying to send data to myself");
			ret = false;
		} else {
			DEBUG("Sending to " << connections_[rem_node_id].ip << ":" << connections_[rem_node_id].send_port << "...");
			lock_send_channel(rem_node_id);
			DEBUG("Sending msg1 of size " << msg1_size << "...");
			if (send(connections_[rem_node_id].send_fd, msg1_data, msg1_size, 0) != msg1_size) {
				ERROR("ERROR: Sending msg1 data to " << connections_[rem_node_id].ip << ":" << connections_[rem_node_id].send_port);
				ret = false;
			}
			DEBUG("Sending msg2 of size " << msg2_size << "...");
			if (send(connections_[rem_node_id].send_fd, msg2_data, msg2_size, 0) != msg2_size) {
				ERROR("ERROR: Sending msg2 data to " << connections_[rem_node_id].ip << ":" << connections_[rem_node_id].send_port);
				ret = false;
			}
			unlock_send_channel(rem_node_id);
			DEBUG("Message sent!");
		}

		return ret;
	}

	/**
	 * @brief Sends a message to a specific node
	 * @param msg_data Buffer containing the raw data to be sent
	 * @param msg_size Size of data to be sent
	 * @param rem_node_id Id of the recipient node
	 * @return true in case of success; false in case of error
	 */
	bool send_to(void* msg_data, int msg_size, unsigned long int rem_node_id) {
		bool ret = true;
		if (rem_node_id == pbsm_tid) {
			ERROR("Trying to send data to myself");
			ret = false;
		} else {
			DEBUG("Sending to " << connections_[rem_node_id].ip << ":" << connections_[rem_node_id].send_port << "...");
			lock_send_channel(rem_node_id);
			if (send(connections_[rem_node_id].send_fd, msg_data, msg_size, 0) != msg_size) {
				ERROR("ERROR: Sending data to " << connections_[rem_node_id].ip << ":" << connections_[rem_node_id].send_port);
				ret = false;
			}
			unlock_send_channel(rem_node_id);
			DEBUG("Message sent!");
		}
		return ret;
	}

	/**
	 * @brief Receive data from a specific node
	 * @param msg_data Buffer where read data must be put
	 * @param msg_size Size of data read
	 * @param rem_node_id Id of the sender node
	 * @return true in case of success; false in case of error
	 */
	bool recv_from(void* msg_data, int msg_size, unsigned long int rem_node_id) {
		if (recv(connections_[rem_node_id].recv_fd, msg_data, msg_size, 0) != msg_size) {
			ERROR("Error in receiving data from " << connections_[rem_node_id].ip << ":" << connections_[rem_node_id].recv_port);
			return false;
		}
		return true;
	}

	void create_connections();

	void lock_send_channel (int node) {
		DEBUG("Locking channel for node " << node);
		if (node > number_of_nodes_) {
			ERROR("Lock channel on wrong entry");
		} else {
			connections_[node].send_channel_lock.lock();
			DEBUG("Channel for node " << node << " LOCKED");
		}
	}

	void unlock_send_channel (int node) {
		DEBUG("Unlocking channel for node " << node);
		if (node > number_of_nodes_) {
			ERROR("Unlock channel on wrong entry");
		} else {
			connections_[node].send_channel_lock.unlock();
			DEBUG("Channel for node " << node << " UNLOCKED");
		}
	}

private:

	/// Singleton pattern for a deterministic initialization order of objects
	static CommunicationHandler* m_;

	/// Mutex for object creation and destruction (i.e., modify m_)
	static std::mutex mutex_;

	CommunicationHandler();
	~CommunicationHandler();

	void start_send_client(int entry);
	void start_recv_server(int entry);

	struct connection {
		std::string ip;

		int recv_port;
		int recv_fd;

		int send_port;
		int send_fd;

		std::mutex send_channel_lock;

	};

	/// UDP connections for sending/receiving data from other peers
	std::array<connection, MAX_NUMBER_OF_NODES> connections_;

	/**
	 * @brief Offset for UDP port connections.
	 *
	 * Numbers lower than 1000 are usually reserved for Operating System services.
	 */
	const int network_port_offset = 2000;

	int number_of_nodes_;
};

#endif // COMMUNICATION_HANDLER_HPP_
