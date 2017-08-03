#include <algorithm>
#include <type_traits>
#include <cassert>
#include <cstdio>
#include <sys/stat.h>

#define __MIN(a, b) (a < b ? a : b)
#define __STRCMP(a, b, len) strncmp(a, b, len) // For testing different comparison functions
#define __MAX_LOAD_FACTOR 0.6 // rehash when hashmap's LF goes over this value. At 0.6 performance is starting to drop noticeably
#define __INLINE __forceinline

typedef unsigned char UCHAR;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef UINT (*HashFunc)(const char*, size_t);

#pragma region Hash functions

// MeiYan is the fastest function (out of ~20 benchmarked) for our set of keys
// Meiyan - 9103.44 ms, std::_Hash - 11394.6 ms, CRC-32 - 11085.9 ms, Murmur3 - 9785.24 ms
__INLINE UINT hashMeiyan(const char *str, size_t len)
{
	const UINT PRIME = 709607;
	UINT hash32 = 2166136261;
	const char *p = str;

	for (; len >= 2 * sizeof(DWORD); len -= 2 * sizeof(DWORD), p += 2 * sizeof(DWORD)) {
		hash32 = (hash32 ^ (_rotl(*(DWORD *)p, 5) ^ *(DWORD *)(p + 4))) * PRIME;
	}
	// Cases: 0,1,2,3,4,5,6,7
	if (len & sizeof(DWORD)) {
		hash32 = (hash32 ^ *(WORD*)p) * PRIME;
		p += sizeof(WORD);
		hash32 = (hash32 ^ *(WORD*)p) * PRIME;
		p += sizeof(WORD);
	}
	if (len & sizeof(WORD)) {
		hash32 = (hash32 ^ *(WORD*)p) * PRIME;
		p += sizeof(WORD);
	}
	if (len & 1)
		hash32 = (hash32 ^ *p) * PRIME;

	return hash32 ^ (hash32 >> 16);
}

#pragma endregion

#pragma region String: Pascal-style string (StringView), stores a pointer to characters and own length

struct String {
	char *str{ nullptr }; // Does't own data, just stores a pointer to it
	int len{ 0 };
	String() {}; 
	explicit String(char * const data, int size) :
		str(data),
		len(size)
	{}
};

// x < y is -1, x == y is 0, x > y is 1
__INLINE int compare(const String& x, const String& y)
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

__INLINE bool operator >(const String& x, const String& y) {
	return compare(x, y) < 0;
}

__INLINE bool operator !=(const String& x, const String& y) {
	return compare(x, y) != 0;
}

#pragma endregion

#pragma region Vector: General purpose scalable storage

template<typename T>
class Vector {
public:
	static const int GROWTH_FACTOR = 4;
	static const int DEFAULT_CAPACITY = 8;
private:
	T * mData{ nullptr };
	int mSize{ 0 };
	int mCapacity{ 0 };
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
		mData = getMemory(DEFAULT_CAPACITY);
		mCapacity = DEFAULT_CAPACITY;
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
			else { // Don't forget to destroy extra objects
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
			// Allocate a new chunk of memory and move data into it
			T * new_data = getMemory(min_capacity);
			// If data can be trivially moved, it will be taken care of by realloc
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

#pragma region WordCount: String + Number of occurences

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

	// Compares counts first, if equal - compares strings lexicographically
	static bool greater(const WordCount& lhs, const WordCount& rhs) {
		return lhs.count > rhs.count
			|| lhs.count == rhs.count && lhs.word > rhs.word;
	}
};

typedef Vector<WordCount> WordList;

#pragma endregion

#pragma region StringPool: Memory pool for storing string data

class StringPool {
public:	
	StringPool()	
	{
		mBlocks.add();
	}
	// Create a new String and store data in the pool
	String add(char *str, int len) 
	{
		assert(len < Block::SIZE);
		if (Block::SIZE - mBlocks.back().used < len)
			mBlocks.add(); // If current block doesn't have enough space, get a new one
		mLast = len;
		return mBlocks.back().add(str, len);
	}
	// Remove the newest string from the pool
	void rollback()
	{
		mBlocks.back().used -= mLast;
		mLast = 0;
	}
private:		
	struct Block {
		const static int SIZE = 1024 * 128;
		char *data{ nullptr };
		int used{ 0 };
		String add(char *str, int len)
		{
			memcpy(data + used, str, len); // We own the data now
			String res(data + used, len);
			used += len;
			return res;
		}
		Block() : 
			data(new char[SIZE])
		{};
		Block(Block &&that) :
			data(std::move(that.data)),
			used(std::move(that.used))
		{}
		~Block() 
		{ 
			delete data; 
		}
	};
private:
	Vector<Block> mBlocks; // Allocated blocks of memory
	int mLast{ 0 }; // Length of the last inserted string
};

#pragma endregion

#pragma region HashMap: One purpose hash map for counting instances of unique strings

class FrequencyHashMap {
public:	
	const static size_t START_SIZE = 16; // Must be a power of 2!
	explicit FrequencyHashMap(HashFunc function) :
		mData(new Element[START_SIZE]),
		mCapacity(START_SIZE),
		mUsed(0),
		mLoadFactor(0.0),
		mHashFunction(function)
	{}
	FrequencyHashMap() :
		FrequencyHashMap(hashMeiyan)
	{}
	~FrequencyHashMap() { 
		delete mData; 
	}

	int& operator[](const String& key)
	{
		if (mLoadFactor > __MAX_LOAD_FACTOR) // Check this before looking for an element, or rehashing can invalidate pointer
			reserve(mUsed * 2); // Growth factor of 2 seems reasonable
		Hash hash = getHash(key);
		Element * elem = findFirst(hash, key);
		assert(elem != nullptr);
		// Initialize element the first time it's accessed		
		if (!elem->used) {
			elem->hash = hash;
			elem->key = key;
			elem->val = 0;
			elem->used = true;
			mUsed++;
			mLoadFactor = (float)mUsed / mCapacity;
		}
		return elem->val;
	}

	void reserve(size_t count)
	{
		// Make sure table's size is always a power of 2 for faster modulos
		size_t new_size = nextPowerOf2((size_t)(count / __MAX_LOAD_FACTOR));
		if (new_size > size()) {
			Element * old_data = mData;
			size_t old_size = mCapacity;
			mData = new Element[new_size];
			mCapacity = new_size;
			// Move old data to the new place
			for (size_t i = 0; i < old_size; i++) {
				if (old_data[i].used) {
					reinsert(old_data[i]); // Keep in mind that collisions can occur
				}
			}
			mLoadFactor = (float)mUsed / mCapacity;
			delete old_data;
		}
	}

	size_t size() const
	{
		return mUsed;
	}

	float loadFactor() const
	{
		return mLoadFactor;
	}

	void toList(WordList &list) const {
		list.reserve(size());
		list.clear();
		for (int i = 0; i < mCapacity; i++)
			if (mData[i].used) {
				list.add(WordCount(mData[i].key, mData[i].val));
			}
	}

private:
	typedef UINT Hash;
	struct Element {
		Hash hash;
		int val;
		String key;
		bool used;
		Element() :
			used(false)
		{};
	};
private:
	Element *mData;
	size_t mCapacity;
	int mUsed;
	float mLoadFactor; // Optimization: store load factor and recalculate on insertions
	HashFunc mHashFunction;

	Element *findFirst(const Hash& hash, const String &key) const
	{		
		int index = hash & (mCapacity-1); // hash % mCapacity
		int i = index;
		// Look until find a matching or unused element and mind the end of an array 
		while (i < mCapacity && mData[i].used && (mData[i].hash != hash || mData[i].key != key))
			i++;		
		if (i == mCapacity) {
			// Reset to start and keep looking until we hit the full cycle. Faster than doing one cycle with i % mCapacity
			i = 0;
			while (i < index && mData[i].used && (mData[i].hash != hash || mData[i].key != key))
				i++;
			assert(i != index);
		}
		return mData + i;
	}

	void reinsert(Element &elem)
	{
		assert(mUsed < mCapacity);
		int index = elem.hash & (mCapacity - 1); // hash % mCapacity
		// Avoiding collision on reinsert
		while (index < mCapacity && mData[index].used)
			index++;
		if (index == mCapacity)
			index = 0;
		while (mData[index].used)
			index++;
		mData[index] = elem;
	}

	Hash getHash(const String &key) const
	{
		return mHashFunction(key.str, key.len);
	}
	
	size_t nextPowerOf2(size_t val)
	{
		// This doesn't affect overall performance, hence no bithacks
		int power = 0;
		bool round = true;
		size_t res = val;
		while (res > 1) {
			if (res && 1)
				round = false;
			res >>= 1;
			power++;
		}
		if (!round)
			power++;
		return 1ULL << power;
	}

};

typedef FrequencyHashMap HashMap;

#pragma endregion

#pragma region CharacterTable: Lookup table for determining valid characters and lowercase conversion

struct CharacterTable {
	const static int MAX_CHAR = 256;
	bool valid[MAX_CHAR]; // lookup table of valid (word) characters
	int lower[MAX_CHAR]; // lookup table for converting to lowercase characters
	
	// Prepare table for use
	template <typename Predicate>
	void fill(Predicate &pr)
	{
		for (int c = 0; c < MAX_CHAR; c++) {
			valid[c] = pr(c);
			if (valid[c])
				lower[c] = towlower(c);			
		}
	}
};

#pragma endregion

#pragma region Reader: Simple sequential reads from file

class Reader {
public:	
	// Move tail bytes to the start of mBuffer and read the rest of it from file
	void tailRead(size_t tail)
	{
		memcpy(mBuffer, mBuffer + mDataSize - tail, tail);
		mDataSize = tail + fread(mBuffer + tail, sizeof(char), BUFFER_SIZE-tail, mInput);
	}

	bool done() const
	{
		return feof(mInput) > 0;
	}

	UCHAR *buffer() const
	{
		return mBuffer;
	}

	size_t dataSize() const
	{
		return mDataSize;
	}

	Reader(const char *filename) :
		mBuffer(new UCHAR[BUFFER_SIZE + 1])
	{
		fopen_s(&mInput, filename, "r");
	};

	~Reader()
	{
		delete mBuffer;
		fclose(mInput);
	}
private:
	const static size_t BUFFER_SIZE = 1024 * 1024;
	FILE *mInput;
	UCHAR *mBuffer;
	size_t mDataSize{ 0 };
};

#pragma endregion

#pragma region Parser: Read file, parse words from it, store them in memory and count using supplied hashmap

class Parser {
public:
	const static int MAX_WORD = 1024;
public:	
	void parse(Reader& reader, CharacterTable &lookup, StringPool &strings, HashMap& map)
	{
		// Unparsed word-characters at the end of the buffer, potentially part of a word
		int tail = 0;
		while (!reader.done()) {
			reader.tailRead(tail); // Move unparsed tail to the start and read more data
			UCHAR *p = reader.buffer();
			UCHAR *end = p + reader.dataSize();
			while (p < end) {
				// Skip non-word characters
				while (p < end && !lookup.valid[*p]) {
					p++;
				}
				if (p == end)
					continue;
				UCHAR *word = p;
				UCHAR *word_end = __MIN(word + MAX_WORD, end); // Make sure that we won't go beyond buffer
				while (p < word_end && lookup.valid[*p]) { 
					*p = lookup.lower[*p];
					//*p |= 0x20; // Not worth it
					p++;
				}
				int len = p - word;
				// Current word may be split in two by the end of the buffer, so remember to parse it on next read
				if (p == end && len < MAX_WORD) {
					tail = end - word;
				} else {
					// Make a new string in the pool, so map would store a pointer to a permanent memory and not temporary buffer
					String s = strings.add(reinterpret_cast<char *>(word), len);
					if (map[s]++ != 0) {
						// This word was already in map, so we don't need to store it in pool twice
						strings.rollback();
					}
					tail = 0; // Whatever unparsed character there were, we've taken care of them
					if (len == MAX_WORD) // If we've hit length limit, cur word and skip following character
						while (p < end && lookup.valid[*p])
							p++;
				}					
			}
		}
	}
};

#pragma endregion

int main(int argc, char* argv[]) 
{
	//struct stat fileinfo;
	//stat(argv[1], &fileinfo);
	Reader r(argv[1]);	
	CharacterTable lookup;
	auto latin_letters = [](int c) { return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z'; };
	lookup.fill(latin_letters);
	StringPool strings;
	HashMap map;
	// Rough estimate is at least 500 unique words per Mb on sufficiently large files (500Mb+)	
	//map.reserve(fileinfo.st_size/(1024*1024)*500);
	Parser p;
	p.parse(r, lookup, strings, map);
	WordList list;
	map.toList(list);
	// Sort list in descending order
	std::sort(list.begin(), list.end(), WordCount::greater);
	FILE *output;
	fopen_s(&output, argv[2], "w");
	for (auto it = list.begin(); it < list.end(); it++) {
		fprintf(output, "%d %.*s\n", it->count, it->word.len, it->word.str);
	}
	fclose(output);
	return 0;
}