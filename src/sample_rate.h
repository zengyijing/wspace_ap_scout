#ifndef SAMPLE_RATE_
#define SAMPLE_RATE_

#include "base_rate.h"


//#define UP_TO_DOWN
#define DOWN_TO_UP 1



  /**
   * to store the packet statistic information
   * for each rate, or all,
   * to make a rate decision, or calculate the average throughput
   */
struct PacketStat
{

	int32_t num_sent;
	int32_t num_acked;
	int32_t num_naked;
	int32_t num_timeout;
	double len_sent;
	double len_acked;
	double len_naked;
	double len_timeout;

	int32_t num_conti_loss;  

	double total_tx_time; //microsecond
	double avg_tx_time;
	double min_avg_tx_time;
	/**
	 * Initialize the PacketStat
	 */
	PacketStat():num_sent(0), num_acked(0), num_naked(0), num_timeout(0),
			num_conti_loss(0), total_tx_time(0.0), avg_tx_time(1e10), min_avg_tx_time(0),
			len_sent(0), len_acked(0), len_naked(0), len_timeout(0)
	{};
  
	/**
	 * Print the data members, debug and test usage
	 */
	void PrintStat()
	{
		cout<<num_sent<<"\t"<<num_acked<<"\t\t"<<num_naked+num_timeout<<"\t\t"<<num_conti_loss<<" \t";
		cout<<total_tx_time<<"\t\t\t"<<avg_tx_time<<"\t\t"<<min_avg_tx_time<<endl;
		//cout<<total_tx_time<<"\t\t"<<
	}
	/**
	 * Debug and test usage, clear all the data members
	 */
	void clear()
	{
		len_sent = 0.0; len_acked = 0.0; len_timeout = 0; len_naked = 0; 
		min_avg_tx_time = 0; avg_tx_time = 1e10;
		num_conti_loss = 0; total_tx_time = 0.0;
		num_sent = 0; num_acked = 0; num_timeout = 0; num_naked = 0;
	}
};



struct RateHistory
{
	uint16_t rate;
	PacketStat history;
	RateHistory(int32_t r, PacketStat ps):rate(r), history(ps){};
};


class SampleRate : public BaseRate
{
private:
	uint32_t pkt_count;
	int32_t cur_rate_pos;
	uint64_t counting;


	MonotonicTimer last_update;
	MonotonicTimer update_interval;



	vector<RateHistory> rate_table;

	PacketStat statistic;

public:
	SampleRate();
	~SampleRate();
	/**
	 * inherit from BaseRate, refer to BaseRate interface
	 */
	int32_t ApplyRate(int32_t& case_num);

	/**
	* inherit from BaseRate, refer to BaseRate interface
	*/
	void InsertIntoRateTable(const PacketInfo& pkt);
 
	/**
	* inherit from BaseRate, refer to BaseRate interface
	*/	
	void RemoveFromRateTable(const PacketInfo& pkt);

	//update means calculate the corresponding avg_tx_time etc.
	bool update_rate_table;
	/**
	* inherit from BaseRate, refer to BaseRate interface
	*/	
	
	void UpdateRateTable();

	/**
	* inherit from BaseRate, refer to BaseRate interface
	 */	
	void InitRateTable();
	/*
	MonotonicTimer sr_start;
	bool starting;
	MonotonicTimer sr_end;
	*/
 
	/**
	* inherit from BaseRate, refer to BaseRate interface
	*/	   
	void PrintRateTable();

};


#endif
