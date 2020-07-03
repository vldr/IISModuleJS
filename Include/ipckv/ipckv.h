#pragma once
#include <Windows.h>
#include <string>
#include <iostream>
#include <tuple> 

#define LOG(...) printf(__VA_ARGS__)
#define IPCKV_MAX_LOCKS 24 

#define IPCKV_MAX_LOAD_FACTOR 0.6f  
#define IPCKV_INITIAL_CAPACITY 101
#define IPCKV_DATA_SIZE 2048 
#define IPCKV_KEY_SIZE 260

#define IPCKV_LOAD_FACTOR (float)m_controller->getSize() / (float)m_controller->getCapacity()

#define IPCKV_C1_CONSTANT 3
#define IPCKV_C2_CONSTANT 5

#define IPCKV_READ_LOCK false
#define IPCKV_WRITE_LOCK true

#define IPCKV_BIT_HIGH 0b00000001

class IPC_Lock;
class IPC_KV_Controller;
struct IPC_KV_Data;
struct IPC_KV_Info;

class IPC_KV
{
public:
	/**
	* Constructors and destructors
	*/
	IPC_KV(const std::string& name);
	~IPC_KV();

	/**
	* Public Methods
	*/
	void set(const std::string& key, unsigned char* data, size_t size);
	bool get(const std::string& key, unsigned char* data, size_t& size);
	bool remove(const std::string& key);
	void clear();
	void print();
	size_t size();
	void close();
private:
	/**
	* Private Methods
	*/
	void initialize_info(const std::string& name);
	std::tuple<IPC_KV_Data*, HANDLE> initialize_data(const std::string& name, size_t capacity, size_t resize_count);

	void resize();
	bool is_prime(size_t input);

	size_t find_nearest_prime(size_t input);
	uint32_t hash(const char* key, size_t count);
	IPC_Lock get_lock(bool is_writing);

	/**
	* Private Members
	*/
	IPC_KV_Controller* m_controller = nullptr;
	std::string m_name;
	size_t m_resize_count;
};

enum IPC_KV_Data_State
{
	Empty = 0,
	Deleted = 1,
	Occupied = 2,
};

struct IPC_KV_Data
{
	IPC_KV_Data_State m_state[2];

	char m_key[2][IPCKV_KEY_SIZE];
	unsigned char m_value[2][IPCKV_DATA_SIZE];

	size_t m_size[2];

	char m_buffer_state;
};

struct IPC_KV_Info
{
	char m_buffer_state;
	size_t m_capacity[2];
	size_t m_size[2];
	size_t m_resize_count[2];
};

class IPC_KV_Controller
{
public:
	IPC_KV_Controller() {}

	~IPC_KV_Controller()
	{
		if (m_data)
			UnmapViewOfFile(m_data);

		if (m_info)
			UnmapViewOfFile(m_info);

		if (m_data_handle)
			CloseHandle(m_data_handle);

		if (m_info_handle)
			CloseHandle(m_info_handle);
	}

	// Disallow copying and moving.
	IPC_KV_Controller(const IPC_KV_Controller&) = delete;
	IPC_KV_Controller(IPC_KV_Controller&&) = delete;
	IPC_KV_Controller& operator=(const IPC_KV_Controller&) = delete;
	IPC_KV_Controller& operator=(IPC_KV_Controller&&) = delete;


	/**
	* m_info Setters
	*/

	enum InfoTransaction
	{
		InfoNone = 0,
		InfoResizeCount = (1 << 0),
		InfoCapacity = (1 << 1),
		InfoSize = (1 << 2),
	};

	/**
	* Commit Info
	*/
	void commitInfo()
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		if (!m_has_started_info_transaction)
			throw std::runtime_error("a info transaction has not been started.");

		/////////////////////////////////////////////////

		if (!(m_info_transaction_flags & InfoTransaction::InfoResizeCount))
			setResizeCount(getResizeCount());

		if (!(m_info_transaction_flags & InfoTransaction::InfoCapacity))
			setCapacity(getCapacity());

		if (!(m_info_transaction_flags & InfoTransaction::InfoSize))
			setSize(getSize());

		/////////////////////////////////////////////////

		InterlockedXor8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		m_has_started_info_transaction = false;
	}

	void startInfoTransaction()
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		if (m_has_started_info_transaction)
			throw std::runtime_error("a info transaction has already been started.");

		m_has_started_info_transaction = true;
		m_info_transaction_flags = InfoTransaction::InfoNone;
	}

	void setResizeCount(size_t resize_count)
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		if (!m_has_started_info_transaction)
			throw std::runtime_error("a info transaction has not been started.");

		bool buffer_state = !InterlockedAnd8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		m_info->m_resize_count[buffer_state] = resize_count;
		m_info_transaction_flags = (InfoTransaction)(m_info_transaction_flags | InfoTransaction::InfoResizeCount);
	}

	void setCapacity(size_t capacity)
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		if (!m_has_started_info_transaction)
			throw std::runtime_error("a info transaction has not been started.");

		bool buffer_state = !InterlockedAnd8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		m_info->m_capacity[buffer_state] = capacity;
		m_info_transaction_flags = (InfoTransaction)(m_info_transaction_flags | InfoTransaction::InfoCapacity);
	}

	void setSize(size_t size)
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		if (!m_has_started_info_transaction)
			throw std::runtime_error("a info transaction has not been started.");

		bool buffer_state = !InterlockedAnd8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		m_info->m_size[buffer_state] = size;
		m_info_transaction_flags = (InfoTransaction)(m_info_transaction_flags | InfoTransaction::InfoSize);
	}

	/**
	* m_Data Setters
	*/

	enum DataTransaction
	{
		DataNone = 0,
		DataState = (1 << 0),
		DataKey = (1 << 1),
		DataValue = (1 << 2),
		DataSize = (1 << 3),
	};

	/**
	* Commit Data
	*/
	void commitData(size_t index)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		if (!m_has_started_data_transaction)
			throw std::runtime_error("a data transaction has not been started.");

		/////////////////////////////////////////////////

		if (!(m_data_transaction_flags & DataTransaction::DataSize))
			setDataSize(index, getDataSize(index));

		if (!(m_data_transaction_flags & DataTransaction::DataState))
			setDataState(index, getDataState(index));

		if (!(m_data_transaction_flags & DataTransaction::DataValue))
			setData(index, getData(index), getDataSize(index));

		if (!(m_data_transaction_flags & DataTransaction::DataKey))
			setDataKey(index, getDataKey(index), strlen(getDataKey(index)));

		///////////////////////////////////////////////// 

		InterlockedXor8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		m_has_started_data_transaction = false;
	}

	void startDataTransaction(size_t index)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		if (m_has_started_data_transaction)
			throw std::runtime_error("a data transaction has already been started.");

		m_has_started_data_transaction = true;
		m_data_transaction_flags = DataTransaction::DataNone;
	}

	void setDataSize(size_t index, size_t size)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = !InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		m_data[index].m_size[buffer_state] = size;
		m_data_transaction_flags = (DataTransaction)(m_data_transaction_flags | DataTransaction::DataSize);
	}

	void setDataState(size_t index, IPC_KV_Data_State state)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = !InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		m_data[index].m_state[buffer_state] = state;
		m_data_transaction_flags = (DataTransaction)(m_data_transaction_flags | DataTransaction::DataState);
	}

	void setData(size_t index, unsigned char* data, size_t size)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = !InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		memcpy_s(m_data[index].m_value[buffer_state], IPCKV_DATA_SIZE, data, size);
		m_data[index].m_size[buffer_state] = size;

		m_data_transaction_flags = (DataTransaction)(m_data_transaction_flags | DataTransaction::DataValue);
		m_data_transaction_flags = (DataTransaction)(m_data_transaction_flags | DataTransaction::DataSize);
	}

	void setDataKey(size_t index, const char* key, size_t size)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = !InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		strncpy_s(m_data[index].m_key[buffer_state], key, size);

		m_data_transaction_flags = (DataTransaction)(m_data_transaction_flags | DataTransaction::DataKey);
	}

	/**
	* m_info Getters
	*/

	size_t getCapacity()
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		return m_info->m_capacity[buffer_state];
	}

	size_t getSize()
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		return m_info->m_size[buffer_state];
	}

	size_t getResizeCount()
	{
		if (!m_info)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_info->m_buffer_state, IPCKV_BIT_HIGH);

		return m_info->m_resize_count[buffer_state];
	}

	/**
	* m_data Getters
	*/

	unsigned char* getData(size_t index)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		return m_data[index].m_value[buffer_state];
	}

	size_t getDataSize(size_t index)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		return m_data[index].m_size[buffer_state];
	}

	IPC_KV_Data_State getDataState(size_t index)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		return m_data[index].m_state[buffer_state];
	}

	char* getDataKey(size_t index)
	{
		if (!m_data)
			throw std::runtime_error("class is in an invalid state.");

		bool buffer_state = InterlockedAnd8(&m_data[index].m_buffer_state, IPCKV_BIT_HIGH);

		return m_data[index].m_key[buffer_state];
	}

	bool m_has_started_info_transaction = false;
	InfoTransaction m_info_transaction_flags = InfoTransaction::InfoNone;

	bool m_has_started_data_transaction = false;
	DataTransaction m_data_transaction_flags = DataTransaction::DataNone;

	IPC_KV_Info* m_info = nullptr;
	IPC_KV_Data* m_data = nullptr;

	HANDLE m_info_handle = nullptr;
	HANDLE m_data_handle = nullptr;
};

class IPC_Lock {
public:
	IPC_Lock(bool is_write_lock, const std::string& name)
	{
		if (is_write_lock)
		{
			if (name.length() > MAX_PATH)
			{
				throw std::runtime_error("rwlock name too long.");
			}

			auto mutex_name = name + "_mutex";

			mutex_handle = CreateMutexA(
				nullptr,
				FALSE,
				mutex_name.c_str()
			);

			if (mutex_handle == nullptr)
			{
				throw std::runtime_error("could not create mutex.");
			}

			auto wait_result = WaitForSingleObject(mutex_handle, INFINITE);

			if (wait_result != WAIT_OBJECT_0)
				throw std::runtime_error("failed to wait for semaphore object");

			//////////////////////////////////////////////

			semaphore_handle = CreateSemaphoreA(
				nullptr,
				0,
				IPCKV_MAX_LOCKS,
				name.c_str()
			);

			if (semaphore_handle == nullptr)
			{
				throw std::runtime_error("could not create rwlock.");
			}

			if (GetLastError() == ERROR_ALREADY_EXISTS)
			{
				for (int i = 0; i < IPCKV_MAX_LOCKS; i++)
				{
					auto wait_result = WaitForSingleObject(semaphore_handle, INFINITE);

					if (wait_result != WAIT_OBJECT_0)
						throw std::runtime_error("failed to wait for semaphore object");
				}
			}
		}
		else
		{
			if (name.length() > MAX_PATH)
			{
				throw std::runtime_error("rwlock name too long.");
			}

			semaphore_handle = CreateSemaphoreA(
				nullptr,
				IPCKV_MAX_LOCKS,
				IPCKV_MAX_LOCKS,
				name.c_str()
			);

			if (semaphore_handle == nullptr)
			{
				throw std::runtime_error("could not create rwlock.");
			}

			auto wait_result = WaitForSingleObject(semaphore_handle, INFINITE);

			if (wait_result != WAIT_OBJECT_0)
				throw std::runtime_error("failed to wait for single object");
		}

	}

	~IPC_Lock() noexcept
	{
		if (semaphore_handle)
		{
			ReleaseSemaphore(semaphore_handle, IPCKV_MAX_LOCKS, nullptr);
			CloseHandle(semaphore_handle);
		}

		if (mutex_handle)
		{
			ReleaseMutex(mutex_handle);
			CloseHandle(mutex_handle);
		}
	}

	IPC_Lock(const IPC_Lock&) = delete;
	IPC_Lock& operator=(IPC_Lock const&) = delete;

	IPC_Lock(IPC_Lock&& ipc_write_lock) noexcept :
		semaphore_handle(std::exchange(ipc_write_lock.semaphore_handle, nullptr)),
		mutex_handle(std::exchange(ipc_write_lock.mutex_handle, nullptr)) { }

	IPC_Lock& operator=(IPC_Lock&& ipc_write_lock)
	{
		semaphore_handle = std::exchange(ipc_write_lock.semaphore_handle, nullptr);
		mutex_handle = std::exchange(ipc_write_lock.mutex_handle, nullptr);

		return *this;
	}
private:
	HANDLE semaphore_handle = nullptr;
	HANDLE mutex_handle = nullptr;
};