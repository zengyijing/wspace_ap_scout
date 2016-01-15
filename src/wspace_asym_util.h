#ifndef WSPACE_ASYM_UTIL_H_
#define WSPACE_ASYM_UTIL_H_ 

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <deque>
#include <map>
#include <vector>

#include "pthread_wrapper.h"
#include "time_util.h"
#include "monotonic_timer.h"
#include "feedback_records.h"

/* Parameter to be tuned */
#define BUF_SIZE 5000
#define PKT_SIZE 1472
#define ACK_WINDOW 720
#define MAX_BATCH_SIZE 10
static const int kMaxRawBufSize = 4000;
/* end parameter to be tuned*/

#define ATH_DATA 1
#define ATH_CODE 2
#define DATA_ACK 3
#define RAW_FRONT_ACK 4
#define RAW_BACK_ACK 5
#define CELL_DATA 6
#define GPS 7

//modified by Zeng
#define STAT_DATA 8
#define FORWARD_DATA 9
#define CONTROL_BS 10
//end modification

#define INVALID_SEQ_NUM 0
#define INVALID_LOSS_RATE (-1)

#define ATH_DATA_HEADER_SIZE   (uint16)sizeof(AthDataHeader)
#define ATH_CODE_HEADER_SIZE	 (uint16)sizeof(AthCodeHeader) 
#define CELL_DATA_HEADER_SIZE  (uint16)sizeof(CellDataHeader)
#define ACK_HEADER_SIZE 	   	 (uint16)sizeof(AckHeader)
#define GPS_HEADER_SIZE 	   	 (uint16)sizeof(GPSHeader)
#define ACK_PKT_SIZE 	   	     (uint16)sizeof(AckPkt)

//#define DEBUG_BASIC_BUF
//#define TEST

#define ATH5K_RATE_CODE_1M	0x1B
#define ATH5K_RATE_CODE_2M	0x1A
#define ATH5K_RATE_CODE_5_5M	0x19
#define ATH5K_RATE_CODE_11M	0x18
/* A and G */
#define ATH5K_RATE_CODE_6M	0x0B
#define ATH5K_RATE_CODE_9M	0x0F
#define ATH5K_RATE_CODE_12M	0x0A
#define ATH5K_RATE_CODE_18M	0x0E
#define ATH5K_RATE_CODE_24M	0x09
#define ATH5K_RATE_CODE_36M	0x0D
#define ATH5K_RATE_CODE_48M	0x08
#define ATH5K_RATE_CODE_54M	0x0C

//#define RAND_DROP

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

enum Status
{
	kEmpty = 1,                /** The slot is empty for storing packets. */
	kOccupiedNew = 2,          /** The slot stores a new packet. */
	kOccupiedRetrans = 3,   	 /** stores a packet should be retransmited. */
	kOccupiedOutbound = 4,     /** a packet transmitted but does not got ack. */
};

//modified by Zeng
enum Laptop
{
	kInvalid = 0, 
	kFront = 1,
	kBack = 2,
	kFrontScout = 3,
	kAfterCombine = 4,
};
//end modification
	
typedef struct
{
	uint32	seq_num;
	Status	status;
	uint16	len;    /** Length of shim layer header + data */
	uint8	num_retrans;
	TIME 	timestamp;
	uint16	rate;
	uint8	num_dups;  /** number of duplications. */
	double	batch_duration;  /** Time to finish sending the entire batch. */
} BookKeeping;

void Perror(char *msg, ...);

class BasicBuf
{
public:
	BasicBuf(): kSize(BUF_SIZE), head_pt_(0), tail_pt_(0)
	{
		// Clear bookkeeping
		for (int i = 0; i < kSize; i++)
		{
			book_keep_arr_[i].seq_num = 0;
			book_keep_arr_[i].status = kEmpty;
			book_keep_arr_[i].len = 0;
			book_keep_arr_[i].num_retrans = 0;
			book_keep_arr_[i].timestamp.GetCurrTime();
			book_keep_arr_[i].num_dups = 0;
		}
		// clear packet buffer
		bzero(pkt_buf_, sizeof(pkt_buf_));
		// initialize queue lock 
		Pthread_mutex_init(&qlock_, NULL);
		// initialize lock array
		for (int i = 0; i < kSize; i++)
		{
			Pthread_mutex_init(&(lock_arr_[i]), NULL);
		}
		// initialize cond variables
		Pthread_cond_init(&empty_cond_, NULL);
		Pthread_cond_init(&fill_cond_, NULL);
	}	

	~BasicBuf()
	{
		// Reset pointer
		head_pt_ = 0;
		tail_pt_ = 0;
		// Destroy lock
		Pthread_mutex_destroy(&qlock_);
		for (int i = 0; i < kSize; i++)
		{
			Pthread_mutex_destroy(&(lock_arr_[i]));
		}
		// Destroy conditional variable
		Pthread_cond_destroy(&empty_cond_);
		Pthread_cond_destroy(&fill_cond_);
	}

	void LockQueue()
	{
		Pthread_mutex_lock(&qlock_);
	}

	void UnLockQueue()
	{
		Pthread_mutex_unlock(&qlock_);
	}

	void LockElement(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("LockElement invalid index: %d\n", index);
		}
		Pthread_mutex_lock(&lock_arr_[index]);
	}

	void UnLockElement(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("UnLockElement invalid index: %d\n", index);
		}
		Pthread_mutex_unlock(&lock_arr_[index]);
	}

	void WaitFill()
	{
		Pthread_cond_wait(&fill_cond_, &qlock_);
	}	

	/** 
	 * @Return: true is time out.
	 */
	bool WaitFill(int wait_ms)
	{
		static struct timespec time_to_wait = {0, 0};
		struct timeval now;
		gettimeofday(&now, NULL);
		while (now.tv_usec+wait_ms*1000 > 1e6)
		{
			now.tv_sec++;
			wait_ms -= 1e3;
		}
		now.tv_usec += wait_ms*1000;
		time_to_wait.tv_sec = now.tv_sec;
		time_to_wait.tv_nsec = now.tv_usec * 1000; 
		int err = pthread_cond_timedwait(&fill_cond_, &qlock_, &time_to_wait);
		return (err == ETIMEDOUT);
	}

	void WaitEmpty()
	{
		Pthread_cond_wait(&empty_cond_, &qlock_);
	}

	void SignalFill()
	{
		Pthread_cond_signal(&fill_cond_);
	}

	void SignalEmpty()
	{
		Pthread_cond_signal(&empty_cond_);
	}

	bool IsFull()
	{
		return ((head_pt_ + BUF_SIZE) == tail_pt_);
	}

	bool IsEmpty()
	{
		return (tail_pt_ == head_pt_);
	}

	uint32 head_pt() const { return head_pt_; }

	uint32 head_pt_mod() const { return (head_pt_%kSize); }

	void set_head_pt(uint32 head_pt) { head_pt_ = head_pt; }

	uint32 tail_pt() const { return tail_pt_; }

	uint32 tail_pt_mod() const { return (tail_pt_%kSize); }

	void set_tail_pt(uint32 tail_pt) { tail_pt_ = tail_pt; }

	void IncrementHeadPt()
	{
		head_pt_++;
	}

	void IncrementTailPt()
	{
		tail_pt_++;
	}
	
	void GetPktBufAddr(uint32 index, uint8 **pt)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetPktBufAddr invalid index: %d\n", index);
		}
		*pt = (uint8*)pkt_buf_[index];
	}

	void StorePkt(uint32 index, uint16 len, const char* pkt)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("StorePkt invalid index: %d\n", index);
		}
		memcpy((void*)pkt_buf_[index], pkt, (size_t)len);
	}

	void GetPkt(uint32 index, uint16 len, char* pkt)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetPkt invalid index: %d\n", index);
		}
		memcpy(pkt, (void*)pkt_buf_[index], (size_t)len);
	}
		
	void AcquireHeadLock(uint32 *index);    // Function acquires and releases qlock_ inside

	void AcquireTailLock(uint32 *index);    // Function acquires and releases qlock_ inside

	void UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len);

	void GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len) const;

	void UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len, uint8 num_retrans, bool update_timestamp);

	void GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len, uint8 *num_retrans,
						TIME *timestamp) const;

	uint8 GetNumDups(uint32 index) const;

	/*
	add by Lei

	*/
	void UpdateRateInBookKeeping(uint32 index, uint16 rate);
	void GetRateFromBookKeeping(uint32 index, uint16* rate, uint16* len);

	Status& GetElementStatus(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetElementStatus invalid index: %d\n", index);
		}
		return book_keep_arr_[index].status;
	}

	uint32& GetElementSeqNum(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetElementSeqNm invalid index: %d\n", index);
		}
		return book_keep_arr_[index].seq_num;
	}

	uint16& GetElementLen(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetElementLen invalid index: %d\n", index);
		}
		return book_keep_arr_[index].len;
	}

	uint8& GetElementNumRetrans(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetElementRetrans invalid index: %d\n", index);
		}
		return book_keep_arr_[index].num_retrans;
	}

	TIME& GetElementTimeStamp(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetElementTimeStamp invalid index: %d\n", index);
		}
		return book_keep_arr_[index].timestamp;
	}

	double& GetElementBatchDuration(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("GetElementBatchSendTime invalid index: %u\n", index);
		}
		return book_keep_arr_[index].batch_duration;
	}

// Data member
	const uint32 kSize;
	uint32 head_pt_;
	uint32 tail_pt_;	
	BookKeeping book_keep_arr_[BUF_SIZE];
	char pkt_buf_[BUF_SIZE][PKT_SIZE];
	pthread_mutex_t qlock_;
	pthread_mutex_t lock_arr_[BUF_SIZE];
	pthread_cond_t empty_cond_, fill_cond_;
};

class TxDataBuf: public BasicBuf
{
public:
	TxDataBuf(): curr_pt_(0), num_retrans_(0) {}	

	~TxDataBuf()
	{
		curr_pt_ = 0;
	}

	void AcquireCurrLock(uint32 *index);

	bool AcquireCurrLock(int wait_ms, uint32 *index);  /** Timeout to sendout whatever packets remained.*/

	uint32 curr_pt() const { return curr_pt_; }

	uint32 curr_pt_mod() const { return (curr_pt_%kSize); }

	void set_curr_pt(uint32 curr_pt) { curr_pt_ = curr_pt; }

	void IncrementCurrPt()
	{
		curr_pt_++;
	}

	bool IsEmpty()
	{
		return (tail_pt_ == curr_pt_);
	}
	
	/** 
	 * Enqueue the packet into the data buffer.
	 * @param [in] len: the length of the packet.
	 * @param [in] pkt: the address of the packet to be enqueued.
	 */
	void EnqueuePkt(uint16 len, uint8 *pkt);

	/**
	 * Dequeue the current packet pointed by the current pointer.
	 * Only return the packet if it's in the new status or retransmission
	 * status.
	 * @param [in] time_out: the timeout value (ms) to return
	 * if no packet is available in this time period. 
	 * @param [out] seq_num: the sequence number of the current packet.
	 * @param [out] len: the length of the dequeued packet.
	 * @param [out] status: the status of the packet.
	 * @param [out] num_retrans: the remaining number of retransmissions for this data packet.
	 * @param [out] buf: the address at which the dequeued packet is stored.
	 * @return true - timeout, no packet is available; false - the
	 * packet is available.
	 */
	bool DequeuePkt(int time_out, uint32 *seq_num, uint16 *len, Status *status, uint8 *num_retrans, uint32 *index, uint8 **buf);

	/**
	 * Update the sending time and batch_duration.
	 * @param [in] batch_duration: time to finish sending the entire batch.
	 * @param [in] seq_arr: seq numbers of packets in this batch.
	 */
	void UpdateBatchSendTime(uint32 batch_duration, const std::vector<uint32> &seq_arr);

	uint8 num_retrans() const { return num_retrans_; }

	void set_num_retrans(uint8 num_retrans) { num_retrans_ = num_retrans; } 

	void DisableRetransmission(uint32 index);

// Data member
	uint32 curr_pt_;
	uint8 num_retrans_;
};

class RxRcvBuf: public BasicBuf
{
public:
	RxRcvBuf()
	{
		Pthread_cond_init(&element_avail_cond_, NULL);
		Pthread_cond_init(&wake_ack_cond_, NULL);
	}

	~RxRcvBuf()
	{
		Pthread_cond_destroy(&element_avail_cond_);
		Pthread_cond_destroy(&wake_ack_cond_);
	}

	void SignalElementAvail()
	{
		Pthread_cond_signal(&element_avail_cond_);
	}

	void WaitElementAvail(uint32 index)
	{
		if (index < 0 || index > BUF_SIZE-1)
		{
			Perror("WaitElementAvail invalid index: %d\n", index);
		}
		Pthread_cond_wait(&element_avail_cond_, &lock_arr_[index]); 
	}

	void SignalWakeAck()
	{
		Pthread_cond_signal(&wake_ack_cond_);
	}

	int WaitWakeAck(int wait_s)
	{
		static struct timespec time_to_wait = {0, 0};
		struct timeval now;
		gettimeofday(&now, NULL);
		time_to_wait.tv_sec = now.tv_sec+wait_s;
		//time_to_wait.tv_nsec = (now.tv_usec + wait_ms * 1000) * 1000;
		int err = pthread_cond_timedwait(&wake_ack_cond_, &qlock_, &time_to_wait);
		return err;
	}

	void AcquireHeadLock(uint32 *index, uint32 *head);

// Data member
	pthread_cond_t element_avail_cond_;  // Associate with element locks
	pthread_cond_t wake_ack_cond_;       // Associate with qlock
};

// Four packet headers
class AthHeader
{
public:
	AthHeader() {}
	~AthHeader() {}
	AthHeader(char type, uint16 rate) : type_(type), raw_seq_(0), rate_(rate) {}

	uint16 GetRate();
	void SetRate(uint16 rate);
	void SetType(char type) { type_ = type; }
	void SetRawSeq(uint32 raw_seq) { raw_seq_ = raw_seq; } 
	int type() const { return type_; }
	uint32 raw_seq() const { return raw_seq_; }

#ifdef RAND_DROP
	bool is_good() const { return is_good_; }
	void set_is_good(bool is_good) { is_good_ = is_good; } 
#endif

// Data
	char type_;
	uint32 raw_seq_;
	uint16 rate_;
#ifdef RAND_DROP
	bool is_good_;
#endif
};

class AthDataHeader : public AthHeader
{
public:
	AthDataHeader() : AthHeader(ATH_DATA, ATH5K_RATE_CODE_6M), seq_num_(0), len_(0) {}
	~AthDataHeader() {}
	void SetAthHdr(uint32 seq_num, uint16 len, uint16 rate);
	void ParseAthHdr(uint32 *seq_num, uint16 *len, char *rate);

// Data
	uint32 seq_num_;
	uint16 len_;    // Length of the data
};

class AthCodeHeader : public AthHeader
{
public:
	AthCodeHeader() : AthHeader(ATH_CODE, ATH5K_RATE_CODE_6M), start_seq_(0), ind_(0), k_(0), n_(0) {}
	~AthCodeHeader() {}
	void SetHeader(uint32 raw_seq, uint32 batch_id, uint32 start_seq, 
			char type, int ind, int k, int n, const uint16 *len_arr);
	void SetInd(uint8 ind) { ind_ = ind; }
	void ParseHeader(uint32 *batch_id, uint32 *start_seq, int *ind, int *k, int *n) const;  
	void GetLenArr(uint16 *len_arr) const
	{
		assert(k_ > 0);
		memcpy(len_arr, (uint8*)this + ATH_CODE_HEADER_SIZE, k_ * sizeof(uint16));
	}
	uint8* GetPayloadStart() const
	{
		return ( (uint8*)this + ATH_CODE_HEADER_SIZE + k_ * sizeof(uint16) );
	}
	int GetFullHdrLen() const
	{
		return (ATH_CODE_HEADER_SIZE + k_ * sizeof(uint16));
	}

	uint32 batch_id() const { return batch_id_; }

// Data
	uint32 batch_id_;   /** Distinguish packets from different batches - for retransmission. start from 1*/
	uint32 start_seq_;  /** indicate the sequence number of the first packet in this batch. */
	uint8 ind_;  				/** current index of the coding packet. */
	uint8 k_;					  /** number of data packets. */
	uint8 n_;						/** number of encoded packets. */
};
	
class CellDataHeader
{
public:
	CellDataHeader(): type_(CELL_DATA)//, len_(0)
	{
	}
	~CellDataHeader() {}

// Data
	char type_;
}; 

class AckHeader
{
public:
	AckHeader() : ack_seq_(0), num_nacks_(0), start_nack_seq_(0), end_seq_(0), num_pkts_(0) {}
	~AckHeader() {}

	void Init(char type)
	{
		type_ = type;
		ack_seq_++;
		num_nacks_ = 0;
		start_nack_seq_ = 0;
		end_seq_ = 0;
		num_pkts_ = 0;
	}

	void set_end_seq(uint32 end_seq) { end_seq_ = end_seq; }

	uint16 num_nacks() const { return num_nacks_; }

	uint16 num_pkts() const { return num_pkts_; }

	void set_num_pkts(uint16 num_pkts) { num_pkts_ = num_pkts; }

// Data
	char type_;
	uint32 ack_seq_;		  		// Record the sequence number of ack 
	uint16 num_nacks_;		  	// number of nacks in the packet
	uint32 start_nack_seq_;   // Starting sequence number of nack
	uint32 end_seq_;          // The end of this ack window - could be a good pkt or a bad pkt 
	uint16 num_pkts_;		  		// Total number of packets included in this ack. 
}; 

class GPSHeader
{
public:
	GPSHeader() : type_(GPS), seq_(0), speed_(-1.0) {}
	~GPSHeader() {}

	void Init(double time, double latitude, double longitude, double speed);

	uint32 seq() const { assert(seq_ > 0); return seq_; }

	double speed() const { assert(speed_ >= 0); return speed_; }

private: 
	friend class GPSLogger;

	char type_; 
	uint32 seq_;  /** sequence number.*/
	double time_;
	double latitude_;
	double longitude_;
	double speed_; 
};

class GPSLogger
{
public:
	GPSLogger();
	~GPSLogger();

	void ConfigFile(const char* filename = NULL);

	void LogGPSInfo(const GPSHeader &hdr);

	std::string filename() const { return filename_; }

private:
	std::string filename_;
	FILE *fp_;
};

class AckPkt
{
public:
	AckPkt() { bzero(rel_seq_arr_, sizeof(rel_seq_arr_)); }
	~AckPkt() {}

	void PushNack(uint32 seq);

	void ParseNack(char *type, uint32 *ack_seq, uint16 *num_nacks, uint32 *end_seq, uint32 *seq_arr, uint16 *num_pkts=NULL);

	uint16 GetLen()
	{
		uint16 len = sizeof(ack_hdr_) + sizeof(rel_seq_arr_[0]) * ack_hdr_.num_nacks_;
		assert(len <= PKT_SIZE && len > 0);
		return len;
	}

	void Print();

	bool IsFull() { return ack_hdr_.num_nacks() >= ACK_WINDOW; }

	void Init(char type) { ack_hdr_.Init(type); }

	void set_end_seq(uint32 end_seq) { ack_hdr_.set_end_seq(end_seq); }

	void set_num_pkts(uint16 num_pkts) { ack_hdr_.set_num_pkts(num_pkts); }

private:
	AckHeader& ack_hdr() { return ack_hdr_; }

	AckHeader ack_hdr_;
	uint16 rel_seq_arr_[ACK_WINDOW];
};

inline void AckPkt::PushNack(uint32 seq)
{
	if (ack_hdr_.num_nacks_ == 0)
	{
		ack_hdr_.start_nack_seq_ = seq;
		rel_seq_arr_[0] = 0;  // Relative seq
	}
	else
	{
		assert(ack_hdr_.num_nacks_ < ACK_WINDOW);
		rel_seq_arr_[ack_hdr_.num_nacks_] = (uint16)(seq - ack_hdr_.start_nack_seq_);
	}
	ack_hdr_.num_nacks_++;
}

// @yijing: Please add a radio id.
class BSStatsPkt
{
public:
	BSStatsPkt()
	{
		bzero(&type_, sizeof(BSStatsPkt));
	}
	~BSStatsPkt() {}

	void Init(char type, uint32 seq, int bs_id, int client_id, double throughput);
	void ParsePkt(uint32 *seq, int *bs_id, int *client_id, double *throughput) const;
	
	char type_; 
	uint32 seq_;  
	int bs_id_;
	int client_id_;
	// int radio_id_;
	double throughput_;
};

inline uint32 Seq2Ind(uint32 seq)
{
	return ((seq-1) % BUF_SIZE);
}

/**
 * This class tracks the status of each raw packet
 * at the sender side. 
 */
class RawPktSendStatus
{
public:
	enum Status
	{
		kBad = -1,
		kUnknown = 0, 
		kGood = 1,
	}; 

	RawPktSendStatus() {}
	~RawPktSendStatus() {}
	RawPktSendStatus(uint32 seq, uint16 rate, uint16 len, Status status) 
		: seq_(seq), rate_(rate), len_(len), status_(status) {} 
	
	void Print() const
	{
		printf("seq[%u] rate[%u] len[%u] status[%d] time[%.3fms]\n", 
			seq_, rate_, len_, int(status_), send_time_.GetMSec());
	}

// Data member
	uint32 seq_;
	uint16 rate_;
	uint16 len_;
	Status status_;  /** Fate of each raw packet. */
	MonotonicTimer send_time_;  
};

/** 
 * A bookkeeping structure to track the info of each
 * raw packet at the sender (base station).
 */
class TxRawBuf
{
public:
	TxRawBuf() : max_buf_size_(kMaxRawBufSize)
	{
		info_deq_.clear();
		Pthread_mutex_init(&lock_, NULL);
	}

	~TxRawBuf()
	{
		Pthread_mutex_destroy(&lock_);
	}

	/**
	 * Insert basic information for each packet. 
	 * Used at TxSendAth.
	 * Note: Locking is included.
	 */
	void PushPktStatus(vector<RawPktSendStatus> &status_vec, const RawPktSendStatus &pkt_status);

	/**
	 * Push both acks and nacks into the status buf and pop out the information
	 * about each fresh raw packet. 
	 * Note: Locking is included.
	 */
	void PopPktStatus(uint32 end_seq, uint16 num_nacks, uint16 num_pkts, 
				const uint32 *nack_seq_arr, std::vector<RawPktSendStatus> &status_vec);

	/**
	 * Delete the records. Used for HandleTimeOut.
	 */
	void ClearPktStatus(vector<RawPktSendStatus> &status_vec, bool is_lock);

	/**
	 * Print out the status of each raw packet.
	 * Note: Locking is included.
	 */
	void Print(bool is_lock);

private:
	/**
	 * Return the index for the sequence number.
	 * @return Index of the raw packet info. If no
	 * info found, return negative. 
	 */
	int GetInd(uint32 seq) const
	{
		if (info_deq_.size() == 0)
			return -1;
		uint32 start_seq = info_deq_.front().seq_;
		int ind = seq - start_seq;
		if (ind >= info_deq_.size())
			return -1;
		else
			return (seq - start_seq);
	}

	void Lock() { Pthread_mutex_lock(&lock_); }
	void UnLock() { Pthread_mutex_unlock(&lock_); }

	void PushPkts(uint32 end_seq, uint16 num_nacks, uint16 num_pkts, const uint32 *nack_seq_arr);

	void PopPktStatus(std::vector<RawPktSendStatus> &pkt_status_vec);

// Data member
	int max_buf_size_;
	std::deque<RawPktSendStatus> info_deq_; 
	pthread_mutex_t lock_;    /** Lock is needed because the bit map is access by two threads. */
};

/**
 * Class to handle two types of ACKs - DATA_ACK and RAW_ACK.  
 */
class AckContext
{
public:
	AckContext(char type) : type_(type), ack_available_(false)
	{
		pkt_ = new AckPkt;
		Pthread_mutex_init(&lock_, NULL);
		Pthread_cond_init(&fill_cond_, NULL);
		Pthread_cond_init(&empty_cond_, NULL);
	}

	~AckContext()
	{
		delete pkt_;
		Pthread_mutex_destroy(&lock_);
		Pthread_cond_destroy(&fill_cond_);
		Pthread_cond_destroy(&empty_cond_);
	}

	void Lock()
	{
		Pthread_mutex_lock(&lock_);
	}

	void UnLock()
	{
		Pthread_mutex_unlock(&lock_);
	}

	/**
	 * Wait for the ack available from TxRcvCell or time out.
	 * @param wait_ms Time duration for timeout in ms.
	 */
	int WaitFill(int wait_ms);

	/**
	 * Wait but no timeout is supported.
	 */
	void WaitFill()
	{
		Pthread_cond_wait(&fill_cond_, &lock_);
	}

	/**
	 * Signal when pkt is filled by TxRcvCell.
	 */
	void SignalFill()
	{
		Pthread_cond_signal(&fill_cond_);
	}

	/**
	 * Wait for TxHandle to process the NACK packet.
	 */
	void WaitEmpty()
	{
		Pthread_cond_wait(&empty_cond_, &lock_);
	}

	/**
	 * Signal by TxHandle when it has finished processing the NACK packet.
	 */
	void SignalEmpty()
	{
		Pthread_cond_signal(&empty_cond_);
	}

	AckPkt* pkt() { return pkt_; }

	bool ack_available() { return ack_available_; }

	void set_ack_available(bool ack_available) { ack_available_ = ack_available; }

	char type() { return type_; }

private:
	char type_;     /** Type of the packet - either DATA_ACK or RAW_ACK. */
	AckPkt *pkt_;
	bool ack_available_;
	pthread_mutex_t lock_;
	pthread_cond_t  fill_cond_, empty_cond_;
};

class FeedbackHandler
{
public:
	FeedbackHandler(char pkt_type) : raw_ack_context_(pkt_type) {}
	~FeedbackHandler() {}

// data member
	AckContext raw_ack_context_;  /** Store the feedback packets with lock.*/
	TxRawBuf raw_pkt_buf_;  			/** Store the raw sequence number for channel estimation. */
};

#endif
