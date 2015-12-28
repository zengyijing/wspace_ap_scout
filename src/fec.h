/*
 * fec.c -- forward error correction based on Vandermonde matrices
 * 980614
 * (C) 1997-98 Luigi Rizzo (luigi@iet.unipi.it)
 *
 * Portions derived from code by Phil Karn (karn@ka9q.ampr.org),
 * Robert Morelos-Zaragoza (robert@spectra.eng.hawaii.edu) and Hari
 * Thirumoorthy (harit@spectra.eng.hawaii.edu), Aug 1995
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:

 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef FEC_H_
#define FEC_H_

#include <stdint.h>
#include <vector>

/*
 * The following parameter defines how many bits are used for
 * field elements. The code supports any value from 2 to 16
 * but fastest operation is achieved with 8 bit elements
 * This is the only parameter you may want to change.
 */
#define GF_BITS  8	/* code over GF(2**GF_BITS) - change to suit */
#if (GF_BITS < 2  && GF_BITS >16)
#error "GF_BITS must be 2 .. 16"
#endif
#if (GF_BITS <= 8)
typedef unsigned char gf;
#else
typedef unsigned short gf;
#endif
#define	GF_SIZE ((1 << GF_BITS) - 1)	/* powers of \alpha */

#define SWAP(a,b,t) {t tmp; tmp=a; a=b; b=tmp;}

#define TICK(t) \
	{struct timeval x ; \
	gettimeofday(&x, NULL) ; \
	t = x.tv_usec + 1000000* x.tv_sec; \
	}
#define TOCK(t) \
	{ u_long t1 ; TICK(t1) ; \
	  if (t1 < t) assert(0); \
	  else t = t1 - t ; \
	  if (t == 0) t = 1 ;}

void fec_free(struct fec_parms *p);
struct fec_parms* fec_new(int k, int n) ;

void fec_encode(struct fec_parms *code, gf *src[], gf *fec, int index, int sz);
int fec_decode(struct fec_parms *code, gf *pkt[], int index[], int sz);

/** Tan's extra functions. */

/** Coding info for vdm. */
class CodeInfo {
public:
	enum Codec {
		kEncoder = 1,
		kDecoder = 2,
	};

	CodeInfo() {}
	CodeInfo(Codec type, int max_batch_size, int pkt_size);
	~CodeInfo();
	void ClearInfo();
	void SetCodeInfo(int k, int n, uint32_t start_seq=0);
	/** 
	 * Push each native packet into the original batch.
	 * Update the length to the maximum length.
	 */
	bool PushPkt(uint16_t len, const gf *src, int ind=0);  
	/** Just return the packet address. No copying happen here.*/
	bool PopPkt(gf **dst, uint16_t *len=NULL);
	void EncodeBatch();
	void DecodeBatch();
	void PrintBatch(Codec type=kEncoder) const;
	void ResetCurInd() { cur_ind_ = 0; }
	int cur_ind() const { return cur_ind_; }
	int k() const { return k_; }
	int n() const { return n_; }
	int sz() const { return sz_; }
	uint32_t start_seq() const { return start_seq_; }
	const int* inds() const { return inds_; }
	void CopyLens(const uint16_t *lens) { 
		assert(k_ > 0);
		memcpy(lens_, lens, k_ * sizeof(uint16_t));
	}
	uint16_t GetLen(int i) const { return lens_[i]; }
	uint16_t* lens() { return lens_; }
	int GetEncodePktLen() const { return sz_; } 
	void GetSeqArr(std::vector<uint32_t> &seq_arr);
	gf** original_batch() const { return original_batch_; }
	gf** coded_batch() const { return coded_batch_; }

private:
	gf** alloc_batch(int k, int sz);
	void zero_batch(gf **batch, int k, int sz);
	void free_batch(gf **batch, int k);
	void print_pkt(int sz, const gf *pkt) const;

	int max_batch_size_;
	int pkt_size_;						
	Codec codec_type_;				/** Server or client. */
	int k_;  									/** Number of data packets. */
	int n_;  									/** Number of encoded packets. */
	int sz_; 									/** Size of packet. */
	uint32_t start_seq_;  			/** Sequence number of the first packet. */
	int *inds_;				  			/** Indices of coding packets. */
	uint16_t *lens_;						/** Length of original packets. */
	int cur_ind_;							/** Current index of the batch. */
	struct fec_parms *code_;	/** Coding struct for fec lib. */
	gf **original_batch_;   	/** Orignal batch of packets. */
	gf **coded_batch_;		  	/** Coded batch. */
};

inline void CodeInfo::zero_batch(gf** batch, int k, int sz) {
	for (int i = 0; i < k; i++) 
		bzero(batch[i], sz * sizeof(gf));
}

/* end of file */
#endif
