#include "wspace_asym_util.h"

using namespace std;

// Member functions for BasicBuf
/*
Purpose: Dequeue an element in the head
*/
void BasicBuf::AcquireHeadLock(uint32 *index)
{
	LockQueue();
	while (IsEmpty())
	{
#ifdef TEST
		printf("Empty!\n");
#endif
		WaitFill();
	}		
	*index = head_pt_mod(); //current element
	LockElement(*index);
	IncrementHeadPt();
	SignalEmpty();
	UnLockQueue();
}

/*
Purpose: Enqueue an element in the tail
*/
void BasicBuf::AcquireTailLock(uint32 *index)
{
	LockQueue();
	while (IsFull())
	{
//#ifdef TEST
		printf("Full! head_pt[%u] tail_pt[%u]\n", head_pt_, tail_pt_);
//#endif
		WaitEmpty();
	}		
	*index = tail_pt_mod(); //current element
	LockElement(*index);
	IncrementTailPt();
	SignalFill();
	UnLockQueue();
}

void BasicBuf::UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len)
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("UpdateBookKeeping invalid index: %d\n", index);
	}
	book_keep_arr_[index].seq_num = seq_num;
	book_keep_arr_[index].status = status;
	book_keep_arr_[index].len = len;
}

void BasicBuf::GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len) const
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("GetBookKeeping invalid index: %d\n", index);
	}
	*seq_num = book_keep_arr_[index].seq_num; 
	*status = book_keep_arr_[index].status;
	*len = book_keep_arr_[index].len; 
}

uint8 BasicBuf::GetNumDups(uint32 index) const
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("GetNumDups invalid index: %u\n", index);
	}
	return book_keep_arr_[index].num_dups;
}

//////////add by Lei
void BasicBuf::UpdateRateInBookKeeping(uint32 index, uint16 rate)
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("UpdateBookKeeping invalid index: %d\n", index);
	}
	book_keep_arr_[index].rate = rate;
}

void BasicBuf::GetRateFromBookKeeping(uint32 index, uint16* rate, uint16* len)
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("GetBookKeeping invalid index: %d\n", index);
	}
	*rate = book_keep_arr_[index].seq_num; 
	*len = book_keep_arr_[index].len; 
}

///////////////////////


void BasicBuf::UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len, 
				uint8 num_retrans, bool update_timestamp)
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("UpdateBookKeeping invalid index: %d\n", index);
	}
	book_keep_arr_[index].seq_num = seq_num;
	book_keep_arr_[index].status = status;
	book_keep_arr_[index].len = len;
	book_keep_arr_[index].num_retrans = num_retrans;
	if (update_timestamp)
	{
		book_keep_arr_[index].timestamp.GetCurrTime();
	}
}

void BasicBuf::GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len, 
				uint8 *num_retrans, TIME *timestamp) const
{
	if (index < 0 || index > BUF_SIZE-1)
	{
		Perror("GetBookKeeping invalid index: %d\n", index);
	}
	*seq_num = book_keep_arr_[index].seq_num; 
	*status = book_keep_arr_[index].status;
	*len = book_keep_arr_[index].len; 
	*num_retrans = book_keep_arr_[index].num_retrans; 
	if (timestamp)
	{
		*timestamp = book_keep_arr_[index].timestamp; 
	}
}

// tx_send_buf
void TxDataBuf::AcquireCurrLock(uint32 *index)
{
	LockQueue();
	while (IsEmpty())
	{
#ifdef TEST
		printf("Empty! head_pt[%u] curr_pt[%u] tail_pt[%u]\n", head_pt_, curr_pt_, tail_pt_);
#endif
		WaitFill();
	}		
	*index = curr_pt_mod(); //current element
	LockElement(*index);
	IncrementCurrPt();
	UnLockQueue();
}

/**
 * Return true if timeout.
 */
bool TxDataBuf::AcquireCurrLock(int wait_ms, uint32 *index)
{
	bool is_timeout=false;
	LockQueue();
	while (IsEmpty() && !is_timeout)
	{
#ifdef TEST
		printf("Empty! head_pt[%u] curr_pt[%u] tail_pt[%u]\n", head_pt_, curr_pt_, tail_pt_);
#endif
		is_timeout = WaitFill(wait_ms);
	}		
	if (!is_timeout)
	{
		*index = curr_pt_mod(); //current element
		LockElement(*index);
		IncrementCurrPt();
	}
	UnLockQueue();
	return is_timeout;
}

void TxDataBuf::EnqueuePkt(uint16 len, uint8 *pkt)
{
	uint32 index=0, seq_num=0;
	uint8 *buf_addr=NULL;
	seq_num = tail_pt()+1;
	AcquireTailLock(&index);
	/** Get the address of the current slot to store the packet. */
	GetPktBufAddr(index, &buf_addr);
	memcpy(buf_addr, pkt, len);
	assert(GetElementStatus(index) == kEmpty);
	// Update bookkeeping
	UpdateBookKeeping(index, seq_num, kOccupiedNew, len, num_retrans(), false/**don't update timestamp for now*/);
	UnLockElement(index);
}

bool TxDataBuf::DequeuePkt(int time_out, uint32 *seq_num, uint16 *len, Status *status, 
				uint8 *num_retrans, uint32 *index, uint8 **buf)
{
	bool is_timeout;
	is_timeout = AcquireCurrLock(time_out, index);
	if (!is_timeout)
	{  /** Incoming pkt from tun.*/
		GetBookKeeping(*index, seq_num, status, len, num_retrans, NULL);
		if (*status == kOccupiedNew || *status == kOccupiedRetrans)
		{
			assert(*len > 0 && *len <= PKT_SIZE && *seq_num > 0);
			/** Get the packet for encoding. */
			GetPktBufAddr(*index, buf);
			/** Update the book keeping info. */
			if (*status == kOccupiedRetrans)
			{   
				//printf("Retransmit pkt[%u] num_retrans[%u]\n", *seq_num, *num_retrans);
				assert(*num_retrans > 0);
				(*num_retrans)--;
				GetElementNumRetrans(*index) = *num_retrans;
			}
			GetElementStatus(*index) = kOccupiedOutbound;
			GetElementTimeStamp(*index).GetCurrTime();  
			//GetElementBatchDuration(*index) = 2000e3;  /** 2000ms. Let the encoding finish before checking the pkt timeout. */
			GetElementBatchDuration(*index) = 0;  /** 2000ms. Let the encoding finish before checking the pkt timeout. */
			//printf("DequeuePkt: pkt[%u] len[%u] batch_duration[%gms]\n", *seq_num, *len, GetElementBatchDuration(*index)/1000.);
		} 
		UnLockElement(*index);
	}
	return is_timeout;
}

void TxDataBuf::DisableRetransmission(uint32 index)
{
	LockElement(index);
	GetElementNumRetrans(index) = 0;
	UnLockElement(index);
}

void TxDataBuf::UpdateBatchSendTime(uint32 batch_duration, const vector<uint32> &seq_arr)
{
	LockQueue();
	for (vector<uint32>::const_iterator it = seq_arr.begin(); it != seq_arr.end(); it++)
	{
		uint32 index = Seq2Ind(*it);
		LockElement(index);
		assert(GetElementStatus(index) == kOccupiedOutbound);
		GetElementTimeStamp(index).GetCurrTime();
		GetElementBatchDuration(index) = batch_duration;
		printf("UpdateBatchSendTime: seq[%u] batch_duration[%ums]\n", GetElementSeqNum(index), batch_duration/1000);
		UnLockElement(index);
	}
	UnLockQueue();
}

void RxRcvBuf::AcquireHeadLock(uint32 *index, uint32 *head)
{
	LockQueue();
	while (IsEmpty())
	{
#ifdef TEST
		printf("Empty!\n");
#endif
		WaitFill();
	}		
	*index = head_pt_mod(); 
	*head = head_pt();
	LockElement(*index);
	UnLockQueue();
}

void Perror(char *msg, ...)
{
	va_list argp;
	va_start(argp, msg);
	vfprintf(stderr, msg, argp);
	va_end(argp);
	exit(-1);
}

void AthHeader::SetRate(uint16 rate)
{
	switch(rate)
	{
 		case 10:
			rate_ = ATH5K_RATE_CODE_1M;
			break;
		case 20:
			rate_ = ATH5K_RATE_CODE_2M;
			break;
		case 55:
			rate_ = ATH5K_RATE_CODE_5_5M;
			break;
		case 110:
			rate_ = ATH5K_RATE_CODE_11M;
			break;
		case 60:
			rate_ = ATH5K_RATE_CODE_6M;
			break;
		case 90:
			rate_ = ATH5K_RATE_CODE_9M;
			break;
		case 120:
			rate_ = ATH5K_RATE_CODE_12M;
			break;
		case 180:
			rate_ = ATH5K_RATE_CODE_18M;
			break;
		case 240:
			rate_ = ATH5K_RATE_CODE_24M;
			break;
		case 360:
			rate_ = ATH5K_RATE_CODE_36M;
			break;
		case 480:
			rate_ = ATH5K_RATE_CODE_48M;
			break;
		case 540:
			rate_ = ATH5K_RATE_CODE_54M;
			break;
		default:
			Perror("Error: SetRate() invalid rate[%d]\n", rate);
	}
}

// Return rate * 10
uint16 AthHeader::GetRate()
{
	switch(rate_)
	{
 		case ATH5K_RATE_CODE_1M:
			return 10;
		case ATH5K_RATE_CODE_2M:
			return 20;
		case ATH5K_RATE_CODE_5_5M:
			return 55;
		case ATH5K_RATE_CODE_11M:
			return 110;
		case ATH5K_RATE_CODE_6M:
			return 60;
		case ATH5K_RATE_CODE_9M:
			return 90;
		case ATH5K_RATE_CODE_12M:
			return 120;
		case ATH5K_RATE_CODE_18M:
			return 180;
		case ATH5K_RATE_CODE_24M:
			return 240;
		case ATH5K_RATE_CODE_36M:
			return 360;
		case ATH5K_RATE_CODE_48M:
			return 480;
		case ATH5K_RATE_CODE_54M:
			return 540;
		default:
			Perror("Error: GetRate() invalid rate[%d]\n", rate_);
	}
}

// For four headers
void AthDataHeader::SetAthHdr(uint32 seq_num, uint16 len, uint16 rate)
{
	type_ = ATH_DATA;
	if (len > PKT_SIZE - ATH_DATA_HEADER_SIZE)    //PKT_SIZE is the MTU of tun0
	{
		Perror("Error: SetAthHdr pkt size: %d ATH_DATA_HEADER_SIZE: %d is too large!\n", 
			   len, ATH_DATA_HEADER_SIZE);
	}
	else
	{
		len_ = len;
	}
	seq_num_ = seq_num;
	SetRate(rate);
}

void AthDataHeader::ParseAthHdr(uint32 *seq_num, uint16 *len, char *rate)
{
	if (type_ != ATH_DATA)
	{
		Perror("ParseAthHdr error! type_: %d != ATH_DATA\n", type_);
	}
	if (len_ > (PKT_SIZE - ATH_DATA_HEADER_SIZE))
	{    
		Perror("Error: ParseAthHdr pkt size: %d ATH_DATA_HEADER_SIZE: %d is too large!\n", 
			   len_, ATH_DATA_HEADER_SIZE);
	}
	else
	{
		*len = len_;		     
	}
	*seq_num = seq_num_;
	*rate = rate_;
}

/** FEC vdm */
void AthCodeHeader::SetHeader(uint32 raw_seq, uint32 batch_id, uint32 start_seq, char type, 
				int ind, int k, int n, const uint16 *len_arr)
{
	assert(raw_seq > 0 && batch_id > 0 && start_seq > 0 && ind >= 0 && ind < n && k >= 0 && n >= 0 && k <= n);
	SetRawSeq(raw_seq);
	batch_id_ = batch_id;
	type_ = type;
	start_seq_ = start_seq;
	ind_ = ind;
	k_ = k;
	n_ = n;
	memcpy((uint8*)this + ATH_CODE_HEADER_SIZE, len_arr, k_ * sizeof(uint16));
}

void AthCodeHeader::ParseHeader(uint32 *batch_id, uint32 *start_seq, int *ind, int *k, int *n) const
{
	assert(batch_id_ > 0 && start_seq_ > 0 && ind_ < n_ && k_ <= n_ && k_ > 0 && k_ <= MAX_BATCH_SIZE);
	*batch_id = batch_id_;
	*start_seq = start_seq_;
	*ind = ind_;
	*k = k_;
	*n = n_;
}

/** GPS Header.*/
void GPSHeader::Init(double time, double latitude, double longitude, double speed)
{
	assert(speed >= 0);
	seq_++;
	type_ = GPS;
	time_ = time;
	latitude_ = latitude;
	longitude_ = longitude;
	speed_ = speed;
}

/** GPSLogger.*/
GPSLogger::GPSLogger() : fp_(NULL) {}

GPSLogger::~GPSLogger()
{
	if (fp_ && fp_ != stdout)
		fclose(fp_);
}

void GPSLogger::ConfigFile(const char* filename)
{
	if (filename)
	{
		filename_ = filename;
		fp_ = fopen(filename_.c_str(), "w");
		assert(fp_);
		fprintf(fp_, "###Seq\tTime\tLatitude\tLongitude\tSpeed\n");
		fflush(fp_);
	}
	else
	{
		fp_ = stdout;
	}
}

void GPSLogger::LogGPSInfo(const GPSHeader &hdr)
{
	if (fp_ == stdout)
		fprintf(fp_, "GPS pkt: ");
	fprintf(fp_, "%d\t%.0f\t%.6f\t%.6f\t%.3f\n", hdr.seq_, hdr.time_, hdr.latitude_, hdr.longitude_, hdr.speed_);
	fflush(fp_);
}

/**
 * Parse the nack packet.
 * @param [out] type: either ACK or RAW_ACK.
 * @param [out] ack_seq: Sequence number of this ack packet.
 * @param [out] num_nacks: number of nacks in this ACK packet.
 * @param [out] end_seq: Highest sequence number of the good packets.
 * @param [out] seq_arr: Array of sequence numbers of lost packets.
*/ 
void AckPkt::ParseNack(char *type, uint32 *ack_seq, uint16 *num_nacks, uint32 *end_seq, uint32 *seq_arr, uint16 *num_pkts)
{
	*type = ack_hdr_.type_;
	*ack_seq = ack_hdr_.ack_seq_;
	*num_nacks = ack_hdr_.num_nacks_;
	*end_seq = ack_hdr_.end_seq_;
	if (num_pkts)
		*num_pkts = ack_hdr_.num_pkts_;
	for (int i = 0; i < ack_hdr_.num_nacks_; i++)
	{
		seq_arr[i] = (uint32)(rel_seq_arr_[i] + ack_hdr_.start_nack_seq_);
	}
}

/**
 * Print the nack packet info.
 */ 
void AckPkt::Print()
{
	if (ack_hdr_.type_ == DATA_ACK)
		printf("data_ack");
	else if (ack_hdr_.type_ == RAW_FRONT_ACK)
		printf("raw_front_ack");
	else
		printf("raw_back_ack");
	printf("[%u] end_seq[%u] num_nacks[%u] num_pkts[%u] {", ack_hdr_.ack_seq_, ack_hdr_.end_seq_, ack_hdr_.num_nacks_, ack_hdr_.num_pkts_);
	for (int i = 0; i < ack_hdr_.num_nacks_; i++)
	{
		printf("%u ", ack_hdr_.start_nack_seq_ + rel_seq_arr_[i]);
	}
	printf("}\n");
}

/**
 * Member functions for raw packet info.
 */ 
void TxRawBuf::PushPktStatus(vector<RawPktSendStatus> &status_vec, const RawPktSendStatus &pkt_status)
{
	Lock();
	if (info_deq_.size() >= max_buf_size_)
	{
		ClearPktStatus(status_vec, false/**don't lock*/);
		printf("TxRawBuf: Warning info_deq is full!\n");
	}
	info_deq_.push_back(pkt_status);
	UnLock();
}

void TxRawBuf::PopPktStatus(uint32 end_seq, uint16 num_nacks, uint16 num_pkts, 
				const uint32 *nack_seq_arr, std::vector<RawPktSendStatus> &status_vec)
{
	Lock();
	PushPkts(end_seq, num_nacks, num_pkts, nack_seq_arr);
	PopPktStatus(status_vec);
	UnLock();
}

void TxRawBuf::PushPkts(uint32 end_seq, uint16 num_nacks, uint16 num_pkts, const uint32 *nack_seq_arr)
{
	/** First store all the nacks. */
	for (int i = 0; i < num_nacks; i++)
	{
		int ind = GetInd(nack_seq_arr[i]);
		if (ind < 0)
		{
			printf("Warning: PushPkts: ind[%d] < 0 nack_seq[%u]\n", ind, nack_seq_arr[i]);
			return;
		}
		assert(info_deq_[ind].status_ == RawPktSendStatus::kUnknown);
		info_deq_[ind].status_ = RawPktSendStatus::kBad;
	}

	/** Put the good packets. */
	uint32 begin_seq = end_seq - num_pkts + 1;
	int begin_ind = GetInd(begin_seq);
	int end_ind = GetInd(end_seq);
	if (begin_ind < 0 || end_ind < 0)
	{
		printf("Warning: PushPkts: begin_seq[%u] end_seq[%u] begin_ind[%d] end_ind[%d]\n", begin_seq, end_seq, begin_ind, end_ind);
		return;
	}

	for (int i = begin_ind; i <= end_ind; i++)
	{
		if (info_deq_[i].status_ == RawPktSendStatus::kUnknown)
			info_deq_[i].status_ = RawPktSendStatus::kGood;
	}

	/** Delete packets before the first ack.*/
	if (begin_ind > 0)
	{
		info_deq_.erase(info_deq_.begin(), info_deq_.begin() + begin_ind);
		//printf("PushPkts: after erase info_deq_len[%u]\n", info_deq_.size());
	}
}

void TxRawBuf::PopPktStatus(vector<RawPktSendStatus> &pkt_status_vec)
{
	pkt_status_vec.clear();
	deque<RawPktSendStatus>::iterator it;
	for (it = info_deq_.begin(); it != info_deq_.end(); it++)
	{
		if (it->status_ != RawPktSendStatus::kUnknown)
		{
			pkt_status_vec.push_back(*it);
		}
		else
		{
			break;
		}
	}
	info_deq_.erase(info_deq_.begin(), it);
}

void TxRawBuf::ClearPktStatus(vector<RawPktSendStatus> &status_vec, bool is_lock)
{
	status_vec.clear();
	if (is_lock)
		Lock();
	for (size_t i = 0; i < info_deq_.size(); i++)
	{
		info_deq_[i].status_ = RawPktSendStatus::kBad;
		status_vec.push_back(info_deq_[i]);
	}
	info_deq_.clear();
	if (is_lock)
		UnLock();
}

void TxRawBuf::Print(bool is_lock)
{
	deque<RawPktSendStatus>::const_iterator it;
	if (is_lock)
		Lock();
	printf("TxRawBuf:\n");
	for (it = info_deq_.begin(); it != info_deq_.end(); it++) 
		it -> Print();
	if (is_lock)
		UnLock();
}

int AckContext::WaitFill(int wait_ms)
{
	struct timespec time_to_wait = {0, 0};
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
	int err = pthread_cond_timedwait(&fill_cond_, &lock_, &time_to_wait);
	return err;
}

