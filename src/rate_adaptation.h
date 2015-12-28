#ifndef PACKET_HISTORY_RECORDS_
#define PACKET_HISTORY_RECORDS_

#include "sample_rate.h"
#include "robust_rate.h"

class RateAdaptation
{
private:  
	list<PacketInfo> history_records; //record history packets
	list<PacketInfo>::iterator it;

	MonotonicTimer duration;  //the duration that keep the history packets

	BaseRate* base_rate;

	SampleRate* sample_rate;
	RobustRate* robust_rate;

	/**
	 * Debug and test usage
	 */
	MonotonicTimer* packet_sent;
	MonotonicTimer* packet_ack;


	int32_t counting;
	MonotonicTimer record_start;
	MonotonicTimer record_end;
	MonotonicTimer record_sum;
	MonotonicTimer record_min;
	MonotonicTimer record_max;

	uint32_t break_number;
	bool setting;

protected: 
	/**
	 * Debug and test usage, the record will no receive any 
	 * packet info/status whose sequence numbers are smaller
	 * than seq_num
	 * @param [in] seq_num: the sequence number of the packet 
	 */
	void SetBreakSeqNum(uint32_t seq_num);

public:
	RateAdaptation(const RateAdaptVersion &version);
	~RateAdaptation();
	/**
	 * The sending packet code will invoke ApplyRate each time to 
	 * make a rate decision, this function will invoke the corresponding
	 * rate adaptation algorithm by base_rate pointer
	 * @param [out] the rate decision 
	*/
	int32_t ApplyRate(int32_t& case_num);

	/**
	 * The packet ACK/NAK/Timeout information will be inserted into the 
	 * record, besides this, the sequence number, rate and length of the 
	 * packet will be recorded too
	 * @param [in] seq: the sequence number of the packet
	 * @param [in] status: the packet type of the packet, please refer to PacketStatus
	 * @param [in] rate: the rate of the packet
	 * @param [in] len: the packet length of the packet
	 */ 
	void InsertRecord(uint32_t seq, PacketStatus status, uint16_t rate, uint16_t len);

	/**
	* In this function, both the packet record and the rate table of rate selection algorithm will be updated, the packet record will remove the out-of-date packets
	* the rate table will remove the corresponding info. of these packets too
	* @param [in] now: the current time stamp
	*/ 
	void UpdateRecords(const MonotonicTimer& now);

};
#endif
