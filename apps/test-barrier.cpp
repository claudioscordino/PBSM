#include <iostream>
#include <thread>
#include <cassert>

#include "logger.hpp"
#include "pbsm.hpp"

int main (int argc, char* argv[])
{
	LOG_FILE("/tmp/pbsm.log");
	pbsm_init(argc, argv);

	std::this_thread::sleep_for(std::chrono::seconds(10));

	for(;;)
		PBSM_BARRIER();

	DEBUG("DONE!");

	return 0;
}
