#include <cassert>

//#define __MY_DEBUG

#ifdef __MY_DEBUG
#define __DEBUG_INLINE __declspec(noinline)
#else
#define __DEBUG_INLINE __forceinline
#endif

// Linear interpolation, returns value between "a" and "b", 
// located on the same ratio as "val" is located between "start" and "end"
int lirp(int a, int b, int start, int end, int val)
{
	return a + (int)floor((b - a)*((float)(val - start) / (end - start)));
}

// Basic hash table to speed up random access to encoded data
class LookupTable {
private:
	const static int HEADER = 6;
public:
	// Create on existing data
	explicit LookupTable(byte * data):
		mData(data),
		mOwner(false)
	{}

	// Allocate memory and create a table on it
	explicit LookupTable(byte hash_bits, byte offset_bits, int min_id) :
		mData(new byte[(1 << hash_bits)*sizeof(int) + HEADER]()),
		mOwner(true)
	{
		mData[0] = hash_bits;
		mData[1] = offset_bits;
		memcpy(mData + 2, &min_id, sizeof(min_id));
	}

	~LookupTable()
	{
		if (mOwner)
			delete mData;
	}

	// Number of elements in the table
	int count()
	{
		return 1 << mData[0];
	}

	// Size of table in bytes
	int byteSize()
	{
		return HEADER + count()*sizeof(int);
	}

	// Number of free cells
	int unused()
	{
		int res = 0;
		int * table = getTable();
		for (int i = 0; i < count(); i++)
			if (!table[i])
				res++;
		return res;
	}

	// Returns position in stream, at which the document's data starts
	int getPosition(int docid)
	{
		int * table = getTable();
		int hash = getHash(docid);
		if (hash >= 0 && hash < count())
			return table[hash];
		else
			return -1;			
	}

	// Sets at which position in stream the document's data starts
	void setPosition(int docid, int pos)
	{
		int * table = getTable();
		int hash = getHash(docid);
		assert(hash >= 0 && hash < count());
		table[hash] = pos;
	}

	// Get stream position of the next hashed docid
	int getNextPosition(int docid)
	{
		int * table = getTable();
		int hash = getHash(docid);
		if (hash >= 0 && hash < count()) {
			do {
				hash++;
			} while (hash < count() && !table[hash]);
			if (hash < count())
				return table[hash];
		}
		return -1;
	}

	// Copies table's data to a specified location in memory
	void dump(byte * dest)
	{
		memcpy(dest, mData, byteSize());
	}
private:
	int getHash(int id) { return (id - *((int *)(mData + 2))) >> mData[1]; }
	int * getTable() { return (int *)(mData + HEADER); }

	byte * mData;
	bool mOwner; // this objects owns mData
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

// Encode integer value using varint algo and insert it in pos
__DEBUG_INLINE void encode_int(vector<byte> &enc, int val, int pos = -1)
{
	// 0s are very rare so we use 0 a special marker and encode real zeroes with an impossible value: [0x80 0x00]
	if (!val) {
		if (pos < 0) {
			enc.push_back(0x80);
			enc.push_back(0);
		}
		else {
			enc.insert(enc.begin() + pos, 0);
			enc.insert(enc.begin() + pos, 0x80);			
		}
		return;
	}
	// 7 data bits per byte, setting the highest bit signals continuation in the next byte
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

// Decode integer value from the position "index" in the stream using varint and put it into val
// returns new position after the decoded value
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

// Encode a sequential series of postings for the same document
// Returns
__DEBUG_INLINE posting * encode_doc_postings(vector<byte> &enc, posting * post, const posting * end)
{
	int docid = post->docid;
	// Encode docid only once
	encode_int(enc, docid);
	// Remember the position right after the encoded docid. We'll use this later
	int size_pos = enc.size();
	// 
	int prev_pos = post->pos;	
	// Since field_id is usually just a few bits and 2nd and 3rd bytes are often 0, move field_id to the start of position
	int field_id = prev_pos >> 24;
	int pos = prev_pos & ((1 << 24) - 1);
	pos = (pos << enc[0]) | field_id;
	encode_int(enc, pos);
	post++;
	// Now encode all other postings' positions of this document using delta encoding + usual compression
	while (post < end && post->docid == docid) {
		encode_int(enc, post->pos - prev_pos);
		prev_pos = post->pos;
		post++;
	}
	// Now that we know how much space encoded postings take, put this size right after the encoded docid
	int doc_size = enc.size() - size_pos;
	encode_int(enc, doc_size, size_pos);
	// Special "end-of-doc" marker, which is just 0
	enc.push_back(0);
	return post;
}


// Decode document's postings and put them into dec
// returns new position in the stream after decoded postings
__DEBUG_INLINE int decode_doc_postings(vector<int> &dec, const vector<byte> &enc, int index)
{
	int docid = 0;
	int doc_size = 0;
	// Decode document id (once)
	index = decode_int(enc, index, &docid);
	// Get size of the block containing positions of postings in this document
	index = decode_int(enc, index, &doc_size);	
	int end = index + doc_size;
	int pos = 0, prev_pos = 0;
	int field_id = 0;	
	// First posting's field_id is encoded in the first few bits of the value
	index = decode_int(enc, index, &pos);
	field_id = pos & ((1 << enc[0]) - 1);
	prev_pos = (pos >> enc[0]) | (field_id << 24);
	dec.push_back(docid);
	dec.push_back(prev_pos);
	// Rest of the postings are encoded using delta encoding
	while (index < end) {
		dec.push_back(docid);
		index = decode_int(enc, index, &pos);
		pos += prev_pos;
		dec.push_back(pos);
		prev_pos = pos;
	}
	return index+1;
}

// Decode document id and return it in docid, skip postings, return the next position
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
	posting * last = e - 1;
	int diff = last->docid - p->docid;
	// Count unique document ids to calculate size of a lookup table
	// And max field value
	int uniques = 1;
	int last_id = -1;
	int max_field = -1;
	for (posting * i = p; i < e; i++) {
		if (i->docid != last_id) {
			uniques++;
			last_id = i->docid;
		}
		max_field = __max(max_field, i->pos >> 24);
	}
	// highest set bit of the largest field_id
	unsigned long field_bits = 0;
	_BitScanReverse(&field_bits, max_field);
	field_bits++;
	// We'll use this to better compress posting positions
	enc.push_back(field_bits);
	// highest set bit of the largest document id
	unsigned long highest = 0;
	_BitScanReverse(&highest, diff);
	highest++; // next power of 2
	// highest set bit of the number of unique docs
	unsigned long unique_bits = 0;
	_BitScanReverse(&unique_bits, uniques);	
	unique_bits++; // next power of 2
	// 32 (2^5) unique docs per cell of a hash table seems reasonable
	// Expanding hash table further noticeably increases size of the vector, but doesn't provide much in the way of performance	
	int lookup_bits = __max((int)unique_bits - 5, 4);
	int offset_bits = __max((int)highest - lookup_bits, 0);	
	// Arguments: number of elements (1 << arg1), hash function (docid >> arg2), lowest document id
	LookupTable lookup(lookup_bits, offset_bits, p->docid);
	// Reserve enough space for the lookup table at the start of the vector
	enc.resize(lookup.byteSize() + 1);
	while (p < e) {
		// Save document's position in encoded stream into hash table
		// Don't rewrite already used cells
		if (lookup.getPosition(p->docid) == 0)
			lookup.setPosition(p->docid, enc.size());
		p = encode_doc_postings(enc, p, e);
	}
	// Save table data to the vector
	lookup.dump(&enc[1]);
	//printf("Unused: %d\n", lookup.unused());
}

// takes encoded input, in your very own cool format, emits raw output
// for verification only, basically
void decode(vector<int> &dec, const vector<byte> &enc)
{	
	// Just skip a lookup table
	LookupTable lookup((byte *)&enc[1]);
	int index = lookup.byteSize()+1;
	while (index < enc.size()) {
		index = decode_doc_postings(dec, enc, index);
	}
}

// Returns position of document postings in the encoded stream, or -1 if not found
int find_doc(const vector<byte> &enc, int id)
{
	LookupTable lookup((byte *)&enc[1]);
	int docid = 0;
	// Find a position of the first document with the same hash value
	int i1 = lookup.getPosition(id);
	if (i1 <= 0)
		return -1;
	int i2 = read_doc(enc, i1, &docid);
	if (docid == id)
		return i1;
	i1 = i2;
	// One iteration of linear interpolation between this hashed docid and the next one
	// As hash tables grows, binary search return diminishes
	int next_pos = lookup.getNextPosition(id);
	if (next_pos >= 0) {
		int next_docid = docid;
		// Get next hashed doc id
		read_doc(enc, next_pos, &next_docid);
		// Guesstimate position of our sought id via linear interpolation
		next_pos = lirp(i1, next_pos, docid, next_docid, id); 		
		while (next_pos >= i1) {
			// This position could be anywhere in the blob, so keep decrementing it until we hit an "end-of-doc" marker (which is 0)
			while (enc[next_pos] != 0 && next_pos >= i1)
				next_pos--;
			// Make sure it's not an actually encoded honest to god 0
			if (next_pos >= i1 && enc[next_pos-1] != 0x80) {
				// Get docid right after the marker
				read_doc(enc, next_pos+1, &next_docid);
				if (next_docid == id) // Found
					return next_pos+1;
				else if (next_docid < id) { // Need to go forward from here
					docid = next_docid;
					i1 = next_pos+1;					
					break;
				}
				else // Keep going back
					next_pos--;
			}
			else // Keep going back
				next_pos--;
		}
	}
	
	// Go forward from there until we find our document or make sure there isn't one
	int size = enc.size();	
	while (i1 < size && docid < id) {
		i2 = read_doc(enc, i1, &docid);
		if (docid == id)
			return i1;		
		i1 = i2;
	}
	return -1;
}

// fetches all postings (for both keywords) that match the given id
// output postings are mixed together, and returned in the ascending in-document position order
// "which keyword was in that position" data is intentionally lost (in reality, it would not be)
void lookup(vector<int> &res, const vector<byte> &enc1, const vector<byte> &enc2, int id)
{
	int from = (int)res.size();
	int i1 = find_doc(enc1, id);
	int i2 = find_doc(enc2, id);
	if (i1 >= 0)
		decode_doc_postings(res, enc1, i1);
	if (i2 >= 0)
		decode_doc_postings(res, enc2, i2);
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
	// Somehow this vvv 
	int p1 = 6 + (1 << enc1[1]) * sizeof(int)+1;
	int p2 = 6 + (1 << enc2[1]) * sizeof(int)+1;
	// is 15 ms faster than this vvv (on a single iteration)
	//LookupTable lookup1((byte *)&enc1[1]);
	//LookupTable lookup2((byte *)&enc2[1]);
	//int p1 = lookup1.byteSize()+1;
	//int p2 = lookup2.byteSize()+1;
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
