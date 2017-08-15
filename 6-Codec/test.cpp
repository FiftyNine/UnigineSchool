#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <set>

using namespace std;

///////////////////////////////////////////////////////////////////////////

typedef long long llong;
typedef unsigned int uint;
typedef unsigned char byte;

//#define __MY_DEBUG

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#pragma comment(linker, "/defaultlib:psapi.lib")
#pragma message("Automatically linking with psapi.lib")
#endif

#ifdef __MY_DEBUG
#define __DEBUG_INLINE __declspec(noinline)
#else
#define __DEBUG_INLINE __forceinline
#endif

llong microtimer()
{
#ifdef _MSC_VER
	// Windows time query
	static llong iBase = 0;
	static llong iStart = 0;
	static llong iFreq = 0;

	LARGE_INTEGER iLarge;
	if (!iBase)
	{
		// get start QPC value
		QueryPerformanceFrequency(&iLarge); iFreq = iLarge.QuadPart;
		QueryPerformanceCounter(&iLarge); iStart = iLarge.QuadPart;

		// get start UTC timestamp
		// assuming it's still approximately the same moment as iStart, give or take a msec or three
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);

		iBase = (llong(ft.dwHighDateTime)<<32) + llong(ft.dwLowDateTime);
		iBase = (iBase - 116444736000000000ULL) / 10; // rebase from 01 Jan 1601 to 01 Jan 1970, and rescale to 1 usec from 100 ns
	}

	// we can't easily drag iBase into parens because iBase*iFreq/1000000 overflows 64bit int!
	QueryPerformanceCounter(&iLarge);
	return iBase + (iLarge.QuadPart - iStart) * 1000000 / iFreq;

#else
	// UNIX time query
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return llong(tv.tv_sec) * llong(1000000) + llong(tv.tv_usec);
#endif
}

///////////////////////////////////////////////////////////////////////////

void die(const char * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("FATAL: ");
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
	exit(1);
}

void load(vector<int> &p, const char *fname)
{
	FILE *fp = fopen(fname, "rb");
	if (!fp)
		die("failed to open %s", fname);

	fseek(fp, 0, SEEK_END);
	int len = ftell(fp); // FIXME? will break on 2gb file
	fseek(fp, 0, SEEK_SET);

	int n = len / 4; // number of ints
	len = n * 4; // rounded length
	p.resize(n);

	if (fread(&p[0], 1, len, fp) != len)
		die("failed to read %s", fname);

	fclose(fp);
}


uint fnv1a(const byte *data, int len)
{
	uint hval = 2166136261UL;
	for (int i = 0; i < len; i++)
	{
		uint c = *data++;
		hval ^= c;
		hval *= 0x1000193;
    }
    return hval;
}


template<typename T>
uint fnv1a(const vector<T> &v)
{
	return fnv1a((const byte*)&v[0], (int)(v.size() * sizeof(T)));
}

//////////////////////////////////////////////////////////////////////////
// YOUR CODE HERE
//////////////////////////////////////////////////////////////////////////


// this is the representation of the data stored in the files
// "this keyword occurred in that docid at that position"
// keyword == file_name :)
struct posting
{
	int docid;
	int pos; // 8:24 field_id:in_field_pos

	bool operator<(const posting &b) const
	{
		return docid < b.docid || (docid == b.docid && pos < b.pos);
	}
};

__DEBUG_INLINE void encode_int(vector<byte> &enc, int val, int pos = -1)
{
	do {
		byte b = val & 0x7F;
		val >>= 7;
		if (val)
			b |= 0x80;
		if (pos < 0)
			enc.push_back(b);
		else {
			enc.insert(enc.begin() + pos, b);
			pos++;
		}
	} while (val > 0);
}

__DEBUG_INLINE int decode_int(const vector<byte> &enc, int index, int * val)
{
	int res = 0;
	int shift = 0;
	byte b = 0;
	int size = enc.size();
	bool more = false;
	do {
		b = enc[index];
		more = b & 0x80;
		b &= 0x7F;
		res |= b << shift;
		shift += 7;
		index++;
	} while (more && index < size);
	*val = res;
	return index;
}

__DEBUG_INLINE posting * encode_doc_postings(vector<byte> &enc, posting * post, const posting * end)
{
	int docid = post->docid;
	encode_int(enc, docid);
	int size_pos = enc.size();
	int prev_pos = 0;
	while (post < end && post->docid == docid) {
		encode_int(enc, post->pos - prev_pos);
		prev_pos = post->pos;
		post++;
	}
	int doc_size = enc.size() - size_pos;
	encode_int(enc, doc_size, size_pos);
	return post;
}

__DEBUG_INLINE int decode_doc_postings(vector<int> &dec, const vector<byte> &enc, int index)
{
	int docid = 0;
	int doc_size = 0;
	index = decode_int(enc, index, &docid);
	index = decode_int(enc, index, &doc_size);	
	int end = index + doc_size;
	int pos = 0, prev_pos = 0;
	while (index < end) {
		dec.push_back(docid);
		index = decode_int(enc, index, &pos);
		pos += prev_pos;
		dec.push_back(pos);
		prev_pos = pos;
	}
	return index;
}

__DEBUG_INLINE int read_doc(const vector<byte> &enc, int index, int * docid)
{
	int doc_size = 0;
	index = decode_int(enc, index, docid);
	index = decode_int(enc, index, &doc_size);
	return index+doc_size;
}

// takes raw input, emits encoded output, in your very own cool format
void encode(vector<byte> &enc, const vector<int> &raw)
{
	posting * p = (posting *)&raw[0];
	posting * e = p+raw.size()*sizeof(int)/sizeof(posting);
	int lookup_bits = 14;	
	int lookup_size = 1 << lookup_bits;
	int maxbit = 0;
	int temp = (e - 1)->docid;
	while (temp > 0) {
		maxbit++;
		temp >>= 1;
	}
	if (maxbit < lookup_bits)
		maxbit = lookup_bits;
	int * lookup = new int[lookup_size]{};
	int last_hash = -1;	
	posting prev{};
	enc.resize(2 + lookup_size*sizeof(int));
	while (p < e) {
		int hash = p->docid >> maxbit - lookup_bits;
		if (last_hash < 0 || hash != last_hash) {
			lookup[hash] = enc.size();
			last_hash = hash;
		}
		p = encode_doc_postings(enc, p, e);
	}
	enc[0] = lookup_bits;
	enc[1] = maxbit;
	memcpy(&enc[2], &lookup[0], lookup_size*sizeof(int));
	delete lookup;
}

// takes encoded input, in your very own cool format, emits raw output
// for verification only, basically
void decode(vector<int> &dec, const vector<byte> &enc)
{	
	int lookup_size = 1 << enc[0];
	int index = lookup_size*sizeof(int) + 2;
	while (index < enc.size()) {
		index = decode_doc_postings(dec, enc, index);
	}
}

struct compare_id
{
	bool operator()(const posting &a, int b) const
	{
		return a.docid < b;
	}
};

int ff = 0;
int ffdp = 0;

int find_first(const vector<byte> &enc, int id)
{
	ff++;
	int hash = id >> (enc[1] - enc[0]);
	int i1 = *(((int*)&enc[2]) + hash);
	int i2 = i1;
	int size = enc.size();
	int docid = 0;
	while (i1 < size && docid < id) {
		i2 = read_doc(enc, i1, &docid);
		ffdp++;
		if (docid == id)
			return i1;
		i1 = i2;
	}
	return -1;
}

void extract_by_doc(vector<int> &res, const vector<byte> &enc, int id)
{	
	int i = find_first(enc, id);
	if (i >= 0) {
		decode_doc_postings(res, enc, i);
	}
}

// fetches all postings (for both keywords) that match the given id
// output postings are mixed together, and returned in the ascending in-document position order
// "which keyword was in that position" data is intentionally lost (in reality, it would not be)
void lookup(vector<int> &res, const vector<byte> &enc1, const vector<byte> &enc2, int id)
{
	int from = (int)res.size();
	extract_by_doc(res, enc1, id);
	extract_by_doc(res, enc2, id);
	if (from == res.size())
		return;
	sort((posting*)&res[from], (posting*)(&res[0] + res.size()));
}

// intersects both keywords
// returns a list of document ids that match both keywords
void match(vector<int> &res, const vector<byte> &enc1, const vector<byte> &enc2)
{	
	int last = -1;
	int docid1 = 0;
	int docid2 = 0;
	int p1 = 2 + (1 << enc1[0]) * sizeof(int);
	int p2 = 2 + (1 << enc2[0]) * sizeof(int);
	while (p1 < enc1.size() && p2 < enc2.size()) {
		if (docid1 == docid2) {
			p1 = read_doc(enc1, p1, &docid1);
			p2 = read_doc(enc2, p2, &docid2);
		}
		else if (docid1 < docid2) 
			p1 = read_doc(enc1, p1, &docid1);
		else
			p2 = read_doc(enc2, p2, &docid2);
		if (docid1 == docid2 && docid1 != last) {
			res.push_back(docid1);
			last = docid1;
		}
	} 
}


//////////////////////////////////////////////////////////////////////////

template<typename T>
int bytes(const vector<T> &v)
{
	return (int)(sizeof(T) * v.size());
}


void print_deltas(const vector<int> &raw, const char * filename)
{
	unordered_map<int, int> delta;

	for (int i = 2; i < raw.size(); i += 2)
		delta[raw[i] - raw[i - 2]]++;

	FILE * out = nullptr;

	fopen_s(&out, filename, "w");

	vector<pair<int, int>> counts;
	counts.reserve(delta.size());

	for (auto it = delta.begin(); it != delta.end(); it++)
		counts.push_back(make_pair(it->first, it->second));

	sort(counts.begin(), counts.end(), [](const pair<int, int> & a, const pair<int, int> & b) {return a.first < b.first; });
	for (auto &p: counts) 
		fprintf(out, "%d %d\n", p.first, p.second);

	fclose(out);
}

void save_truncated(const vector<int> &data, int count, const char * filename)
{
	FILE * out = nullptr;
	fopen_s(&out, filename, "wb+");
	fwrite(&data[0], sizeof(posting), count, out);
	fclose(out);
}


int main(int, char **)
{
	vector<int> raw1, raw2, bench;
	load(raw1, "the.bin");
	load(raw2, "i.bin");
	load(bench, "bench.bin");
	/*
	set<int> ids;
	for (int i = 0; i < raw1.size(); i += 2)
		ids.insert(raw1[i]);
	printf("%d\n", ids.size());
	
	FILE *fp1;
	fp1 = fopen("bench_small.bin", "wb+");
	for (int i = 0; i < raw1.size() && i < raw2.size(); i += 100) {
		fwrite(&raw1[i], sizeof(int), 1, fp1);
		fwrite(&raw2[i], sizeof(int), 1, fp1);
	}
	fclose(fp1);
	return 0;



	print_deltas(raw1, "the.delta");
	print_deltas(raw2, "i.delta");

	return 0;

	save_truncated(raw1, 10000, "the_small.bin");
	save_truncated(raw2, 10000, "i_small.bin");

	return 0;
	*/
	vector<byte> enc1, enc2;
	llong tm1 = microtimer();
	encode(enc1, raw1);
	encode(enc2, raw2);
	tm1 = (microtimer() - tm1) / 1000;

	vector<int> dec1, dec2;
	llong tm2 = microtimer();
	decode(dec1, enc1);
	decode(dec2, enc2);
	tm2 = (microtimer() - tm2) / 1000;	

	llong tm3 = microtimer();
	vector<int> lookups;
	for (int id : bench)
		lookup(lookups, enc1, enc2, id);
	tm3 = (microtimer() - tm3) / 1000;

	llong tm4 = microtimer();
	vector<int> matches;
	match(matches, enc1, enc2);
	tm4 = (microtimer() - tm4) / 1000;

	printf("size, %d\n", bytes(enc1) + bytes(enc2));
	printf("ratio, %.03f\n", double(bytes(enc1) + bytes(enc2)) / (bytes(raw1) + bytes(raw2)));
	printf("encode, %d ms\n", tm1);
	printf("decode, %d ms\n", tm2);
	printf("randread, %d ms\n", tm3); // !
	printf("match, %d ms\n", tm4); // !!!
	FILE *fp;
	fp = fopen("randread.res", "wb+");
	fwrite(&lookups[0], 1, bytes(lookups), fp);
	fclose(fp);
	fp = fopen("match.res", "wb+");
	fwrite(&matches[0], 1, bytes(matches), fp);
	fclose(fp);
	
	printf("raw %08x, %08x\n", fnv1a(raw1), fnv1a(raw2));
	printf("enc %08x, %08x\n", fnv1a(enc1), fnv1a(enc2)); // the only crcs that should change
	printf("dec %08x, %08x\n", fnv1a(dec1), fnv1a(dec2));
	printf("lookup %08x\n", fnv1a(lookups));
	printf("match %08x, %d results\n", fnv1a(matches), matches.size());
	//printf("%d, %d, %.3f\n", ffdp, ff, (float)ffdp/ff);
}
