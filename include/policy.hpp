#ifndef POLICY_HPP
#define POLICY_HPP

#include <vector>
#include <thread>	// std::this_thread::get_id()
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <map>

#include "communication_handler.hpp"
#include "abstract_shared.hpp"
#include "messages.hpp"

/**
 * @brief Policy for data synchronization among nodes.
 *
 * This class implements the policy for keeping synchronization of data across different nodes.
 * It is closely tied to messages in messages.hpp file.
 * It uses CommunicationHandler for sending/receiving messages between the nodes.
 */
class Policy {
public:
	static Policy& getInstance() {
		// Double-checked locking pattern for performance issues
		if (m_ == nullptr) {
			std::unique_lock<std::mutex> lock (mutex_);
			if (m_ == nullptr)
				m_ = new Policy();
		}
		return *m_;
	}

	// This was called disable_ ownership()
	/**
	 * @brief Method for disabling ownerhsips of all existing variables.
	 *
	 * This method is called at start-up on slave nodes.
	 */
	void slave_node_init(){
		std::unique_lock<std::mutex> lock (mutex_);
		for (auto& i: dictionary_){
			// At beginning the master node becomes owner of all variables:
			std::unique_lock<std::mutex> data_lock (i.second->policy_data_.mutex_);
			i.second->policy_data_.state_ = state::REMOTE_OWNER_CACHED;
			i.second->policy_data_.remote_owner_ = 0;
		}
	}

	/**
	 * @brief Method for enabling ownerhsips of all existing variables.
	 *
	 * This method is called at start-up on the master node.
	 */
	void master_node_init(){
		std::unique_lock<std::mutex> lock (mutex_);
		for (auto& i: dictionary_){
			// At beginning the master node becomes owner of all variables:
			std::unique_lock<std::mutex> data_lock (i.second->policy_data_.mutex_);
			i.second->policy_data_.state_ = state::OWNER_SHARED;
		}
	}


	// This was called refresh_value()
	/**
	 * @brief Method to refresh the value of a variable that is going to be read.
	 *
	 * The value is refreshed only the first time.
	 * Then, the variable enters the "cached" state, that it can exit only with a write
	 * operation (either local or remote).
	 *
	 * This method is called when a node wants to read a variable not owned.
	 * @param var_id	Id of the written variable
	 */
	void before_local_read(uint32_t var_id) {
		DEBUG("Attempt local read");
		var_data* v = dictionary_[var_id];
		if (v != nullptr){
			std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
			if (v->policy_data_.state_ == state::REMOTE_OWNER_NO_CACHED) {
				DEBUG("No owner and no cached: need to request new value");
				requestCurrentValue(v);
				DEBUG("BLOCKING on wait_value_updated_");
				v->policy_data_.wait_value_updated_.wait(lock);
				v->policy_data_.state_ = state::REMOTE_OWNER_CACHED;
			}
		}
	}

	// This was called acquire_ownership()
	/**
	 * @brief Method to acquire ownership of a variable that is going to be locally written.
	 *
	 * This method is called when a node wants to write a local variable.
	 * @param var_id	Id of the written variable
	 * @return		false in case of network error or variable unknown
	 */
	bool before_local_write(uint32_t var_id) {
		bool ret = true;
		DEBUG("Checking variable ownership...");
		struct var_data* v = dictionary_[var_id];
		if (v != nullptr){
			std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
			if ((v->policy_data_.state_ == state::REMOTE_OWNER_NO_CACHED) ||
			    (v->policy_data_.state_ == state::REMOTE_OWNER_CACHED)) {
				// We're not owners: request ownership and wait grant
				DEBUG("We're not owners. Sending request to owner");
				send_request_ownership(v);
				DEBUG("BLOCKING on waiting_ownership_grant_...");
				v->policy_data_.waiting_ownership_grant_.wait(lock);
				DEBUG("Waked up from waiting_ownership_grant_. Changing ownership.");
				v->policy_data_.state_ = state::OWNER_NO_SHARED;
			} else if (v->policy_data_.state_ == state::OWNER_SHARED) {
				// We need to invalidate all nodes' copies
				DEBUG("Sending MSG__INVALIDATE_COPY...");
				msg_t ans;
				ans.type = msg_type_t::MSG_INVALIDATE_COPY;
				ans.data.node = pbsm_tid;
				ans.id = var_id;
				if (!CommunicationHandler::getInstance().send_to_all(&ans, sizeof(ans))) {
					ERROR("ERROR in sending MSG_INVALIDATE_COPY");
					ret = false;
				}
				v->policy_data_.waiting_invalidate_copies_.counter_ = CommunicationHandler::getInstance().get_number_of_nodes() -1;
				DEBUG("BLOCKING on waiting_invalidate_copies_");
				v->policy_data_.waiting_invalidate_copies_.wait_condition_.wait(lock);
				v->policy_data_.state_ = state::OWNER_NO_SHARED;
			}
		} else {
			ret = false;
		}
		return ret;
	}

	/**
	 * @brief Method invoked after a local write happened.
	 *
	 * This method is empty, since the current Policy does not do any operation after writing a local variable.
	 * A possibly different approach is to inform all nodes about the new value.
	 * @param var_id	Id of the written variable
	 */
	void after_local_write(uint32_t  var_id) {}

	// This was called wake_up_waiting_update()
	/**
	 * @brief Method invoked after the value of a not-owned variable has been refreshed.
	 *
	 * This method is called when the node attempted to read a non-cached variable.
	 * @param var_id	Id of the updated variable
	 */
	void after_remote_write(uint32_t var_id) {
		DEBUG("Waking up blocked nodes...");
		var_data* v = dictionary_[var_id];
		if (v != nullptr){
			DEBUG("UNBLOCKING wait_value_updated_");
			v->policy_data_.wait_value_updated_.notify_all();
		} else {
			WARNING ("Not unblocking wait_value_updated_");
		}
	}

	/**
	 * @brief Method invoked when a new (either global, stack or heap) variable is created.
	 *
	 * This method sets ownership depending on the master/slave node status.
	 * Moreover, it adds the new variable to the internal dictionary.
	 * @param data		Pointer to the AbstractShared structure of the newly created variable.
	 */
	void at_variable_creation(AbstractShared* data) {
		DEBUG("Policy informed of new variable " << data->get_id() << " created");
		var_data* v = new var_data;
		v->variable_ = data;

		// This works also when pbsm_tid has not yet been set (because it is initialized to -1)
		if (pbsm_tid == 0) {
			// Master node
			// Shared because other nodes may create their own copies
			DEBUG("We're master. Setting ownership to us");
			v->policy_data_.state_ = state::OWNER_SHARED;
		} else {
			// Slave node
			DEBUG("We're slave. Setting ownership to master");
			v->policy_data_.state_ = state::REMOTE_OWNER_CACHED;
			// Master node is owner
			v->policy_data_.remote_owner_= 0;
		}
		dictionary_[data->get_id()] = v;
	}

	/**
	 * @brief Method invoked when a (either global, stack or heap) variable is destroyed.
	 *
	 * This method sends a message to all nodes with the latest value of the variable.
	 * Moreover, it removes the variable from the internal dictionary.
	 *
	 * Note: this method is called by the variable destructor. Therefore, we can only rely on
	 * the function parameters, and we cannot access dictionary_[var_id].second->variable_
	 * @param var_id	ID of the variable that is going to be destroyed
	 * @param data		Pointer to the buffer containing the value
	 * @param size		Size of the buffer
	 * @return		true in case of success (also of network communication); false otherwise
	 */
	bool at_variable_destruction(uint32_t var_id, void* data, int size) {
		bool ret = true;
		DEBUG("Sending new value of variable " << var_id << " to all");

		var_data* v = dictionary_[var_id];
		if (v == nullptr){
			ERROR ("Variable not found!");
			ret = false;
		} else {
			std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);

			msg_t ans;
			ans.type = msg_type_t::MSG_SET_NEW_VALUE;
			ans.id = var_id;
			ans.data.var_size = size;

			ret = CommunicationHandler::getInstance().send_two_messages_to_all(&ans, sizeof(ans), data, size);

			delete v;
			dictionary_[var_id] = nullptr;
		}
		return ret;
	}




	/**
	 * @brief Method invoked when reaching a barrier
	 *
	 * @param s	ID of the barrier
	 */
	void thread_wait_barrier(uint32_t s) {
		DEBUG("Barrier " << s << " locally reached");
		if (pbsm_tid == 0)
			thread_wait_master_barrier(s);
		else
			thread_wait_slave_barrier(s);
	}

	/**
	 * @brief Method invoked to start receiving from receiving sockets.
	 *
	 * This method starts a set of new threads, each one listening from a specific UDP channel
	 * (connected to a specific node).
	 * It can be called only when CommunicationHandler::getInstance().create_connections() and pbsm_tid have been set.
	 */
	void start_receiving() {
		DEBUG("Policy constructor starting...");
		DEBUG("TID of Policy constructor is " << std::this_thread::get_id());
		int hosts_nb = CommunicationHandler::getInstance().get_number_of_nodes();

		for (int i = 0; i < hosts_nb; ++i){
			DEBUG("Checking node " << i << "...");
			if (i != pbsm_tid) {
				DEBUG("Node " << i << " is not me. Starting new thread for node " << i << "...");
				std::thread* t = new std::thread ([=] {this->receive_messages(i);});
				t->detach();
				threads_.push_back(t);
			}
		}
	}


private:
	/**
	 * @brief Possible states of a shared variable.
	 */
	enum class state {
		OWNER_NO_SHARED = 1,		//< We are owner of data; value not (yet) shared.
		OWNER_SHARED = 2,		//< We are owner of data; value already shared.
		REMOTE_OWNER_CACHED = 3,	//< We are not owner of data; we have a valid cached value
		REMOTE_OWNER_NO_CACHED = 4,	//< We are not owner of data; we have not a valid cached value.
	};

	/**
	 * @brief Semaphore for blocking waiting a certain amount of events.
	 *
	 * Implemented since C++11 does not provide semaphores.
	 */
	struct semaphore {
		std::atomic_ulong counter_;		//< Counter. When reaches 0 wait_condition_ is notified.
		std::condition_variable wait_condition_;
	};

	/**
	 * @brief Policy data associated to a shared variable.
	 */
	struct var_data {
		/// Pointer to the actual shared<> variable
		AbstractShared* variable_;

		struct {
			/// State of the shared<> variable
			Policy::state state_;

			/// Condition variable to wait when waiting for value refresh
			std::condition_variable wait_value_updated_;

			/// Remote owner (meaningful only if this node isn't the owner)
			unsigned long int remote_owner_;

			/// Lock for mutual exclusion to access data
			std::mutex mutex_;

			/// Condition variable to wait variable refresh when reading and value is not cached
			std::condition_variable waiting_ownership_grant_;

			/// Semaphore to wait all nodes to invalidate their own copies
			semaphore waiting_invalidate_copies_;
		} policy_data_;
	};

	/**
	 * @brief Method invoked to request the current value of a variable to a remote node.
	 *
	 * Must be called with lock already acquired
	 *
	 * @param var_id	ID of the variable whose value is requested
	 * @return		true in case of success (also of network communication); false otherwise
	 */
	bool requestCurrentValue(var_data* v) {
		bool ret = true;

		DEBUG("Requesting current value of variable " << v->variable_->get_id());
		DEBUG("Sending MSG_ASK_CURRENT_VALUE...");
		unsigned long int owner = v->policy_data_.remote_owner_;
		msg_t msg;
		msg.type = msg_type_t::MSG_ASK_CURRENT_VALUE;
		msg.data.node = pbsm_tid;
		msg.id = v->variable_->get_id();

		if (!CommunicationHandler::getInstance().send_to(&msg, sizeof(msg), owner)) {
			ERROR("ERROR in sending MSG_ASK_CURRENT_VALUE");
			ret = false;
		}

		return ret;
	}

	/**
	 * @brief Method to request ownership of a variable to the current owner
	 *
	 * This method sends a message on the network to the current owner to get ownership.
	 * @param var	Pointer to var_data of the variable
	 * @return	true in case of success; false in case of network error
	 */
	bool send_request_ownership(struct var_data* var) {
		bool ret = true;
		DEBUG("Sending MSG_REQUEST_OWNERSHIP message...");

		msg_t msg;
		msg.type = msg_type_t::MSG_REQUEST_OWNERSHIP;
		msg.data.node = pbsm_tid;
		msg.id = var->variable_->get_id();
		if (!CommunicationHandler::getInstance().send_to_all(&msg, sizeof(msg))) {
				ERROR("ERROR in sending MSG_REQUEST_OWNERSHIP");
				ret = false;
		}

		return ret;
	}

	void receive_messages(int rem_node);
	void thread_wait_master_barrier(uint32_t s);
	void thread_wait_slave_barrier(uint32_t s);

	/**
	 * @brief Method to change the owner of a owned variable.
	 *
	 * This method is called when receiving MSG_REQUEST_OWNERSHIP or MSG_SET_NEW_OWNER:
	 * <ul>
	 * <li> In case of MSG_SET_NEW_OWNER (we requested the ownership to an old owner),
	 * a message MSG_REQUEST_OWNERSHIP will be re-issued after this method
	 * (by calling send_request_ownership()).
	 * <li> In case of MSG_REQUEST_OWNERSHIP a message MSG_GRANT_OWNERSHIP message will
	 * be sent after this method.
	 * </ul>
	 * @param var		Pointer to written variable data
	 * @param rem_node_id	New owner
	 * @return		True in case this node was the owner; false otherwise
	 */
	bool change_owner(var_data* var, unsigned long int node) {
		DEBUG("Changing owner of the variable...");
		bool ret = false;
		std::unique_lock<std::mutex> lock (var->policy_data_.mutex_);
		if ((var->policy_data_.state_ == state::OWNER_NO_SHARED) ||
		    (var->policy_data_.state_ == state::OWNER_SHARED))
			ret = true;
		var->policy_data_.state_ = state::REMOTE_OWNER_NO_CACHED;
		var->policy_data_.remote_owner_= node;
		return ret;
	}

	/// Singleton pattern for a deterministic initialization order of objects
	static Policy* m_;
	static std::mutex mutex_;

	Policy(){}

	/// Destructor: just clean up data (i.e., threads and dictionary)
	~Policy(){
		for (auto i: threads_)
			delete i;
		threads_.clear();
		for (auto i: dictionary_)
			delete i.second;
		dictionary_.clear();

	}

	/**
	 * @brief Main data structure of to map variable IDs to var_data structures.
	 */
	std::map<uint32_t, var_data*> dictionary_;

	/**
	 * @brief Data structure for barriers on the master node
	 *
	 * This data structure maps barriers IDs to semaphores with a counter.
	 * It is used only on the master node, which is responsible of waiting all other nodes.
	 */
	std::map<uint32_t, semaphore*> master_waiting_barrier_grants_;

	/**
	 * @brief Condition variable for barrier on the slave node
	 *
	 * The slave waits on this condition variable a MSG_BARRIER_UNBLOCK message
	 * from the master node.
	 */
	std::condition_variable slave_wait_barrier_;

	/**
	 * @brief Threads for asynchronous message receiving
	 *
	 * This data structure contains the IDs of the threads launched to receive messages
	 * on a specific channel connected to a specific node.
	 */
	std::vector<std::thread*> threads_;
};


#endif // POLICY_HPP

