#include "policy.hpp"

Policy* Policy::m_ = nullptr;
std::mutex Policy::mutex_;

/**
 * @brief Method for receiving messages on a specific UDP channel connected to a specific node.
 *
 * We have several concurrent executions of this method, executed on different threads.
 * @param node_id	ID of the remote node
 */

void Policy::receive_messages(int rem_node)
{
	DEBUG("TID of thread for receiving data for rem_node " << rem_node << " is " << std::this_thread::get_id());
	for(;;) {
		msg_t msg;
		DEBUG("Receiving new message...");
		if (!CommunicationHandler::getInstance().recv_from(&msg, sizeof(msg), rem_node)){
			ERROR("Error in receiving message");
		} else {
			DEBUG(sizeof(msg) << " bytes received.");
		}

		switch (msg.type) {
		case (msg_type_t::MSG_REQUEST_OWNERSHIP): {
			DEBUG("Received new message of type MSG_REQUEST_OWNERSHIP");

			// Check if we're still owners of the variable
			bool ret = false;
			var_data* v = dictionary_[msg.id];
			if (v != nullptr){
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				//DEBUG("Current state of variable is " << v->policy_data_.state_ );
				if ((v->policy_data_.state_ == state::OWNER_NO_SHARED) ||
				    (v->policy_data_.state_ == state::OWNER_SHARED)) {

					// We are owners: disable ownership and grant ownership.
					DEBUG("We are still owners of the variable. Change owner.");
					v->policy_data_.state_ = state::REMOTE_OWNER_NO_CACHED;
					v->policy_data_.remote_owner_= msg.data.node;

					DEBUG("Sending MSG_GRANT_OWNERSHIP...");
					msg_t ans;
					ans.type = msg_type_t::MSG_GRANT_OWNERSHIP;
					ans.data.node = pbsm_tid;
					ans.id = msg.id;
					if (!CommunicationHandler::getInstance().send_to(&ans, sizeof(ans), msg.data.node))
						ERROR("ERROR in sending grant message to " << msg.data.node);
				} else {
					// We are not owners: send the new owner to the requesting node.
					DEBUG("We are not owners anymore. Sending MSG_SET_NEW_OWNER to the requesting node...");

					DEBUG("Sending MSG_SET_NEW_OWNER...");
					msg_t ans;
					ans.type = msg_type_t::MSG_SET_NEW_OWNER;
					ans.data.node = v->policy_data_.remote_owner_;
					ans.id = msg.id;

					if (!CommunicationHandler::getInstance().send_to(&ans, sizeof(ans), msg.data.node))
						ERROR("ERROR in sending MSG_SET_NEW_OWNER message to " << msg.data.node);
				}
			}
			break;
		}
		case (msg_type_t::MSG_GRANT_OWNERSHIP): {
			DEBUG("Received MSG_GRANT_OWNERSHIP");

			var_data* v = dictionary_[msg.id];
			if (v == nullptr){
				ERROR("Received MSG_GRANT_OWNERSHIP but no ownership was requested");
			} else {
				DEBUG("Waking up sleeping thread");
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				DEBUG("UNBLOCKING waiting_ownership_grant_");
				v->policy_data_.waiting_ownership_grant_.notify_all();
			}

			break;
		}
		case (msg_type_t::MSG_ASK_CURRENT_VALUE): {
			DEBUG("Received MSG_ASK_CURRENT_VALUE");

			var_data* v = dictionary_[msg.id];
			if (v != nullptr){
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				if ((v->policy_data_.state_ == state::REMOTE_OWNER_CACHED) ||
				    (v->policy_data_.state_ == state::REMOTE_OWNER_NO_CACHED)) {
					DEBUG("We are not owners anymore. Sending MSG_SET_NEW_OWNER to the requesting node...");

					DEBUG("Sending MSG_SET_NEW_OWNER...");
					msg_t ans;
					ans.type = msg_type_t::MSG_SET_NEW_OWNER;
					ans.data.node = v->policy_data_.remote_owner_;
					ans.id = msg.id;

					if (!CommunicationHandler::getInstance().send_to(&ans, sizeof(ans), msg.data.node))
						ERROR("ERROR in sending MSG_SET_NEW_OWNER message to " << msg.data.node);

				} else {
					DEBUG("Setting cached status to variable " << msg.id);
					v->policy_data_.state_ = state::OWNER_SHARED;

					msg_t ans;
					ans.type = msg_type_t::MSG_SET_NEW_VALUE;
					ans.id = msg.id;

					DEBUG("Sending MSG_SET_NEW_VALUE...");
					ans.data.var_size = v->variable_->get_size();
					char* data = new char [ans.data.var_size];
					v->variable_->get_value(data);

					CommunicationHandler::getInstance().send_two_messages_to(&ans, sizeof(ans), data, ans.data.var_size, msg.data.node);

					delete[] data;

				}
			} else {
				ERROR("Variable not found");
			}
			break;
		}
		case (msg_type_t::MSG_SET_NEW_VALUE): {
			DEBUG("Received MSG_SET_NEW_VALUE");

			DEBUG("Need to receive data with length " << msg.data.var_size);
			char* d = new char [msg.data.var_size];
			if (!CommunicationHandler::getInstance().recv_from(d, msg.data.var_size, rem_node)){
				ERROR("Error in receiving data of MSG_SET_NEW_VALUE");
			} else {
				DEBUG(msg.data.var_size << " bytes received.");
			}
			DEBUG("Data succesfully received");

			var_data* v = dictionary_[msg.id];
			if (v == nullptr){
				ERROR("Variable " << msg.id << " not found");
			} else {
				DEBUG("Variable " << msg.id <<" found. Changing its value");
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				v->variable_->set_value((void*) d);
				after_remote_write(msg.id);
				DEBUG("New value succesfully set");
			}
			delete[] d;
			break;
		}
		case (msg_type_t::MSG_BARRIER_BLOCK): {
			DEBUG("Received MSG_BARRIER_BLOCK");
			if (pbsm_tid != 0)
				ERROR("Received message MSG_BARRIER_BLOCK but I'm not the master");

			// We can't block in this function, otherwise we go in deadlock!
			std::unique_lock<std::mutex> lock (mutex_);
			semaphore* elem = master_waiting_barrier_grants_[msg.id];
			if (elem == nullptr) {
				DEBUG("Remote note is the first node to reach the barrier. Creating data structures...");
				elem = new semaphore;
				elem->counter_ = CommunicationHandler::getInstance().get_number_of_nodes();
				master_waiting_barrier_grants_[msg.id] = elem;
			}
			elem->counter_--;
			if (elem->counter_ == 0) {
				DEBUG("UNBLOCKING elem->wait_condition_");
				elem->wait_condition_.notify_all();
			}
			break;
		}
		case (msg_type_t::MSG_BARRIER_UNBLOCK): {
			DEBUG("Received MSG_BARRIER_UNBLOCK");
			if (pbsm_tid == 0)
				ERROR("Received message MSG_BARRIER_UNBLOCK but I'm the master");

			DEBUG("Waking up blocked thread...");
			DEBUG("UNBLOCKING slave_wait_barrier_");
			slave_wait_barrier_.notify_all();
			break;
		}
		case (msg_type_t::MSG_SET_NEW_OWNER): {
			DEBUG("Received MSG_SET_NEW_OWNER");

			var_data* v = dictionary_[msg.id];
			if (v != nullptr){
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				change_owner(v, msg.data.node);
				// Request again ownership to the right node but do not lock
				send_request_ownership(v);
			}
			break;
		}
		case (msg_type_t::MSG_INVALIDATE_COPY): {
			DEBUG("Received MSG_INVALIDATE_COPY");

			var_data* v = dictionary_[msg.id];
			if (v != nullptr){
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				v->policy_data_.state_ = state::REMOTE_OWNER_NO_CACHED;
			}
			DEBUG("Sending MSG_INVALIDATE_COPY_ACK...");
			msg_t ans;
			ans.type = msg_type_t::MSG_INVALIDATE_COPY_ACK;
			ans.data.node = pbsm_tid;
			ans.id = msg.id;
			if (!CommunicationHandler::getInstance().send_to(&ans, sizeof(ans), msg.data.node)) {
				ERROR("ERROR in sending MSG_INVALIDATE_COPY_ACK for variable " << msg.id);
			} else {
				DEBUG("MSG_INVALIDATE_COPY_ACK sent for variable " << msg.id);
			}

			break;
		}
		case (msg_type_t::MSG_INVALIDATE_COPY_ACK): {
			DEBUG("Received MSG_INVALIDATE_COPY_ACK");

			var_data* v = dictionary_[msg.id];
			if (v != nullptr){
				std::unique_lock<std::mutex> lock (v->policy_data_.mutex_);
				v->policy_data_.waiting_invalidate_copies_.counter_--;
				if (v->policy_data_.waiting_invalidate_copies_.counter_ == 0){
					DEBUG("UNBLOCKING waiting_invalidate_copies_");
					v->policy_data_.waiting_invalidate_copies_.wait_condition_.notify_all();
				}
			}
			break;
		}

		default: {
			ERROR("ERROR: Unrecognized message");
		}
		}
	}
}


void Policy::thread_wait_master_barrier(uint32_t s)
{
	// We're the master node
	DEBUG("We're the master node.");
	std::unique_lock<std::mutex> lock (mutex_);
	semaphore* elem = master_waiting_barrier_grants_[s];
	if (elem == nullptr) {
		DEBUG("I'm first node to reach the barrier. Creating data structures...");
		elem = new semaphore;
		elem->counter_ = CommunicationHandler::getInstance().get_number_of_nodes();
		master_waiting_barrier_grants_[s] = elem;
	}
	elem->counter_--;
	if (elem->counter_ > 0) {
		DEBUG("Waiting other nodes...");
		DEBUG("BLOCKING on elem->wait_condition_");
		elem->wait_condition_.wait(lock);
	}
	DEBUG("All nodes already reached the barrier.");

	delete elem;
	master_waiting_barrier_grants_[s] = nullptr;

	DEBUG("Sending MSG_BARRIER_UNBLOCK to everybody...");
	msg_t ans;
	ans.type = msg_type_t::MSG_BARRIER_UNBLOCK;
	ans.id = s;
	if (!CommunicationHandler::getInstance().send_to_all(&ans, sizeof(ans)))
			ERROR("ERROR in sending MSG_BARRIER_UNBLOCK");
}



void Policy::thread_wait_slave_barrier(uint32_t s)
{
	// We're not the master node. We're a slave.
	// We have to send barrier message and wait
	DEBUG("I'm slave.");
	msg_t ans;
	ans.type = msg_type_t::MSG_BARRIER_BLOCK;
	ans.id = s;
	std::unique_lock<std::mutex> lock (mutex_);
	DEBUG("Sending MSG_BARRIER_BLOCK...");
	if (!CommunicationHandler::getInstance().send_to(&ans, sizeof(ans), 0))
		ERROR("ERROR in sending MSG_BARRIER_BLOCK");
	DEBUG("Waiting master's answer...");
	DEBUG("BLOCKING on slave_wait_barrier_");
	slave_wait_barrier_.wait(lock);
}
