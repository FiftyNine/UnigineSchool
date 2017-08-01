#include <fstream>
#include <algorithm>
#include <type_traits>
#include <cassert>
#include <cstdio>
#include <windows.h>

#define __MIN(a, b) (a < b ? a : b)
#define __STRCMP(a, b, len) strncmp(a, b, len)

typedef unsigned char uchar;
typedef UINT (*HashFunc)(const char*, size_t);

#pragma region Hash functions

// MeiYan is the fastest function (out of ~20 benchmarked) for our set of keys
// Meiyan - 9103.44 ms, std::_Hash - 11394.6 ms, CRC-32 - 11085.9 ms, Murmur3 - 9785.24 ms
UINT hashMeiyan(const char *str, size_t wrdlen)
{
	const UINT PRIME = 709607;
	UINT hash32 = 2166136261;
	const char *p = str;

	for (; wrdlen >= 2 * sizeof(DWORD); wrdlen -= 2 * sizeof(DWORD), p += 2 * sizeof(DWORD)) {
		hash32 = (hash32 ^ (_rotl(*(DWORD *)p, 5) ^ *(DWORD *)(p + 4))) * PRIME;
	}
	// Cases: 0,1,2,3,4,5,6,7
	if (wrdlen & sizeof(DWORD)) {
		hash32 = (hash32 ^ *(WORD*)p) * PRIME;
		p += sizeof(WORD);
		hash32 = (hash32 ^ *(WORD*)p) * PRIME;
		p += sizeof(WORD);
	}
	if (wrdlen & sizeof(WORD)) {
		hash32 = (hash32 ^ *(WORD*)p) * PRIME;
		p += sizeof(WORD);
	}
	if (wrdlen & 1)
		hash32 = (hash32 ^ *p) * PRIME;

	return hash32 ^ (hash32 >> 16);
}

#pragma endregion

#pragma region String

struct String {
	char *str{ nullptr };
	int len{ 0 };
	String() {}; 
	explicit String(char * const data, int size) :
		str(data),
		len(size)
	{}
};

inline int compare(const String& x, const String& y)
{
	int m = __MIN(x.len, y.len);
	int res = __STRCMP(x.str, y.str, m);
	if (!res) {
		int dif = y.len - x.len;
		if (dif > 0)
			res = -1;
		else if (dif < 0)
			res = 1;
	}
	return res;
}

inline bool operator <(const String& x, const String& y) {
	return compare(x, y) > 0;
}

inline bool operator >(const String& x, const String& y) {
	return compare(x, y) < 0;
}

inline bool operator !=(const String& x, const String& y) {
	return compare(x, y) != 0;
}

inline std::ostream& operator<<(std::ostream& os, const String& str)
{
	os.write(str.str, str.len);
	return os;
}

#pragma endregion

#pragma region Vector

template<typename T>
class Vector {
public:
	static const int GROWTH_FACTOR = 4;
	static const int DEFAULT_CAPACITY = 8;
private:
	T * mData;
	int mSize{ 0 };
	int mCapacity;
private:
	T * getMemory(int count) {
		void * p = nullptr;
		if (std::is_trivially_move_constructible<T>::value)
			p = realloc(mData, count*sizeof(T));
		else
			p = malloc(count*sizeof(T));
		return reinterpret_cast<T *>(p);
	}

	void grow() {
		reserve(mCapacity * GROWTH_FACTOR + 1);
	}

public:
	Vector() {
		reserve(DEFAULT_CAPACITY);
	};

	~Vector()  {
		clear();
		free(mData);
	}

	int capacity() const {
		return mCapacity;
	}

	int size() const {
		return mSize;
	}

	void add(const T & value) {
		if (mSize == mCapacity)
			grow();
		new (mData + mSize) T(value);
		++mSize;
	};

	T & add() {
		if (mSize == mCapacity)
			grow();
		new (mData + mSize) T();
		return mData[mSize++];
	};

	T & operator[](int index) {
		assert(index >= 0 && index < mSize);
		return mData[index];
	}

	const T & operator[](int index) const {
		assert(index >= 0 && index < mSize);
		return mData[index];
	}

	T * begin() const {
		return mData;
	}

	T * end() const {
		return mData + mSize;
	}

	T & front() const {
		assert(size() > 0);
		return mData[0];
	}

	T & back() const {
		assert(size() > 0);
		return mData[mSize - 1];
	}

	void clear() {
		resize(0);
	}

	void resize(int new_size) {
		if (new_size <= mSize) {
			if (std::is_pod<T>::value)
				mSize = new_size;
			else {
				for (int i = mSize - 1; i >= new_size; i--) {
					mData[i].~T();
					mSize--;
				}
			}
		}
		else {
			reserve(new_size);
			int add_count = new_size - mSize;
			for (int i = 0; i < add_count; i++) {
				add();
			}
		}
	}

	void reserve(int min_capacity) {
		if (min_capacity > capacity()) {
			T * new_data = getMemory(min_capacity);
			if (!std::is_trivially_move_constructible<T>::value) {
				std::move(mData, mData + mSize, new_data);
				free(mData);
			}
			mData = new_data;
			mCapacity = min_capacity;
		}
	}
};

#pragma endregion

#pragma region WordCount

struct WordCount {
	String word;
	int count;
	WordCount() :
		count(0)
	{};

	explicit WordCount(const String &word, int count) :
		word(word),
		count(count)
	{}

	static bool greater(const WordCount& lhs, const WordCount& rhs) {
		return lhs.count > rhs.count
			|| lhs.count == rhs.count && lhs.word > rhs.word;
	}
};

typedef Vector<WordCount> WordList;

#pragma endregion

#pragma region StringPool

class StringPool {
public:	
	StringPool()	
	{
		mPools.add();
	}
	String add(char *str, int len) 
	{
		assert(len < Pool::SIZE);
		if (Pool::SIZE - mPools.back().used < len)
			mPools.add();
		mLast = len;
		return mPools.back().add(str, len);
	}
	void removeLast()
	{
		mPools.back().used -= mLast;
	}
private:		
	struct Pool {
		const static size_t SIZE = 1024 * 128; // 128 Kb
		char *data{ nullptr };
		int used{ 0 };
		String add(char *str, int len)
		{
			memcpy(data + used, str, len);
			String res(data + used, len);
			used += len;
			return res;
		}

		Pool() : 
			data(new char[SIZE])
		{};
		Pool(Pool &&that) :
			data(std::move(that.data)),
			used(std::move(that.used))
		{}
		~Pool() { delete data; }
	};
	Vector<Pool> mPools;
	int mLast{ 0 };
};

#pragma endregion

#pragma region HashMap

class FrequencyHashMap {
public:
	#define MAX_LOAD_FACTOR 0.7
	const static int START_SIZE = 16;
	typedef UINT Hash;

	struct Element {
		enum class State {
			Free,
			Used,
			Deleted
		};
		Hash hash;
		int val;
		String key;
		State state;
		Element() :
			state(State::Free) {};
		Element(const String &key, int val, const Hash &hash) :
			key(key),
			val(val),
			hash(hash),
			state(State::Used)
		{};
	};	

public:
	explicit FrequencyHashMap(HashFunc function) :
		mData(new Element[START_SIZE]),
		mSize(START_SIZE),
		mUsed(0),
		mHashFunction(function)
	{}
	FrequencyHashMap() :
		FrequencyHashMap(hashMeiyan)
	{}
	~FrequencyHashMap() { delete mData; }

	Element * insert(const String &key, int value)
	{
		if (loadFactor() > MAX_LOAD_FACTOR)
			reserve(mUsed * 2);
		Hash hash = getHash(key);
		Element * elem = findFirst(hash, key, true);
		assert(elem != nullptr);
		if (elem->state == Element::State::Used) {
			return nullptr;
		}
		else {
			new (elem) Element(key, value, hash);
			mUsed++;
			return elem;
		}
	}

	int& operator[](const String& key)
	{
		if (loadFactor() > MAX_LOAD_FACTOR)
			reserve(mUsed * 2);
		Hash hash = getHash(key);
		Element * elem = findFirst(hash, key, true);
		assert(elem != nullptr);
		if (elem->state != Element::State::Used) {			
			new (elem) Element(key, 0, hash);
			mUsed++;
		}
		return elem->val;
	}

	Element *find(const String& key)
	{		
		Hash hash = getHash(key);
		return findFirst(hash, key, false);
	}

	void reserve(int count)
	{
		int new_size = (int)(count * MAX_LOAD_FACTOR) + 1;
		if (new_size > size()) {
			Element * old_data = mData;
			int old_size = mSize;
			mData = new Element[new_size];
			mSize = new_size;
			for (int i = 0; i < old_size; i++) {
				if (old_data[i].state == Element::State::Used) {
					put(old_data[i]);
				}
			}
			delete old_data;
		}
	}

	int size() const
	{
		return mUsed;
	}

	float loadFactor() const
	{
		return mUsed / mSize;
	}

	void toList(WordList &list) {
		list.reserve(size());
		list.clear();
		for (int i = 0; i < mSize; i++)
			if (mData[i].state == Element::State::Used) {
				list.add(WordCount(mData[i].key, mData[i].val));
			}
	}
private:
	Element *mData;
	int mSize;
	int mUsed;
	HashFunc mHashFunction;

	Element *findFirst(const Hash& hash, const String &key, bool for_insert) const
	{		
		//int index = hash && (mSize-1);
		int index = hash % mSize;
		Element * first = nullptr;
		int i = index;
		bool found = true;
		while (i < mSize && (mData[i].state == Element::State::Deleted || mData[i].state == Element::State::Used && (mData[i].hash != hash || mData[i].key != key)))
		{
			if (!first && mData[i].state == Element::State::Deleted)
				first = mData+i;
			i++;
		}
		if (i == mSize) {
			i = 0;
			while (i < index && (mData[i].state == Element::State::Deleted || mData[i].state == Element::State::Used && (mData[i].hash != hash || mData[i].key != key)))
			{
				if (!first && mData[i].state == Element::State::Deleted)
					first = mData + i;
				i++;
			}
			if (i == index)
				return nullptr;
		}
		if (mData[i].state == Element::State::Free)
		{
			if (for_insert) {
				if (first)
					return first;
				else
					return mData + i;
			}
			else
				return nullptr;
		}
		else {
			return mData + i;
		}
	}

	void put(Element &elem)
	{
		assert(mUsed < mSize);
		int index = elem.hash % mSize;		
		while (index < mSize && mData[index].state == Element::State::Used)
			index++;
		if (index == mSize)
			index = 0;
		while (mData[index].state == Element::State::Used)
			index++;
		mData[index] = elem;
		//mUsed++;
	}

	Hash getHash(const String &key)
	{
		return mHashFunction(key.str, key.len);
	}
};

typedef FrequencyHashMap HashMap;

#pragma endregion

#pragma region CharacterTable

struct CharacterTable {
	const static int MAX_CHAR = 256;
	bool allowed[MAX_CHAR];
	int lower[MAX_CHAR];

	template <typename Predicate>
	void fill(Predicate &pr)
	{
		for (int c = 0; c < MAX_CHAR; c++) {
			lower[c] = towlower(c);
			allowed[c] = pr(c);
		}
	}
};

#pragma endregion

#pragma region Reader

class Reader {
public:
	const static size_t BUFFER_SIZE = 1024 * 1024; // 1Mb
	uchar *buffer;
	size_t data_size;

	Reader(const char *filename) :
		buffer(new uchar[BUFFER_SIZE + 1])
	{
		auto x = fopen_s(&mInput, filename, "r");
		assert(x == 0);
	};

	~Reader()
	{
		delete buffer;
		fclose(mInput);
	}

	void tailRead(size_t tail)
	{
		memcpy(buffer, buffer + data_size - tail, tail);
		data_size = tail + fread(buffer + tail, sizeof(char), BUFFER_SIZE-tail, mInput);
	}

	bool done() const
	{
		return feof(mInput) > 0;
	}

private:	
	FILE *mInput;
};

#pragma endregion

#pragma region Parser

class Parser {
public:
	const static int MAX_WORD = 1024;
public:	
	void parse(Reader& reader, CharacterTable &lookup, StringPool &strings, HashMap& map)
	{
		int size = 0;
		int tail = 0;
		while (!reader.done()) {
			reader.tailRead(tail);
			size = reader.data_size;
			uchar *p = reader.buffer;
			uchar *end = p + size;
			while (p < end) {
				while (p < end && !lookup.allowed[*p])
					p++;
				if (p == end)
					continue;
				uchar *word = p;
				uchar *word_end = __MIN(word + MAX_WORD, end);
				while (p < word_end && lookup.allowed[*p]) {
					*p = lookup.lower[*p];
					p++;
				}
				int len = p - word;
				if (len == MAX_WORD || p < end && len > 0) { 
					// Add word to the map
					String s = strings.add(reinterpret_cast<char *>(word), len);
					if (map[s]++ != 0) {
						strings.removeLast();
					}
					tail = 0;
					if (len == MAX_WORD)
						while (p < end && lookup.allowed[*p])
							p++;
				}
				else
					tail = end - word;
			}
		}
	}
};

#pragma endregion

int main(int argc, char* argv[]) 
{
	Reader r(argv[1]);
//	struct stat fileinfo;
//	stat(argv[1], &fileinfo);
	CharacterTable lookup;
	auto latin_letters = [](int c) { return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z'; };
	lookup.fill(latin_letters);
	StringPool strings;
	HashMap map;
//	map.reserve(220000);
	Parser p;
	p.parse(r, lookup, strings, map);
	WordList list;
	map.toList(list);
	std::sort(list.begin(), list.end(), WordCount::greater);
	std::ofstream output;
	output.open(argv[2]);
	for (auto it = list.begin(); it < list.end(); it++) {
		output << it->count << " " << it->word << std::endl;
	}
	return 0;
}