#include <cassert>

//#define __MY_DEBUG

#ifdef __MY_DEBUG
#define __DEBUG_INLINE __declspec(noinline)
#else
#define __DEBUG_INLINE __forceinline
#endif

int lirp(int a, int b, int start, int end, int val)
{
	return a + (int)floor((b - a)*((float)(val - start) / (end - start)));
}

class LookupTable {
public:
	LookupTable(byte * data):
		mData(data),
		mOwner(false)
	{}

	LookupTable(byte hash_bits, byte offset_bits) :
		mData(new byte[1 << hash_bits + 2]),
		mOwner(true)
	{
		mData[0] = hash_bits;
		mData[1] = offset_bits;
	}

	~LookupTable()
	{
		if (mOwner)
			delete mData;
	}

	int count()
	{
		return 1 << mData[0];
	}

	int byteSize()
	{
		return 2 + count()*sizeof(int);
	}

	int getPosition(int docid)
	{
		int * table = (int *)(mData + 2);
		int hash = docid >> mData[1];
		if (hash >= 0 && hash < count())
			return table[hash];
		else
			return -1;			
	}

	int getNextPosition(int docid)
	{
		int * table = (int *)(mData + 2);
		int hash = docid >> mData[1];
		if (hash >= 0 && hash < count()) {
			do {
				hash++;
			} while (hash < count() && !table[hash]);
			if (hash < count())
				return table[hash];
		}
		return -1;
	}

	void setPosition(int docid, int pos)
	{
		int * table = (int *)(mData + 2);
		int hash = docid >> mData[1];
		assert(hash >= 0 && hash < count());
		table[hash] = pos;
	}

	void dump(byte * dest)
	{
		memcpy(dest, mData, byteSize());
	}
private:
	byte * mData;
	bool mOwner;
};


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
	if (!val) {
		if (pos < 0) {
			enc.push_back(0x80);
			enc.push_back(0);
		}
		else {
			enc.insert(enc.begin() + pos, 0x80);
			enc.insert(enc.begin() + pos + 1, 0);
		}
		return;
	}
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
		more = (b & 0x80) != 0;
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
	int prev_pos = post->pos;	
	int field_id = prev_pos >> 24;
	int pos = prev_pos & ((1 << 24) - 1);
	encode_int(enc, field_id);
	encode_int(enc, pos);
	post++;
	while (post < end && post->docid == docid) {
		encode_int(enc, post->pos - prev_pos);
		prev_pos = post->pos;
		post++;
	}
	int doc_size = enc.size() - size_pos;
	encode_int(enc, doc_size, size_pos);
	enc.push_back(0);
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
	int field_id = 0;	
	index = decode_int(enc, index, &field_id);
	index = decode_int(enc, index, &pos);
	prev_pos = pos | (field_id << 24);
	dec.push_back(docid);
	dec.push_back(prev_pos);

	while (index < end) {
		dec.push_back(docid);
		index = decode_int(enc, index, &pos);
		pos += prev_pos;
		dec.push_back(pos);
		prev_pos = pos;
	}
	return index+1;
}

__DEBUG_INLINE int read_doc(const vector<byte> &enc, int index, int * docid)
{
	int doc_size = 0;
	index = decode_int(enc, index, docid);
	index = decode_int(enc, index, &doc_size);
	return index+doc_size+1;
}

// takes raw input, emits encoded output, in your very own cool format
void encode(vector<byte> &enc, const vector<int> &raw)
{
	posting * p = (posting *)&raw[0];
	posting * e = p+raw.size()*sizeof(int)/sizeof(posting);
	byte lookup_bits = 18; // 
	unsigned long highest = 0;
	_BitScanReverse(&highest, (e - 1)->docid);
	highest = __max(highest + 1, lookup_bits);
	LookupTable lookup(lookup_bits, highest - lookup_bits);
	enc.resize(lookup.byteSize());
	while (p < e) {
		if (lookup.getPosition(p->docid) == 0)
			lookup.setPosition(p->docid, enc.size());
		p = encode_doc_postings(enc, p, e);
	}
	lookup.dump(&enc[0]);
}

// takes encoded input, in your very own cool format, emits raw output
// for verification only, basically
void decode(vector<int> &dec, const vector<byte> &enc)
{	
	LookupTable lookup((byte *)&enc[0]);
	int index = lookup.byteSize();
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

int find_first(const vector<byte> &enc, int id)
{
	LookupTable lookup((byte *)&enc[0]);
	int docid = 0;
	int i1 = lookup.getPosition(id);
	if (i1 <= 0)
		return -1;
	int i2 = read_doc(enc, i1, &docid);
	ff++;
	if (docid == id)
		return i1;
	i1 = i2;
/*
	int next_pos = lookup.getNextPosition(id);
	if (next_pos >= 0) {
		int next_docid = docid;
		read_doc(enc, next_pos, &next_docid);
		ff++;
		//decode_int(enc, next_pos, &next_docid);
		next_pos = lirp(i1, next_pos, docid, next_docid, id); 
		while (next_pos >= i1) {
			while (enc[next_pos] != 0 && next_pos >= i1)
				next_pos--;
			if (next_pos >= i1 && enc[next_pos-1] != 0x80) {
				read_doc(enc, next_pos+1, &next_docid);
				ff++;
				//decode_int(enc, next_pos+1, &next_docid);
				if (next_docid == id)
					return next_pos+1;
				else if (next_docid < id) {
					docid = next_docid;
					i1 = next_pos+1;					
					break;
				}
				else
					next_pos--;
			}
			else
				next_pos--;
		}
	}
*/
	int size = enc.size();	
	while (i1 < size && docid < id) {
		i2 = read_doc(enc, i1, &docid);
		ff++;
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
	int docid1 = -1;
	int docid2 = -1;
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
