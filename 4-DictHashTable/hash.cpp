//#include <iostream>
#include <sys/stat.h>
#include <fstream>
#include <algorithm>
#include <stdio.h>
#include <unordered_map>
#include <set>

#define __MIN(a, b) (a < b ? a : b)
#define __STRCMP(a, b, len) strncmp(a, b, len)

typedef unsigned char uchar;
typedef unsigned int uint;

using namespace std;

#pragma region String

inline int fast_compare(const char *ptr0, const char *ptr1, int len) {
	int fast = len / sizeof(size_t) + 1;
	int offset = (fast - 1)*sizeof(size_t);
	int current_block = 0;

	if (len <= sizeof(size_t)) { fast = 0; }


	size_t *lptr0 = (size_t*)ptr0;
	size_t *lptr1 = (size_t*)ptr1;

	while (current_block < fast) {
		if ((lptr0[current_block] ^ lptr1[current_block])) {
			int pos;
			for (pos = current_block*sizeof(size_t); pos < len; ++pos) {
				if ((ptr0[pos] ^ ptr1[pos]) || (ptr0[pos] == 0) || (ptr1[pos] == 0)) {
					return  (int)((unsigned char)ptr0[pos] - (unsigned char)ptr1[pos]);
				}
			}
		}

		++current_block;
	}

	while (len > offset) {
		if ((ptr0[offset] ^ ptr1[offset])) {
			return (int)((unsigned char)ptr0[offset] - (unsigned char)ptr1[offset]);
		}
		++offset;
	}


	return 0;
}


struct String {
	char *str{ nullptr };
	size_t len{ 0 };
	String() {};
	explicit String(char * const data, size_t size) :
		str(data),
		len(size)
	{}
/*	String(String &&that) :
		str(std::move(that.str)),
		len(std::move(that.len))
	{}*/
public:
	struct EqualTo : public std::binary_function<String, String, bool>
	{
		bool operator()(const String& __x, const String& __y) const
		{
			return (__x.len == __y.len) && !__STRCMP(__x.str, __y.str, __x.len);
		}
	};


	struct Hash {
		//BKDR hash algorithm
		size_t operator()(const String& str)const
		{
			return std::_Hash_seq((const unsigned char *)str.str, str.len);
		}
	};
};

inline int compare(const String& x, const String& y)
{
	size_t m = __MIN(x.len, y.len);
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

bool operator <(const String& x, const String& y) {
	return compare(x, y) > 0;
}

bool operator !=(const String& x, const String& y) {
	return compare(x, y) != 0;
}

std::ostream& operator<<(std::ostream& os, const String& str)
{
	os.write(str.str, str.len);
	return os;
}

#pragma endregion

/// Vector

/////////////!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//////////////////
#include "..\2-MyVector\myvector.h"

#pragma region StringPool

class StringPool {
public:	
	String add(char *str, size_t len) 
	{
		assert(len < Pool::SIZE);
		if (pools.size() == 0 || Pool::SIZE - pools.back().used < len)
			pools.add();
		return pools.back().add(str, len);
	}
private:		
	struct Pool {
		const static size_t SIZE = 1024 * 128; // 128 Kb
		char *data{ nullptr };
		size_t used{ 0 };
		String add(char *str, size_t len)
		{
			memcpy(data + used, str, len);
			String res(data + used, len);
			used += len;
			return res;
		}

		Pool() : data(new char[SIZE]) {};
		Pool(Pool &&that) :
			data(std::move(that.data)),
			used(std::move(that.used))
		{}
		~Pool() { delete data; }
	};
	myvector<Pool> pools;
};

#pragma endregion

/// HashMap



// For ordering
bool order_by_freq_then_lex(const pair<String, int>& lhs, const pair<String, int>& rhs) {
		return lhs.second < rhs.second
			|| lhs.second == rhs.second && lhs.first < rhs.first;
	}

typedef myvector<pair<String, int> > FreqList;

class FrequencyHashMap {
public:
	#define MAX_LOAD_FACTOR 0.7
	typedef size_t Hash;
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
	Element * insert(const String &key, int value)
	{
		if (load_factor() > MAX_LOAD_FACTOR)
			reserve(mUsed * 2);
		Hash hash = get_hash(key);
		Element * elem = find_first(hash, key, true);
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
		if (load_factor() > MAX_LOAD_FACTOR)
			reserve(mUsed * 2);
		Hash hash = get_hash(key);
		Element * elem = find_first(hash, key, true);
		assert(elem != nullptr);
		if (elem->state != Element::State::Used) {			
			new (elem) Element(key, 0, hash);
			mUsed++;
		}
		return elem->val;
	}

	Element *find(const String& key)
	{		
		Hash hash = get_hash(key);
		return find_first(hash, key, false);
	}

	void reserve(size_t count)
	{
		size_t new_size = (int)(count * MAX_LOAD_FACTOR) + 1;
		if (new_size > size()) {
			Element * old_data = data;
			int old_size = mSize;
			data = new Element[new_size];
			mSize = new_size;
			for (int i = 0; i < old_size; i++) {
				if (old_data[i].state == Element::State::Used) {
					put(old_data[i]);
				}
			}
			delete old_data;
		}
	}

	size_t size() const
	{
		return mUsed;
	}

	float load_factor() const
	{
		return mUsed / mSize;
	}

	void to_list(FreqList &list) {
		list.reserve(size());
		list.clear();
		for (int i = 0; i < mSize; i++)
			if (data[i].state == Element::State::Used) {
				list.push_back(make_pair(data[i].key, data[i].val));
			}
	}
private:
	Element *data{ nullptr };
	int mSize{ 0 };
	int mUsed{ 0 };
	Element *find_first(const Hash& hash, const String &key, bool for_insert) const
	{		
		int index = hash % mSize;
		Element * first = nullptr;
		int i = index;
		bool found = true;
		while (i < mSize && (data[i].state == Element::State::Deleted || data[i].state == Element::State::Used && data[i].hash != hash && data[i].key != key))
		{
			if (!first && data[i].state == Element::State::Deleted)
				first = data+i;
			i++;
		}
		if (i == mSize) {
			i = 0;
			while (i < index && (data[i].state == Element::State::Deleted || data[i].state == Element::State::Used && data[i].hash != hash && data[i].key != key))
			{
				if (!first && data[i].state == Element::State::Deleted)
					first = data + i;
				i++;
			}
			if (i == index)
				return nullptr;
		}
		if (data[i].state == Element::State::Free)
		{
			if (for_insert) {
				if (first)
					return first;
				else
					return data + i;
			}
			else
				return nullptr;
		}
		else {
			return data + i;
		}
	}
	void put(Element &elem)
	{
		assert(mUsed < mSize);
		int index = elem.hash % mSize;		
		while (index < mSize && data[index].state == Element::State::Used)
			index++;
		if (index == mSize)
			index = 0;
		while (data[index].state == Element::State::Used)
			index++;
		data[index] = elem;
		mUsed++;
	}
	Hash get_hash(const String &key)
	{
		return _Hash_seq((const unsigned char *)key.str, key.len);
	}
};

typedef FrequencyHashMap HashMap;

// class HashMap : public unordered_map<String, int, String::Hash, String::EqualTo>

FreqList MakeOrderedList(HashMap map)
{
	FreqList list;
	map.to_list(list);
	sort(list.begin(), list.end(), order_by_freq_then_lex);
	return list;
}


#pragma region CharacterTable

struct CharacterTable {
	const static int MAX_CHAR = 256;
	bool allowed[MAX_CHAR];
	int lower[MAX_CHAR];
	CharacterTable() {
		for (int c = 0; c < MAX_CHAR; c++) {
			lower[c] = towlower(c);
			allowed[c] = (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z');
		}
	}
};

#pragma endregion

#pragma region Reader

class Reader {
public:
	const static int BUFFER_SIZE = 1024 * 1024; // 1Mb
	uchar *buffer;
	size_t data_size;

	void tail_read(size_t tail)
	{
		memcpy(buffer, buffer + data_size - tail, tail);
		data_size = tail + fread(buffer + tail, sizeof(char), BUFFER_SIZE-tail, input);
	}

	bool done() const
	{
		return feof(input) > 0;
	}

	Reader(const char *filename) :
		buffer(new uchar[BUFFER_SIZE + 1])
	{
		auto x = fopen_s(&input, filename, "r");
		assert(x == 0);
	};

	~Reader()
	{
		delete buffer;
		fclose(input);
	}
private:	
	FILE *input;
};

#pragma endregion

#pragma region Parser

class Parser {
public:
	const static size_t MAX_WORD = 1024;
public:	
	void parse(Reader& reader, CharacterTable &lookup, StringPool &strings, HashMap& map)
	{
		size_t size = 0;
		size_t tail = 0;
		while (!reader.done()) {
			reader.tail_read(tail);
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
				size_t len = p - word;
				if (len == MAX_WORD || p < end && len > 0) { 
					// Add word to map
					String s = strings.add(reinterpret_cast<char *>(word), len);
					map[s]++;
					tail = 0;
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
	struct stat fileinfo;
	stat(argv[1], &fileinfo);
	CharacterTable lookup;
	StringPool strings;
	HashMap map;
	map.reserve(220000);
	Parser p;
	p.parse(r, lookup, strings, map);
	FreqList ordered = MakeOrderedList(map);

	std::ofstream output;
	output.open(argv[2]);

	for (auto it = ordered.end()-1; it >= ordered.begin(); it--) {
		output << it->second << " " << it->first << std::endl;
	}
	return 0;
}