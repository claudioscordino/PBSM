#ifndef ABSTRACT_SHARED_HPP_
#define ABSTRACT_SHARED_HPP_

#include <string>

/**
 * @brief Abstract class for shared<> variable
 *
 * This class stores the variable id and provides a basic interface for setting/getting value.
 */
class AbstractShared {
public:
	explicit AbstractShared(uint32_t s): id_(s) {}

	virtual bool get_value(void* buffer)=0;
	virtual bool set_value(void* buffer)=0;
	virtual std::size_t get_size() const=0;
	inline uint32_t get_id() const {
		return id_;
	}

private:
	const uint32_t id_;
};

	
#endif // ABSTRACT_SHARED_HPP_
