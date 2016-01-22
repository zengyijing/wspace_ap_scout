#ifndef FEEDBACK_RECORDS_H_
#define FEEDBACK_RECORDS_H_

#include "c_lib.h"
#include "cpp_lib.h"
#include "base_rate.h"
#include "pthread_wrapper.h"

/** 
 * track the number of ack n_ack_ and number of sent packets
 * n_sent_ of each rate, number of nak and timeout is n_sent_ - n_ack_ 
 */

struct RateInfo {
  uint32_t n_ack_;
  uint32_t n_sent_; 
};

class FeedbackRecords {
 private:
  MonotonicTimer duration_;   /* The tracking time window size */
  deque<PacketInfo> records_; /* tracking each ack and nak of each packet during the time window */
  pthread_mutex_t lock_;   /* Lock the data structure. */

  void ResetRateTable();

  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }

 protected:
  /**
  * temporarly store the rate table of each rate's n_ack_ and n_sent_
  */
  map<uint16_t, struct RateInfo> rate_table_; 

//=========================================================================
  /** 
   * Get t's index in the records_
   * @param [in] t: 
   * @return index: the smallest index of the timer that larger than t  
   */
  int32_t BinarySearchRecords(const MonotonicTimer& t);  

 public:  
//=========================================================================
  /**
   * Default constructor:
   * @param [in] rate_arr: Candidate data rates.
   * @param [in] rate_cnt: Number of candidate rates.
   * @param [in] t: 2 seconds by default
   */
  FeedbackRecords(const int32_t *rate_arr, int32_t num_rates, MonotonicTimer t = MonotonicTimer(2, 0));
  ~FeedbackRecords();
//=========================================================================
  /**
   * Change the tracking window size
   * Note: Locking is included.
   * @param [in] t: the new tracking window size
   */
  void SetWindowSize(MonotonicTimer t);
  
//=========================================================================
  /** 
   * Calculate the loss rate of each data rate
   * in the time range [start, end).
   * If no packet is sent at a given rate, set loss rate to be -1.
   * Note: Locking is included.
   * @param [in] start: start time to calculate the loss rates.
   * @param [in] end: end time to calculate the loss rates.
   * @param [in] rates: data rates interested. 
   * @param [out] loss_rate: loss rates corresponding to the rates.
   * @return true if packet records are available in this time period and false otherwise.
   */
  bool CalcLossRates(const MonotonicTimer& start, const MonotonicTimer& end, 
        const vector<uint16_t>& rates, vector<double>& loss_rate, bool is_print=false);

//=========================================================================
  /** 
   * Calculate the loss rate of the given data rate.
   * in the time range [start, end).
   * If no packet is sent at a given rate, set loss rate to be -1.
   * Note: Locking is included.
   * @param [in] start: start time to calculate the loss rate.
   * @param [in] end: end time to calculate the loss rate.
   * @param [in] rate: data rate interested. 
   * @param [out] loss_rate: loss rate of the data rate. If no rate is sent, 
   * loss is set to -1.
   */
  void CalcLossRates(const MonotonicTimer& start, const MonotonicTimer& end, 
        uint16_t rate, double *loss_rate);

//=========================================================================
  /** 
   * Calculate the success rates (the rates that are either acked or are not sent at all)
   * in the time range [start, end).
   * Note: Locking is included.
   * @param [in] start: start time to calculate the loss rates.
   * @param [in] end: end time to calculate the loss rates.
   * @param [in] is_loss_available: If available, 
   * use the loss rates calculated in rate_table_ to find successful rates.
   * @param [out] rates: Data rates that have ack received during [start, end].
   * @return true if packet records are available in this time period and false otherwise.
   */
  bool FindSuccessRates(const MonotonicTimer& start, const MonotonicTimer& end, 
        vector<uint16_t>& rates, bool is_loss_available=false);  
  
//=========================================================================
  /**
   * Insert the newly arrived ACK/NACK and discard the old ACK/NACK.
   * Note: Locking is included.
   * @param [in] seq: the sequence number of the ACK/NAK.
   * @param [in] status: the type of the packet, e.g., ACK(1) or NAK(-1).
   * @param [in] rate: the rate of the packet. 
   * @param [in] time: time when inserting this packet. If 0, use the current time.  
   */  
  bool InsertPacket(uint32_t seq, PacketStatus status, uint16_t rate, MonotonicTimer time = MonotonicTimer(0, 0));  

//=========================================================================
  /**
   * Clear all the data members, e.g., records_ and rate_table_
   * Note: Locking is included.
   */
  void ClearRecords();
 
//========================================================================= 
  /**
   * Print the records in data member records_
   * Note: Locking is included.
   */
  void PrintRecords();
//=========================================================================
};

#endif
