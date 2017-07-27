#include <vector>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <unordered_map>
#include <stdio.h>
#include <algorithm>
#include <chrono>

using namespace std;

struct my_string_view {
	char* str;
	size_t len;
	explicit operator string() const { return string(str, str + len); }
};

inline int compare(const my_string_view& x, const my_string_view& y)
{
	size_t m = x.len < y.len ? x.len : y.len;
	int res = strncmp(x.str, y.str, m);
	if (!res) {
		int dif = y.len - x.len;
		if (dif > 0)
			res = -1;
		else if (dif < 0)
			res = 1;
	}
	return res;
}

bool operator <(const my_string_view& x, const my_string_view& y) {
	return compare(x, y) > 0;
}

bool operator >(const my_string_view& x, const my_string_view& y) {
	return compare(x, y) < 0;
}



struct cstring_equal_to : public binary_function<my_string_view, my_string_view, bool>
{
	bool operator()(const my_string_view& __x, const my_string_view& __y) const
	{
		return (__x.len == __y.len) && !strncmp(__x.str, __y.str, __x.len);
	}
};


struct cstring_hash {
	//BKDR hash algorithm
	size_t operator()(const my_string_view& str)const
	{
		return _Hash_seq((const unsigned char *)str.str, str.len);
	}
};

// For ordering
struct order_by_freq_then_lex {
	bool operator()(pair<int, my_string_view> lhs, pair<int, my_string_view> rhs) const {
		return lhs.first < rhs.first
			|| lhs.first == rhs.first && lhs.second < rhs.second;
	}
};

typedef vector<pair<string, int> > FreqList;

typedef set<pair<int, my_string_view>, order_by_freq_then_lex> OrderedSet;

class FrequencyMap : public unordered_map<my_string_view, unsigned int, cstring_hash, cstring_equal_to>
{
public:
	FreqList MakeOrderedList(int len = -1)
	{
		size_t n = len > 0 ? len : size();
		OrderedSet top;
		for (auto it = cbegin(); it != cend(); it++) {
			if (top.size() < n || it->second > top.cbegin()->first || it->second == top.cbegin()->first && it->first > top.cbegin()->second) {
				if (top.size() >= n) {
					top.erase(top.cbegin());
				}
				top.insert(OrderedSet::value_type(it->second, it->first));
			}
		}
		FreqList list;
		list.reserve(n);
		for (auto it = top.crbegin(); it != top.crend(); it++) {
			list.push_back(make_pair(static_cast<string>(it->second), it->first));
		}
		return list;
	}
};


// Just in case basic checks for input parameters
int ParseParams(int argc, char *argv[], string& inFile, string& outFile, int& count) {
	if (argc < 3) {
		throw invalid_argument("Invalid parameters. At least two arguments required: input filename and output filename");
	}
	count = -1;
	if ("-n" == string(argv[1])) {
		if (5 == argc) {
			count = stoi(argv[2]);
			inFile = argv[3];
			outFile = argv[4];
		}
		else
			throw invalid_argument("Invalid parameters");
	}
	else {
		inFile = argv[1];
		outFile = argv[2];
	}
	return 0;
}

bool domainChars[256], pathChars[256];
int total = 0;

__forceinline bool tryReadUrlC(char* line, size_t len, size_t& domain, size_t& path, size_t& end)
{
	size_t p = 4;
	end = 4;
	if (p < len && 's' == line[p])
		p++;
	if (p > len - 3 || line[p] != ':' || line[p + 1] != '/' || line[p + 2] != '/') {
		if (!line[p] || !line[p + 1] || !line[p + 2]) end = 0;
		return false;
	}
	else
		p += 3;
	domain = p;
	while (p < len && domainChars[line[p]])
		p++;
	if (p == domain) {
		if (!line[p]) end = 0;
		return false;
	}
	path = p;
	if (p >= len || '/' != line[p]) {
		if (!line[p]) {
			end = 0;
			return false;
		}
		else {
			end = p;
			return true;
		}
	}
	while (p < len && pathChars[line[p]])
		p++;
	if (!line[p]) {
		end = 0;
		return false;
	}
	else {
		end = p;
		return true;
	}
}

char* HTTP = "http";
char* EMPTY_PATH = "/";

char* parseLineC(char* line, char* lend, FrequencyMap& domains, FrequencyMap& paths)
{
	char* p = line, *f = NULL;
	size_t domPos = 0, pathPos = 0, urlEnd = 0;
	while (f = strchr(p, 'h')) {
		if (lend - f <= 4) {
			p = f;
			break;
		}
		else if (*(_Uint32t*)f == *(_Uint32t*)HTTP) {
			if (tryReadUrlC(f, lend - f, domPos, pathPos, urlEnd)) {
				my_string_view s;
				s.str = f + domPos;
				s.len = pathPos - domPos;
				domains[s]++;
				if (urlEnd > pathPos) {
					s.str = f + pathPos;
					s.len = urlEnd - pathPos;
					paths[s]++;
				}
				else {
					s.str = EMPTY_PATH;
					s.len = 1;
					paths[s]++;
				}
				total++;
			}
			p = f;
			if (!urlEnd) {
				break;
			}
			p += urlEnd;
		}
		else
			p = f + 1;
	}
	if (!f) {
		p = lend;
	}
	return p;
}

void prepareAllowedCharacters()
{
	for (char c = '0'; c <= '9'; c++) {
		domainChars[c] = true;
		pathChars[c] = true;
	}
	for (char c = 'a'; c <= 'z'; c++) {
		domainChars[c] = true;
		pathChars[c] = true;
	}
	for (char c = 'A'; c <= 'Z'; c++) {
		domainChars[c] = true;
		pathChars[c] = true;
	}
	domainChars['.'] = true;
	pathChars['.'] = true;
	domainChars['-'] = true;
	pathChars[','] = true;
	pathChars['/'] = true;
	pathChars['+'] = true;
	pathChars['_'] = true;
}

const size_t bufstep = 1024 * 1024;

int main(int argc, char *argv[]) {
	string inFile, outFile;
	int count;
	ParseParams(argc, argv, inFile, outFile, count);
	string token;
	FrequencyMap domains, paths;
	ifstream input;
	input.open(inFile, std::ios::binary);
	ofstream output;
	output.open(outFile);
	prepareAllowedCharacters();
	struct stat fileinfo;
	stat(inFile.c_str(), &fileinfo);
	size_t offset = 0;
	char* buffer = new char[fileinfo.st_size + 1]();
	bool reserved = false;
	char* p = buffer;
	while (input.good()) {
		size_t len = min(fileinfo.st_size - offset, bufstep);
		input.read(buffer + offset, len);
		p = parseLineC(p, p + len + (buffer + offset - p), domains, paths);
		if (!reserved) {
			int chunks = fileinfo.st_size / bufstep + 1;
			domains.reserve(chunks * domains.size() * 2);
			paths.reserve(chunks * paths.size() * 2);
			reserved = true;
		}
		offset += bufstep;
	}
	output << "total urls " << total << ", domains " << domains.size() << ", paths " << paths.size() << endl << endl;
	// Now make an ordered list of items
	FreqList orderDomains = domains.MakeOrderedList(count), orderPaths = paths.MakeOrderedList(count);
	// We did it boys
	output << "top domains" << endl;
	for (auto it = orderDomains.cbegin(); it != orderDomains.cend(); it++) {
		output << it->second << ' ' << it->first << endl;
	}
	output << endl;
	output << "top paths" << endl;
	for (auto it = orderPaths.cbegin(); it != orderPaths.cend(); it++) {
		output << it->second << ' ' << it->first << endl;
	}
	delete[] buffer;
	return 0;
}
