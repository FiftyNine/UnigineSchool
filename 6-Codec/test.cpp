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

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#pragma comment(linker, "/defaultlib:psapi.lib")
#pragma message("Automatically linking with psapi.lib")
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

#include "my.cpp"
//#include "nop.cpp"

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
	printf("%d\n", ff);
}
