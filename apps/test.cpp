#include <iostream>
#include <thread>
#include <cassert>

#include "logger.hpp"
#include "pbsm.hpp"


int main (int argc, char* argv[])
{
	LOG_FILE("/tmp/pbsm.log");
	pbsm_init(argc, argv);
	std::cout << "Starting application!" << std::endl;

	std::this_thread::sleep_for(std::chrono::seconds(10));

	DEBUG("pbsm_tid = " << pbsm_tid << "\t pbsm_hosts = " << pbsm_hosts);
	DEBUG("Main.cpp thread id: " << std::this_thread::get_id());

	// Example of variable of fundamental type allocated on the stack:
	shared<int> a (DEF, 0);

	PBSM_BARRIER();

	while (a != 10) {
		if ((pbsm_tid == 0) && (a%2 == 0)) {
			// The Master node increments variable when even
			DEBUG("Calling a++ to increment variable from " << a << "...");
			a++;
		} else if ((pbsm_tid != 0) && (a%2 == 1)) {
			// The Slave node increments variable when odd
			DEBUG("Calling a++ to increment variable from " << a << "...");
			a++;
		}

		DEBUG("a = " << a);
		//PBSM_BARRIER();
	}

	PBSM_BARRIER();

	std::cout << "DONE!" << std::endl;

	return 0;
}
