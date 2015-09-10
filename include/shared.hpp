#ifndef SHARED_HPP_
#define SHARED_HPP_

#include <mutex>

#include "abstract_shared.hpp"
#include "logger.hpp"
#include "policy.hpp"

/** Macro that must be provided to shared<> constructor
 * for generating variable's id based upon location of its
 * definition in the source code.
 */
//#define PBSM (__FILE__ + std::to_string(__LINE__))

#define H1(s,i,x)   (x*65599u+(uint8_t)s[(i)<sizeof(s)?sizeof(s)-1-(i):sizeof(s)-1])
#define H4(s,i,x)   H1(s,i,H1(s,i+1,H1(s,i+2,H1(s,i+3,x))))
#define H16(s,i,x)  H4(s,i,H4(s,i+4,H4(s,i+8,H4(s,i+12,x))))
#define H64(s,i,x)  H16(s,i,H16(s,i+16,H16(s,i+32,H16(s,i+48,x))))
#define H256(s,i,x) H64(s,i,H64(s,i+64,H64(s,i+128,H64(s,i+192,x))))
#define H512(s,i,x) H256(s,i,H256(s,i+256,H256(s,i+512,H256(s,i+768,x))))
#define HASH(s)    ((uint32_t)(H64(s,0,0)^(H64(s,0,0)>>16)))

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define DEF HASH(__FILE__ ":" TOSTRING(__LINE__))


/**
 * These two templates implement the shared<> variables.
 * We need two templates for dealing with both fundamental (e.g., int)
 * types and user-defined types.
 * The implementation relies on template specialization, C++11 type traits
 * and operator overloading. It is explained at
 *
 *	http://stackoverflow.com/a/30803131/2214693
 */

/// Base template class for non-fundamental (i.e., user-defined) types
template<class T, class=void>
class shared : public T, public AbstractShared {
public:
	/// Mechanism for exposing the methods of the user-defined type
	using T::T;

	/// Constructor
	explicit shared(uint32_t s): T(), AbstractShared(s), temp_object_(false)  {
		DEBUG("New variable " << get_id() << " created.");

		// Inform the policy that a new variable has been created:
		Policy::getInstance().at_variable_creation(this);
	}

	/// Constructor
	explicit shared(uint32_t s, T init): T(init), AbstractShared(s), temp_object_(false)  {
		DEBUG("New variable " << get_id() << " created.");

		// Inform the policy that a new variable has been created:
		Policy::getInstance().at_variable_creation(this);
	}

	/// Copy constructor
	shared(shared& other): T(other), AbstractShared(0), temp_object_(true) {
		// Inform the policy that a new variable has been created:
		// Policy::getInstance().at_variable_creation(this);
		DEBUG("Temporary object created from variable " << other.id_);
	}

	/// Const copy constructor
	shared(const shared& other): T(other), AbstractShared(0), temp_object_(true) {
		// Inform the policy that a new variable has been created:
		// Policy::getInstance().at_variable_creation(this);
		DEBUG("Temporary object created from variable " << other.id_);
	}

	/// Destructor
	virtual ~shared() {
		DEBUG("Variable's destructor called!");
		if (!temp_object_) {
			DEBUG("Destroying not temporary object");
			std::unique_lock<std::mutex> lock (mutex_);
			// Inform the policy that a new variable has been destroyed:
			Policy::getInstance().at_variable_destruction(get_id(), (T*) this , sizeof(T));
		} else {
			DEBUG("Destroying temporary object");
		}
	}

	/// Prefix increment
	shared operator++(){
		Policy::getInstance().before_local_write(get_id());
		mutex_.lock();
		T::operator++();
		mutex_.unlock();
		Policy::getInstance().after_local_write(get_id());
		return *this;
	}

	/// Postfix increment
	shared operator++(int){
		Policy::getInstance().before_local_write(get_id());
		mutex_.lock();
		shared<T> ret = *this;
		T::operator++();
		mutex_.unlock();
		Policy::getInstance().after_local_write(get_id());
		return ret;
	}

	/// Assignment operator
	shared& operator=(shared& other) {
		if (this != (&other)){
			Policy::getInstance().before_local_write(get_id());
			mutex_.lock();
			other.mutex_.lock();
			T::operator=(other);
			other.mutex_.unlock();
			mutex_.unlock();
			Policy::getInstance().after_local_write(get_id());
		}
		return *this;
	}
	
	/// T assignment operator
	shared& operator=(T other) {
		Policy::getInstance().before_local_write(get_id());
		mutex_.lock();
		T::operator=(other);
		mutex_.unlock();
		Policy::getInstance().after_local_write(get_id());
		return *this;
	}

	T* operator->() {
		// Refresh the value:
		Policy::getInstance().before_local_read(get_id());
		return (T*) this;
	}

#if 0
	shared& operator*() {
		// Refresh the value:
		Policy::getInstance().before_local_read(get_id());
		return *this;
	}


	T& operator()() {
		return *this;
	}
#endif

	/// Conversion to T (e.g., conversion to int)
	operator T() {
		Policy::getInstance().before_local_read(get_id());
		return (T) this;
	}

	/**
	 * @brief Change value of the variable
	 *
	 * This method is called asynchronously when the owner node sends the updated value
	 * (because this node has tried to read the variable).
	 *
	 * FIXME: performance issues: we can't have messages exchange every time that a node
	 * reads a variable.
	 *
	 * @param data Raw buffer containing the new value
	 */
	virtual bool set_value(void* new_value_buffer) {
		DEBUG("Setting new raw value");
		if (new_value_buffer == nullptr) {
			ERROR("ERROR: set_value() got nullptr");
			return false;
		} else {
			mutex_.lock();
			T::operator= (*((T*)new_value_buffer));
			mutex_.unlock();
			return true;
		}
	}

	/**
	 * @brief Get size of the variable
	 *
	 * This method is used for serializing data in network communications.
	 */
	std::size_t get_size() const {
		return sizeof(T);
	}

	/**
	 * @brief Get the value of the variable
	 *
	 * @param data Raw buffer where the variable value must be written
	 */
	bool get_value(void* current_value_buffer) {
		DEBUG("Getting new raw value");
		if (current_value_buffer == nullptr) {
			ERROR("ERROR: get_value() got nullptr");
			return false;
		} else {
			std::unique_lock<std::mutex> lock (mutex_);
			T* fill = (T*) current_value_buffer;
			*fill = *this;
			return true;
		}
	}

	bool operator== (const T& oth) {
		DEBUG("operator== called");
		Policy::getInstance().before_local_read(get_id());
		return T::operator==(oth);
	}

	bool operator== (const shared& oth) {
		DEBUG("operator== called");
		Policy::getInstance().before_local_read(get_id());
		T t = oth;
		return T::operator==(t);
	}

	bool operator!= (const T& oth) {
		DEBUG("operator!= called");
		Policy::getInstance().before_local_read(get_id());
		return T::operator!=(oth);
	}

	bool operator!= (const shared& oth) {
		DEBUG("operator!= called");
		Policy::getInstance().before_local_read(get_id());
		T t = oth;
		return T::operator!=(t);
	}

	T operator% (T oth) {
		DEBUG("operator% called");
		Policy::getInstance().before_local_read(get_id());
		return T::operator%(oth);
	}


private:

	/// Lock for mutual exclusion to access data
	std::mutex mutex_;

	bool temp_object_;

};


/// Specialization class for fundamental types (e.g., int)
template<class T>
class shared<T, typename std::enable_if<!std::is_class<T>{}>::type >: public AbstractShared{
public:
	/// Constructor
	explicit shared(uint32_t s): AbstractShared(s), temp_object_(false) {
		DEBUG("New variable " << get_id() << " created.");

		// Inform the policy that a new variable has been created:
		Policy::getInstance().at_variable_creation(this);
	}

	explicit shared(uint32_t s, T init): AbstractShared(s), data_(init), temp_object_(false) {
		DEBUG("New variable " << get_id() << " created.");

		// Inform the policy that a new variable has been created:
		Policy::getInstance().at_variable_creation(this);
	}

	/// Copy constructor
	shared(shared& other): AbstractShared(0), data_(other.data_), temp_object_(true) {
		// Inform the policy that a new variable has been created:
		//P olicy::getInstance().at_variable_creation(this);
		DEBUG("Temporary object created from variable " << other.get_id());
	}

	/// Const copy constructor
	shared(const shared& other): AbstractShared(0), data_(other.data_), temp_object_(true) {
		// Inform the policy that a new variable has been created:
		// Policy::getInstance().at_variable_creation(this);
		DEBUG("Temporary object created from variable " << other.get_id());
	}

	/// Destructor
	virtual ~shared() {
		DEBUG("Variable's destructor called!");

		if (!temp_object_) {
			DEBUG("Destroying not temporary object");
			std::unique_lock<std::mutex> lock (mutex_);
			// Inform the policy that a new variable has been destroyed:
			Policy::getInstance().at_variable_destruction(get_id(), (void*) &data_, sizeof(T));
		} else {
			DEBUG("Destroying temporary object");
		}
	}

	/// Prefix increment
	shared operator++(){
		Policy::getInstance().before_local_write(get_id());
		mutex_.lock();
		data_++;
		mutex_.unlock();
		Policy::getInstance().after_local_write(get_id());
		return *this;
	}

	/// Postfix increment
	shared operator++(int){
		Policy::getInstance().before_local_write(get_id());
		mutex_.lock();
		shared<T> ret = *this;
		data_++;
		mutex_.unlock();
		Policy::getInstance().after_local_write(get_id());
		return ret;
	}

	/// Assignment operator
	shared& operator=(shared& other) {
		if (this != (&other)){
			Policy::getInstance().before_local_write(get_id());
			mutex_.lock();
			other.mutex_.lock();
			data_ = other.data_;
			other.mutex_.unlock();
			mutex_.unlock();
			Policy::getInstance().after_local_write(get_id());
		}
		return *this;
	}

	/// T assignment operator
	shared& operator=(T other) {
		DEBUG("Called operator=(T)");
		Policy::getInstance().before_local_write(get_id());
		mutex_.lock();
		data_ = other;
		mutex_.unlock();
		Policy::getInstance().after_local_write(get_id());
		return *this;
	}

	T* operator->() {
		Policy::getInstance().before_local_read(get_id());
		return (T*) &data_;
	}

#if 0
	shared& operator*() {
		return *this;
	}

	T& operator()() {
		return data_;
	}
#endif

	/// Conversion to T (e.g., conversion to int)
	operator T() {
		Policy::getInstance().before_local_read(get_id());
		return data_;
	}

	/**
	 * @brief Change value of the variable
	 *
	 * This method is called asynchronously when the owner node sends the updated value
	 * (because this node has tried to read the variable).
	 *
	 * FIXME: performance issues: we can't have messages exchange every time that a node
	 * reads a variable.
	 *
	 * @param data Raw buffer containing the new value
	 */
	virtual bool set_value(void* new_value_buffer) {
		DEBUG("Setting new raw value");
		if (new_value_buffer == nullptr) {
			ERROR("ERROR: set_new_value() got nullptr");
			return false;
		} else {
			std::unique_lock<std::mutex> lock (mutex_);
			data_ = *((T*) new_value_buffer);

			return true;
		}
	}

	/**
	 * @brief Get size of the variable
	 *
	 * This method is used for serializing data in network communications.
	 */
	std::size_t get_size() const {
		return sizeof(T);
	}

	/**
	 * @brief Get the value of the variable
	 *
	 * @param data Raw buffer where the variable value must be written
	 */
	bool get_value(void* current_value_buffer) {
		DEBUG("Getting new raw value");
		if (current_value_buffer == nullptr) {
			ERROR("ERROR: get_value() got nullptr");
			return false;
		} else {
			std::unique_lock<std::mutex> lock (mutex_);
			T* fill = (T*) current_value_buffer;
			*fill = data_;
			return true;
		}
	}

	bool operator== (const T& oth) {
		DEBUG("operator== called");
		Policy::getInstance().before_local_read(get_id());
		return (data_==oth);
	}

	bool operator== (const shared& oth) {
		DEBUG("operator== called");
		Policy::getInstance().before_local_read(get_id());
		return (data_==oth.data_);
	}

	bool operator!= (const T& oth) {
		DEBUG("operator!= called");
		Policy::getInstance().before_local_read(get_id());
		return (data_!=oth);
	}

	bool operator!= (const shared& oth) {
		DEBUG("operator!= called");
		Policy::getInstance().before_local_read(get_id());
		return (data_!=oth.data_);
	}

	T operator% (int oth) {
		DEBUG("operator% called");
		Policy::getInstance().before_local_read(get_id());
		return data_%oth;
	}


private:

	/// Actual data
	T data_;

	/// Lock for mutual exclusion to access data
	std::mutex mutex_;

	bool temp_object_;
};

	
#endif // SHARED_HPP_
