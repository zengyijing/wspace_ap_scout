#ifndef BASE_RATE_
#define BASE_RATE_


#include "cpp_lib.h"
#include "c_lib.h"
#include "monotonic_timer.h"

//rate testing
//#define TEST_RATE 1

//microseconds
//#define DIFS_80211b 50
#define DIFS_80211b 28
#define DIFS_80211ag 28
#define SLOT_TIME 9

static int32_t mac80211b_rate[] = {10,20,55,110};
static int32_t mac80211ag_rate[] = {60, 90, 120, 180, 240, 360, 480, 540};
static int32_t mac80211abg_rate[] = {10, 20, 55, 60, 90, 110, 120, 180, 240, 360, 480, 540};
static int32_t mac80211abg_num_rates = sizeof(mac80211abg_rate)/sizeof(mac80211abg_rate[0]);

//static int32_t loss_rates[12] = {0, 10, 0, 10, 0, 10, 100, 0, 100, 100, 0, 100}; 
static int32_t loss_rates[12] = {0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}; 
//static int32_t loss_rates[12] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 100}; 

/**
 * Different rate adaptation algorithms
 */
enum RateAdaptVersion
{
	kSampleRate = -2,
	kRRAA = -1,
	kFixed = 0,
	kBatchSeq = 1,
	kBatchRandom = 2,
	kScoutSeq = 3, 
	kScoutRandom = 4, 
	kScoutBoundLossSeq = 5,
	kScoutBoundLossRandom = 6, 
};

/**
 * Packet type or status
 */
enum PacketStatus
{
	kInValid = -99,
	kTimeOut = -2,
	kNAKed = -1,
	kSent = 0,
	kACKed = 1,
};


/**
 * Packet information
 * timer: the time record the packet
 */
struct PacketInfo
{
	MonotonicTimer timer;
	uint32_t seq_num;
	uint16_t rate;
	uint16_t length;
	PacketStatus status;
};



class BaseRate
{
public:
	BaseRate() {};
	~BaseRate() {};

//===========================================================================
	/** 
	* Each rate adaptation algorithm must inherit from this method
	* @return the selected best rate
	*/
	virtual int32_t ApplyRate(int32_t& case_num) = 0;

//===========================================================================
	/** 
	* Each rate adaptation algorithm must inherit from this method
	* Insert one new packet information into rate table
	* @param [in] pkt: the packet information, to insert into rate table
	*/
	virtual void InsertIntoRateTable(const PacketInfo& pkt) = 0;

//===========================================================================
	/** 
	* Each rate adaptation algorithm must inherit from this method
	* Remove one old packet information from the rate table
	* @param [in] pkt: the packet information, to insert into rate table
	*/
	virtual void RemoveFromRateTable(const PacketInfo& pkt) = 0;

//===========================================================================
	/** 
	* Each rate adaptation algorithm must inherit from this method
	* Different from InsertIntoRateTable() and RemoveFromRateTable()
	* UpdateRateTable() only update the rate table by calculation
	* e.g., calculate a new loss rate etc.
	*/
	virtual void UpdateRateTable() = 0;

//===========================================================================
	/** 
	* Each rate adaptation algorithm must inherit from this method
	* Initialize the rate table, e.g., put the rates into the table, zero all the data members
	*/
	virtual void InitRateTable() = 0;

//===========================================================================
	/**
	* Only For debugging and testing usage
	*/
	virtual void PrintRateTable() = 0;


//===========================================================================
	/**
	* Only For debugging and testing usage
	*/
	int32_t GetDropThreshold(int32_t rate)
	{
		for(int32_t i = 0; i < 12; ++i)
			if(mac80211abg_rate[i]==rate)
				return loss_rates[i];
	}

};


#endif
