#include <type_traits>
#include <assert.h>
#include <string.h>
#include <algorithm>

#define GROWTH_FACTOR 4 // but really anything => 2 is bad because memory fragmentation

template<typename T>
	class myvector {
public:
	static const int DEFAULT_BYTE_CAPACITY = 64;
private:
	T * data_;	
	int size_{ 0 };
	int capacity_;	
	char small_data_[DEFAULT_BYTE_CAPACITY];
private:
	// Data is located in small_data_ array, no dynamically allocated memory is used
	bool is_small() const {
		return reinterpret_cast<char *>(data_) == small_data_;
	}

	T * get_memory(int count) {
		void * p = nullptr;
		if (!is_small() && std::is_trivially_move_constructible<T>::value)
			p = realloc(data_, count*sizeof(T));
		else
			p = malloc(count*sizeof(T));
		return reinterpret_cast<T *>(p);
	}

	void grow() {
		reserve(capacity_ * GROWTH_FACTOR + 1);
	}

public:
	myvector() {
		capacity_ = DEFAULT_BYTE_CAPACITY / sizeof(T);
		data_ = reinterpret_cast<T *>(small_data_);
	};

	~myvector() {
		clear();
		if (!is_small())
			free(data_);
	}

	myvector(const myvector<T> & rhs): myvector() {
		if (!rhs.is_small())
			data_ = get_memory(rhs.capacity_);
		capacity_ = rhs.capacity_;
		if (std::is_trivially_copyable<T>::value) {
			memcpy(data_, rhs.data_, rhs.size_*sizeof(T));
			size_ = rhs.size_;
		}
		else {
			for (int i = 0; i < rhs.size_; ++i) {
				new (data_ + i) T(rhs.data_[i]);
				++size_;
			}
		}
	}

	myvector<T> &operator=(myvector<T> rhs) {
		swap(*this, rhs);
		return *this;
	}

	friend void swap(myvector<T> & lhs, myvector<T> & rhs) // nothrow
	{
		// enable ADL (not necessary in our case, but good practice)
		using std::swap;
		using std::move;

		myvector<T> & sh = lhs;
		myvector<T> & lo = rhs;
		if (lhs.size_ > rhs.size_) {
			sh = rhs;
			lo = lhs;
		}
		if (sh.is_small() && lo.is_small()) {
			for (int i = 0; i < sh.size_; i++)
				std::swap(sh.data_[i], lo.data_[i]);
			for (int i = sh.size_; i < lo.size_; i++)
				sh.data_[i] = std::move(lo.data_[i]);
			std::swap(sh.size_, lo.size_);
		}
		else if (sh.is_small()) {
			T * psmall = reinterpret_cast<T *>(lo.small_data_);
			std::move(sh.begin(), sh.end(), psmall);
			sh.data_ = lo.data_;
			lo.data_ = psmall;
			std::swap(sh.size_, lo.size_);
			std::swap(sh.capacity_, lo.capacity_);
		}
		else {
			std::swap(sh.data_, lo.data_);
			std::swap(sh.size_, lo.size_);
			std::swap(sh.capacity_, lo.capacity_);
		}
	}

	int capacity() const {
		return capacity_;
	}

	int size() const {
		return size_;
	}

	void add(const T & value) {
		if (size_ == capacity_)
			grow();
		new (data_+size_) T(value);
		++size_;
	};

	T & add() {
		if (size_ == capacity_)
			grow();		
		new (data_ + size_) T();
		return data_[size_++];
	};

	void erase(int index) {
		assert(index >= 0 && index < size());
		int new_size = size_ - 1;
		if (std::is_trivially_move_assignable<T>::value) {
			T * dest = data_ + index;
			memmove(dest, dest+1, (new_size - index)*sizeof(T));
		}
		else {			
			for (int i = index; i < new_size; ++i)
				data_[i] = data_[i + 1];
			data_[size_ - 1].~T();
		}
		size_ = new_size;
	}

	void push_back(const T & value) {
		add(value);
	}

	void erase(const T * item) {		
		erase(static_cast<int>(item - data_));
	}

	T & operator[](int index) {
		assert(index >= 0 && index < size_);
		return data_[index];
	}

	const T & operator[](int index) const {
		assert(index >= 0 && index < size_);
		return data_[index];
	}

	T * begin() const {
		return data_;
	}

	T * end() const {
		return data_ + size_;
	}

	T & back() const {
		assert(size() > 0);
		return data_[size_ - 1];
	}

	void clear() {
		resize(0);
	}

	void resize(int new_size) {
		if (new_size <= size_) {
			if (std::is_pod<T>::value)
				size_ = new_size;
			else {
				for (int i = size_-1; i >= new_size; --i) {
					data_[i].~T();
					--size_;
				}
			}
		}
		else {
			reserve(new_size);
			int add_count = new_size - size_;
			for (int i = 0; i < add_count; ++i) {
				add();
			}
		}
	}

	void reserve(int min_capacity) {
		if (min_capacity > capacity()) {
			T * new_data = get_memory(min_capacity);
			if (is_small() || !std::is_trivially_move_constructible<T>::value) {
				std::move(data_, data_ + size_, new_data);
/*				for (int i = 0; i < size_; ++i) {
					new (new_data + i) T(data_[i]);
					data_[i].~T();
				}
*/
				if (!is_small())
					free(data_);
			}
			data_ = new_data;
			capacity_ = min_capacity;
		}
	}
};