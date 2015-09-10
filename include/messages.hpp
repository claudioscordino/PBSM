#ifndef MESSAGES_HPP_
#define MESSAGES_HPP_

#include <cstdint>

/// Type of messages
enum class msg_type_t
{
	/// Message sent when one node attempts to write on a variable not owned
	MSG_REQUEST_OWNERSHIP		= 1,

	/// Message sent to grant ownership of a variable to a node that wants to write on it.
	/// Message sent in response to MSG_REQUEST_OWNERSHIP.
	MSG_GRANT_OWNERSHIP		= 2,

	/// Message sent to specify the new owner.
	/// Message sent in response to MSG_REQUEST_OWNERSHIP if the owner has changed in the meantime.
	MSG_SET_NEW_OWNER		= 3,

	/// Message sent to get the latest value of a variable when a node is reading a variable not owned.
	MSG_ASK_CURRENT_VALUE		= 4,

	/// Message sent to specify the new value of a variable.
	/// Message sent in response to MSG_ASK_CURRENT_VALUE.
	MSG_SET_NEW_VALUE		= 5,

	/// Message sent by the slaves node to the master node when reaching a barrier.
	MSG_BARRIER_BLOCK		= 6,

	/// Message sent by the master node to slaves to unblock from the barrier.
	/// Message sent in response to MSG_BARRIER_BLOCK.
	MSG_BARRIER_UNBLOCK		= 7,

	/// Message sent my a owner node to a not-owner node to invalidate the cached value
	MSG_INVALIDATE_COPY		= 8,

	/// Message sent by a non-owner node in response to MSG_INVALIDATE_COPY
	MSG_INVALIDATE_COPY_ACK		= 9,
};

#pragma pack(1)

/**
 * @brief General message
 *
 */
struct msg_t
{
	/// Type of message
	msg_type_t type;
	/// Variable or barrier ID
	uint32_t id;
	union {
		/// Node ID
		unsigned long int node;
		/// Variable size
		unsigned long int var_size;
	} data;
	// In case of MSG_SET_NEW_VALUE, the value is sent after this message
};

#pragma pack()

///////////////////////////////////////////////

#endif // MESSAGES_HPP_
