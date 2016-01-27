#ifndef WSPACE_AP_SCOUT_H_
#define WSPACE_AP_SCOUT_H_ 

#include <time.h>
#include <cmath>
#include "wspace_asym_util.h"
#include "tun.h"
#include "fec.h"
#include "rate_adaptation.h"
#include "scout_rate.h"

static const int kMaxDupAckCnt = 10;
//static const int kMaxContiguousTimeOut = 5;

class ClientContext {
 public:
  ClientContext(): encoder_(CodeInfo::kEncoder, MAX_BATCH_SIZE, PKT_SIZE), 
                            data_ack_context_(DATA_ACK), 
                            scout_rate_maker_(mac80211abg_rate, mac80211abg_num_rates, 
                                              GF_SIZE, MAX_BATCH_SIZE), 
                            feedback_handler_(RAW_ACK) {}
  ~ClientContext() {}

  TxDataBuf* data_pkt_buf() { return &data_pkt_buf_; }
  CodeInfo* encoder() { return &encoder_; }
  ScoutRateAdaptation* scout_rate_maker() { return &scout_rate_maker_; }
  AckContext* data_ack_context() { return &data_ack_context_; }
  FeedbackHandler* feedback_handler() { return &feedback_handler_; }
  GPSLogger* gps_logger() { return &gps_logger_; }
  pthread_t* p_tx_send_ath() { return &p_tx_send_ath_; }
  pthread_t* p_tx_handle_data_ack() { return &p_tx_handle_data_ack_; }
  pthread_t* p_tx_handle_raw_ack() { return &p_tx_handle_raw_ack_; }

 private:
  TxDataBuf data_pkt_buf_;
  CodeInfo encoder_;
  ScoutRateAdaptation scout_rate_maker_;
  AckContext data_ack_context_;
  FeedbackHandler feedback_handler_;
  GPSLogger gps_logger_;
  pthread_t  p_tx_send_ath_, p_tx_handle_data_ack_, p_tx_handle_raw_ack_;
};

class WspaceAP {
 public:
  WspaceAP(int argc, char *argv[], const char *optstring);
  ~WspaceAP();
  
  void* TxReadTun(void* arg);

  void* TxSendAth(void* arg);
 
  void* TxSendProbe(void* arg);

  /** 
   * Parse the ACK of data sequence number, used for freeing buffer space and 
   * retransmission.
   */
  void* TxHandleDataAck(void* arg);

  /** 
   * Parse the ACK of raw sequence number and calculate the loss rates for rate 
   * adaptation and FEC. 
   */
  void* TxHandleRawAck(void* arg);  

  void* TxRcvCell(void* arg);

  bool HandleAck(char type, uint32 ack_seq, uint16 num_nacks, uint32 end_seq, uint32* nack_arr, int client_id);

  void HandleTimeOut(int client_id);

  uint8 num_retrans() const { return num_retrans_; }

  void set_num_retrans(uint8 num_retrans) { num_retrans_ = num_retrans; } 

  void InsertFeedback(const vector<RawPktSendStatus> &status_vec_front, int client_id);

#ifdef LOG_LOSS
  void LogLossRate(uint16 rate, double loss, double redundancy, int k, int n);
#endif

#ifdef RAND_DROP
  bool IsDrop() {
    return (rand() % 100 < drop_prob_);
  }

  bool IsDrop(int cnt, const int *inds, int ind) {
    for (int i = 0; i < cnt; i++) {
      if (inds[i] == ind)
        return true;
    }
    return false;
  }

  /**
   * Note: If inds != NULL, it's the caller's job to free the 
   * allocated memory pointed by inds.
   */
  void GetDropInds(int *drop_cnt, int **inds, int client_id);
#endif

  void ParseIP(const vector<int> &ids, map<int, string> &ip_table);

// Data member
  uint8 num_retrans_;
  int ack_time_out_;  // in ms 
  int batch_time_out_;
  int rtt_;   // in ms
  //TxDataBuf data_pkt_buf_;  /** Store the data sequence number and data packets for retransmission.*/
  pthread_t p_tx_read_tun_, p_tx_rcv_cell_, p_tx_send_probe_;
  Tun tun_;    // tun interface
  uint32 coherence_time_;  // in microseconds.
  int contiguous_time_out_;
  int max_contiguous_time_out_;
  int probing_interval_;  // in microseconds.
  uint16 probe_pkt_size_; // in bytes.
  //CodeInfo encoder_;
  //AckContext data_ack_context_;
  //FeedbackHandler front_handler_, back_handler_;
  //ScoutRateAdaptation scout_rate_maker_;
  //GPSLogger gps_logger_;   /** Log the GPS readings.*/
  map<int, ClientContext*> client_context_tbl_;
#ifdef RAND_DROP 
  int drop_prob_;  // drop probability in percentage
#endif

  vector<int> client_ids_;
  int server_id_;

 private:
  /**
   * Overload function used for processing both DATA_ACK and RAW_ACK.
   * @return true - ACK is available, false - timeout for DATA_ACK.
   */
  bool TxHandleAck(AckContext &ack_context, char *type, uint32 *ack_seq, 
    uint16 *num_nacks, uint32 *end_seq, int client_id, int* radio_id, uint32 *nack_seq_arr, uint16 *num_pkts = NULL); 
  
  /**
   * Store the received packet into the ack_context.
   * Used in TxRcvCell.
   */
  void RcvAck(AckContext &ack_context, const char* buf, uint16 len);  

  void RcvGPS(const char* buf, uint16 len, int client_id);

  /**
   * @param is_duplicate: whether to duplicate packets over the cellular link.
   */
  void SendCodedBatch(uint32 extra_wait_time, bool is_duplicate, const vector<uint16> &rate_arr, int client_id,
        int drop_cnt=-1, int *drop_inds=NULL);

  void SendLossRate(int client_id);

};

/** Wrapper function for pthread_create. */
void* LaunchTxReadTun(void* arg);
void* LaunchTxSendAth(void* arg);
void* LaunchTxSendProbe(void* arg);
void* LaunchTxHandleDataAck(void* arg);
void* LaunchTxHandleRawAck(void* arg);
void* LaunchTxRcvCell(void* arg);

/**
 * Print out the information about nack array.
 */
void PrintNackInfo(char type, uint32 ack_seq, uint16 num_nacks, uint32 end_seq, uint32 *nack_arr, uint16 num_pkts=0);

#endif
