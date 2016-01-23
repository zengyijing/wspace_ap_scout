#ifndef ROBUST_RATE_
#define ROBUST_RATE_

#include "base_rate.h"


struct RRAARateInfo {
  int32_t rate;
  double loss_ratio;
  double p_ori;
  double p_mtl;
  int32_t wnd_size;

  int32_t num_sent;
  int32_t num_acked;
  double len_sent;
  double len_acked;

  RRAARateInfo():rate(0), loss_ratio(1.0), p_ori(0.0), p_mtl(0.0), wnd_size(0),
      num_sent(0), num_acked(0), len_sent(0.0), len_acked(0.0) {};
  void PrintRateInfo() {cout<<rate/10.0<<"\t\t"<<num_sent<<"\t"<<num_acked<<"\t"<<loss_ratio<<"\t"<<p_ori<<"\t"<<p_mtl<<"\t"<<wnd_size<<endl;};
};




class RobustRate : public BaseRate {
 private:
  vector<RRAARateInfo> rraa_rate_table;
  int32_t wnd_counter;
  int32_t cur_rate_pos;

//empirical parameters
  double rraa_alpha;
  double rraa_beta;
  double rraa_difs;
  double packet_len;
 public:
  RobustRate();
  ~RobustRate();


  /**
   * Initilize the RRAA  rate table
   * Inherited from BaseRate, please refer to BaseRate for detail
   */
  void InitRateTable();
  MonotonicTimer sr_start;
  bool starting;
  MonotonicTimer sr_end;
   
  /**
  * insert means insert record into the rate table
  * inherit from BaseRate, please refer to BaseRate interface
  */
  void InsertIntoRateTable(const PacketInfo& pkt);
  /**
   * Remove the out-of-date packet from the rate table
   * inherit from BaseRate, please refer to BaseRate interface
   */
  void RemoveFromRateTable(const PacketInfo& pkt);

  //update means calculate the corresponding avg_tx_time etc.
  bool update_rraa_rate_table;
  /**
   * Do the rate table calculations
   * inherit from BaseRate, please refer to BaseRate interface
   */
  void UpdateRateTable();
  /**
   * return the rate decision based on current rate table
   * inherit from BaseRate
   * @return the rate value (actual value * 10 for current implementation)
   */
  int32_t ApplyRate(int32_t& case_num);

  /**
   * Debug and test usage, print the rate table out
   * inherit from BaseRate
   */
  void PrintRateTable();

};




#endif
