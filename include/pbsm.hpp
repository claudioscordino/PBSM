#ifndef PBSM_HPP
#define PBSM_HPP

#include <cstdlib>	// atoi()

#include "communication_handler.hpp"
#include "policy.hpp"
#include "shared.hpp"

/// Macro for barrier synchronization.
#define PBSM_BARRIER() Policy::getInstance().thread_wait_barrier(HASH(__FILE__ ":" TOSTRING(__LINE__)))

///  Macro for getting total number of nodes (included the current node)
#define pbsm_hosts CommunicationHandler::getInstance().get_number_of_nodes()

/**
 * @brief Thread ID.
 *
 * The master node (0) is the initial owner of all variables and responsible for barrier synchronization.
 * This variable is set by pbsm_init() and made available to both the user application and the rest of the run-time.
 */
int pbsm_tid = -1;

// FIXME: check if something here is needed.
void pbsm_cleanup()
{
	std::cerr << "Exiting from program!" << std::endl;
}

void pbsm_init(int argc, char* argv [])
{
	if (argc != 2) {
		ERROR("Wrong number of arguments. Please specyfy node id.");
		exit (-1);
	}

	for (int i=0; i < 1000; ++i)
		DEBUG("%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%");

	atexit(pbsm_cleanup);
	pbsm_tid = atoi (argv[1]);
	if (pbsm_tid == 0)
		// We are the master
		Policy::getInstance().master_node_init();
	else
		// We are a slave
		Policy::getInstance().slave_node_init();

	CommunicationHandler::getInstance().create_connections();
	Policy::getInstance().start_receiving();
}

#endif // PBSM_HPP

