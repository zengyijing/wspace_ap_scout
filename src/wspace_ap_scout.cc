#include <vector>
#include <cmath>
#include "wspace_ap_scout.h"
#include "base_rate.h"
#include "monotonic_timer.h"

using namespace std;

WspaceAP *wspace_ap;

static const uint16 kTunMTU = PKT_SIZE - ATH_CODE_HEADER_SIZE - MAX_BATCH_SIZE * sizeof(uint16);

int main(int argc, char **argv) {
  printf("PKT_SIZE: %d\n", PKT_SIZE);
  printf("ACK header size: %d\n", ACK_HEADER_SIZE);
  const char* opts = "r:R:t:T:i:I:S:s:C:c:P:p:r:B:d:V:v:m:M:O:f:n:";
  wspace_ap = new WspaceAP(argc, argv, opts);
  wspace_ap->tun_.CreateConn();

  char front_pkt_type = RAW_FRONT_ACK; 
  char back_pkt_type = RAW_BACK_ACK;

  Pthread_create(&wspace_ap->p_tx_read_tun_, NULL, LaunchTxReadTun, NULL);
  Pthread_create(&wspace_ap->p_tx_send_ath_, NULL, LaunchTxSendAth, NULL);
  Pthread_create(&wspace_ap->p_tx_handle_front_raw_ack_, NULL, LaunchTxHandleRawAck, &front_pkt_type);
  Pthread_create(&wspace_ap->p_tx_handle_back_raw_ack_, NULL, LaunchTxHandleRawAck, &back_pkt_type);
  Pthread_create(&wspace_ap->p_tx_handle_data_ack_, NULL, LaunchTxHandleDataAck, NULL);
  Pthread_create(&wspace_ap->p_tx_rcv_cell_, NULL, LaunchTxRcvCell, NULL);

  Pthread_join(wspace_ap->p_tx_read_tun_, NULL);
  Pthread_join(wspace_ap->p_tx_send_ath_, NULL);
  Pthread_join(wspace_ap->p_tx_handle_front_raw_ack_, NULL);
  Pthread_join(wspace_ap->p_tx_handle_back_raw_ack_, NULL);
  Pthread_join(wspace_ap->p_tx_handle_data_ack_, NULL);
  Pthread_join(wspace_ap->p_tx_rcv_cell_, NULL);

  delete wspace_ap;
  return 0;
}

WspaceAP::WspaceAP(int argc, char *argv[], const char *optstring) 
    : encoder_(CodeInfo::kEncoder, MAX_BATCH_SIZE, PKT_SIZE), 
      data_ack_context_(DATA_ACK), front_handler_(RAW_FRONT_ACK), back_handler_(RAW_BACK_ACK), 
      scout_rate_maker_(mac80211abg_rate, mac80211abg_num_rates, GF_SIZE, MAX_BATCH_SIZE), 
      num_retrans_(0), coherence_time_(0), contiguous_time_out_(0), max_contiguous_time_out_(5) {
  int option;
  uint16 rate;
  bool use_fec = true;
  RateAdaptVersion rate_adapt_version;
  bool enable_duplicate = true;
#ifdef RAND_DROP
  drop_prob_ = 0;
#endif
  vector<int> client_ids;

  while ((option = getopt(argc, argv, optstring)) > 0) {
    switch(option) {
      case 'R':  /** Number of retransmission. */
        num_retrans_ = (uint8)atoi(optarg);
        data_pkt_buf_.set_num_retrans(num_retrans_);
        printf("NUM_RETRANS: %d\n", num_retrans_);
        break;
      case 'r':  /** For data rate*/
        rate = (uint16)atoi(optarg);
        scout_rate_maker_.set_rate(rate);
        printf("Rate: %gMbps\n", rate/10.);
        break;
      case 'T':
        ack_time_out_ = atoi(optarg);
        printf("ACK_TIME_OUT: %dms\n", ack_time_out_);
        break;
      case 't':
        rtt_ = atoi(optarg);
        printf("RTT: %dms\n", rtt_);
        break;
      case 'i':
        strncpy(tun_.if_name_, optarg, IFNAMSIZ-1);
        tun_.tun_type_ = IFF_TUN;
        break;
      case 'S':
        strncpy(tun_.server_ip_eth_,optarg,16);
        printf("server_ip_eth: %s\n", tun_.server_ip_eth_);
        break;

      case 'C':
        strncpy(tun_.controller_ip_eth_,optarg,16);
        printf("controller_ip_eth: %s\n", tun_.controller_ip_eth_);
        break;
      case 'I':
        tun_.server_id_ = atoi(optarg);
        printf("server_id_: %s\n", tun_.server_id_);
        break;
      case 's':
        strncpy(tun_.server_ip_ath_,optarg,16);
        printf("server_ip_ath: %s\n", tun_.server_ip_ath_);
        break;
      case 'M':
        coherence_time_ = atoi(optarg);
        printf("coherence_time[%gms]\n", coherence_time_/1000.);
        break;
      case 'm':
        strncpy(tun_.broadcast_ip_ath_, optarg, 16);
        printf("broadcast_ip_ath: %s\n", tun_.broadcast_ip_ath_);
        break;
      case 'P':
        tun_.port_eth_ = atoi(optarg);
        break;
      case 'p':
        tun_.port_ath_ = atoi(optarg);
        break;
      case 'B':
        batch_time_out_ = atoi(optarg);
        printf("Batch timeout: %dms\n", batch_time_out_);
        break;
      case 'V':
        use_fec = (bool)atoi(optarg);
        scout_rate_maker_.set_use_fec(use_fec);
        printf("Use fec: %d\n", use_fec);
        break;
      case 'v':
        rate_adapt_version = (RateAdaptVersion)atoi(optarg);
        scout_rate_maker_.set_rate_adapt_version(rate_adapt_version);
        scout_rate_maker_.PrintRateAdaptVersion();
        break;
      case 'O':
        enable_duplicate = (bool)atoi(optarg);
        scout_rate_maker_.set_enable_duplicate(enable_duplicate);
        printf("enable_duplicate[%d] Threshold for duplicating over cellular: %g\n", 
        scout_rate_maker_.enable_duplicate(), scout_rate_maker_.duplicate_thresh());
        break;
      case 'f':
        gps_logger_.ConfigFile(optarg);
        printf("GPS log file: %s\n", gps_logger_.filename().c_str());
        break;
      case 'n':
        max_contiguous_time_out_ = atoi(optarg);
        printf("max_contiguous_time_out: %d\n", max_contiguous_time_out_);
        assert(max_contiguous_time_out_ > 0);
        break;
#ifdef RAND_DROP
      case 'd': 
        drop_prob_ = atoi(optarg);
        printf("Packet corrupt probability: %d\%\n", drop_prob_);
        break;
#endif
      case 'c': {
        string addr;
        stringstream ss(optarg);
        while(getline(ss, addr, ',')) {
          if(atoi(addr.c_str()) == 1)
              Perror("id 1 is reserved by controller\n");
          client_ids.push_back(atoi(addr.c_str()));
        }
        break;
      }
      case 'b': {
        ParseIP(client_ids_, tun_.client_ip_tbl_);
        break;
      }
      default:
        Perror("Usage: %s -i tun0/tap0 -S server_eth_ip -s server_ath_ip -C client_eth_ip -c client_ath_ip -m tcp/udp\n", argv[0]);
    }
  }

  assert(tun_.if_name_[0] && tun_.broadcast_ip_ath_[0] && tun_.server_ip_eth_[0] && tun_.server_ip_ath_[0] && tun_.controller_ip_eth_[0] && tun_.server_id_ && tun_.client_ip_tbl_.size());
  for (map<int, string>::iterator it = tun_.client_ip_tbl_.begin(); it != tun_.client_ip_tbl_.end(); ++it) {
    assert(strlen(it->second.c_str()));
  }
  assert(coherence_time_ > 0);
#ifdef RAND_DROP
  srand(time(NULL));
#endif
}

WspaceAP::~WspaceAP() {
}

void WspaceAP::ParseIP(const vector<int> &ids, map<int, string> &ip_table) {
  if (ids.empty()) {
    Perror("Need to indicate ids first!\n");
  }
  vector<int>::const_iterator it = ids.begin();
  string addr;
  stringstream ss(optarg);
  while(getline(ss, addr, ',')) {
    if (it == ids.end())
      Perror("Too many input addresses\n");
    int id = *it;
    ip_table[id] = addr;
    ++it;
  }
}

void WspaceAP::SendLossRate(const Laptop &laptop, const int &client_id) {
  char type = BS_STATS;
  static uint32 seq = 0;
  int bs_id = tun_.server_id_;
  double throughput = 0;
  double loss_rate, th;
  LossMap *loss_map = scout_rate_maker_.GetLossMap(laptop);
  for (int i = 0; i < mac80211abg_num_rates; i++) {
    loss_rate = loss_map->GetLossRate(mac80211abg_rate[i]);
    th = mac80211abg_rate[i] * (1 - loss_rate);
    if(th > throughput)
      throughput = th;
  }
  BSStatsPkt pkt;
  pkt.Init(++seq, bs_id, client_id, (int)laptop, throughput);
  tun_.Write(Tun::kControl, (char *)&pkt, sizeof(pkt));
}


void* WspaceAP::TxReadTun(void* arg) {
  return (void*)NULL;
}

//bool not_drop = false;

void WspaceAP::SendCodedBatch(uint32 extra_wait_time, bool is_duplicate, const vector<uint16> &rate_arr, 
        int drop_cnt, int *drop_inds) {
  uint8 *encoded_payload=NULL;
  uint32 batch_duration=0;
  static uint32 batch_id = 1, raw_seq = 1;
  vector<uint32> seq_arr;
  vector<RawPktSendStatus> status_vec;

  uint8 *pkt = new uint8[PKT_SIZE];
  AthCodeHeader *hdr = (AthCodeHeader*)pkt;

  assert(rate_arr.size() == encoder_.n());

  for (int j = 0; j < encoder_.n(); j++) {
    uint16 send_len=0;
    uint16 rate = rate_arr[j];
    hdr->SetHeader(raw_seq++, batch_id, encoder_.start_seq(), ATH_CODE, j, encoder_.k(), encoder_.n(), encoder_.lens());
    hdr->SetRate(rate);
    assert(encoder_.PopPkt(&encoded_payload, &send_len));
    memcpy(hdr->GetPayloadStart(), encoded_payload, send_len);
    send_len += hdr->GetFullHdrLen();
    uint32 pkt_duration = (send_len * 8.0) / (rate / 10.0) + extra_wait_time;  /** in us.*/
#if 0
    /** Update the sending time of each data packet to determine retransmission. */
    if (j == 0) {  /** Before send the first packet, udpate the timing for the entire batch.*/
      batch_duration = pkt_duration * encoder_.n();
      encoder_.GetSeqArr(seq_arr);  /** Get the sequence number of all the packets in this batch.*/
      data_pkt_buf_.UpdateBatchSendTime(batch_duration, seq_arr);
    }
#endif

    /** Store raw packet info into the raw packet buffer. Have to track front and back separately for ACK and NACK. */
    RawPktSendStatus status(hdr->raw_seq(), hdr->GetRate(), send_len, RawPktSendStatus::kUnknown);
    Laptop laptops[] = {kFront, kBack};
    for (int i = 0; i < sizeof(laptops)/sizeof(laptops[0]); i++) {
      if (laptops[i] == kFront)
        continue;  // No front laptop for now
      FeedbackHandler *feedback_handler = GetFeedbackHandler(laptops[i]); 
      feedback_handler->raw_pkt_buf_.PushPktStatus(status_vec, status);
      InsertFeedback(laptops[i], status_vec);
    }

    if (is_duplicate && j < encoder_.k()) { /** only duplicate data packets + 1 redundant packet.*/
#ifdef RAND_DROP
      hdr->set_is_good(true);
#endif
      tun_.Write(Tun::kCellular, (char*)hdr, send_len);
      //printf("Duplicate: raw_seq: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d length: %u\n", 
      hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len);
    }

#ifdef RAND_DROP
    if (IsDrop() || ((hdr->raw_seq() > 20000 && hdr->raw_seq() < 20040) || (hdr->raw_seq() > 20050 && hdr->raw_seq() < 25000))) { 
    //if (IsDrop(drop_cnt, drop_inds, j)) {
      hdr->set_is_good(false); 
      //printf("Bad pkt: raw_seq: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d length: %u rate: %u\n", 
      hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len, hdr->GetRate());
    }
    else { 
      hdr->set_is_good(true); 
      //printf("Good pkt: raw_seq: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d length: %u rate: %u\n", 
      hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len, hdr->GetRate());
    }
#else 
    //printf("Send: raw_seq: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d length: %u rate: %u\n", 
      hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len, hdr->GetRate());
#endif
    tun_.Write(Tun::kWspace, (char*)hdr, send_len);
    //not_drop = false;

    /** Flow control.*/
    usleep(pkt_duration);
    //printf("pkt_duration: %u\n", pkt_duration);
  }

  batch_id++;
  delete[] pkt;
}

void* WspaceAP::TxSendAth(void* arg) {
  enum State {
    kHandleNewPkt = 1, 
    kHandlePartialBatch,   /** Timeout (batch_time_out), send whatever packets are available. */
    kHandleRetransmission, 
    kHandleEncoding, 
  };

  uint32 seq_num, index;
  uint16 len;
  Status pkt_status;
  uint8 *buf_addr=NULL;
  uint8 num_retrans=0;
  bool handle_retransmission = false, is_timeout = false;
  int coding_pkt_cnt = 0;
  State state = kHandleNewPkt;
  int k_local=-1, n_local=-1;   
  uint16 pkt_size=0;
  vector<uint16> rate_arr;
  uint32 kExtraWaitTime = DIFS_80211ag + SLOT_TIME * 3; 
  bool is_duplicate_cell = false;

  while (1) {
    //printf("TxSendAth:: state[%d]\n", int(state));
    switch (state) {
      case kHandleNewPkt:
        is_timeout = data_pkt_buf_.DequeuePkt(batch_time_out_, &seq_num, &len, &pkt_status, &num_retrans, &index, &buf_addr);
        if (is_timeout) { 
          state = kHandlePartialBatch;
          break;
        } 
        if (pkt_status == kOccupiedNew) {
          if (coding_pkt_cnt == 0) {  /** First packet */
            pkt_size = ATH_CODE_HEADER_SIZE + MAX_BATCH_SIZE * sizeof(uint16) + len;
            scout_rate_maker_.MakeDecision(ScoutRateAdaptation::kData, coherence_time_, pkt_size, 
                    kExtraWaitTime, k_local, n_local, rate_arr, is_duplicate_cell);
            encoder_.SetCodeInfo(k_local, n_local, seq_num);
          }
          //if (is_duplicate_cell) data_pkt_buf_.DisableRetransmission(index);
          /** Copy the packet for encoding. */
          assert(encoder_.PushPkt(len, buf_addr));
          coding_pkt_cnt++;
          if (coding_pkt_cnt == k_local)
            state = kHandleEncoding;
          else
            state = kHandleNewPkt;  /** keep pushing packet into the batch. */
        }
        else if (pkt_status == kOccupiedRetrans) {
          handle_retransmission = true;
          state = kHandlePartialBatch;  /** First finish encoding the current batch and then retransmit this packet.*/
        }
        else {  /** For Empty packet or untimed out packets - only happens in retransmission. */
          state = kHandleNewPkt; /** Move on to the next slot.*/
        }
        break;

      case kHandlePartialBatch:
        if (coding_pkt_cnt > 0) { /** Check batch timeout where not a single packet is available.*/
          /** Change k to coding_pkt_cnt - send whatever is available. */
          k_local = coding_pkt_cnt;
          scout_rate_maker_.MakeDecision(ScoutRateAdaptation::kTimeOut, 0, 0, 0, k_local, n_local, rate_arr, is_duplicate_cell);
          encoder_.SetCodeInfo(k_local, n_local);  /** the start sequence number has not changed. */
          state = kHandleEncoding;
        }
        else {  /** the current batch is empty. */
          if (handle_retransmission)
            state = kHandleRetransmission;
          else
            state = kHandleNewPkt;
        }
        break;

      case kHandleRetransmission:  /** Retransmit one packet at a time. */
        /** Include the retransmited packet as the only packet in this batch. */
        handle_retransmission = false;
        k_local = 1;
        pkt_size = ATH_CODE_HEADER_SIZE + MAX_BATCH_SIZE * sizeof(uint16) + len;
        scout_rate_maker_.MakeDecision(ScoutRateAdaptation::kRetrans, coherence_time_, pkt_size, 
                kExtraWaitTime, k_local, n_local, rate_arr, is_duplicate_cell);
        encoder_.SetCodeInfo(k_local, n_local, seq_num);  /** Sequence number of the retransmitted packet.*/
        //if (is_duplicate_cell) data_pkt_buf_.DisableRetransmission(index);
        assert(encoder_.PushPkt(len, buf_addr));  
        coding_pkt_cnt++;
        /** Duplicate packets over the cellular if this is the last retransmission.*/
        //if (pkt_status == kOccupiedRetrans && num_retrans == 0) not_drop = true;
        state = kHandleEncoding;
        break;

      case kHandleEncoding:
        assert(coding_pkt_cnt > 0);
        encoder_.EncodeBatch();
#ifdef RAND_DROP
        int drop_cnt, *drop_inds;
        GetDropInds(&drop_cnt, &drop_inds);
        //printf("drop_cnt: %d\n", drop_cnt);
        SendCodedBatch(kExtraWaitTime, is_duplicate_cell, rate_arr, drop_cnt, drop_inds);
        if (drop_inds)
          delete[] drop_inds;
#else
        SendCodedBatch(kExtraWaitTime, is_duplicate_cell, rate_arr);
#endif
        coding_pkt_cnt = 0;
        encoder_.ClearInfo();
        if (handle_retransmission) {
          state = kHandleRetransmission;  /** Retransmit the lost packet.*/
        }
        else {
          state = kHandleNewPkt;
        }
        break;

      default:
        Perror("TxSendAth invalid");
    }
  }
  return (void*)NULL;
}

bool WspaceAP::HandleAck(char type, uint32 ack_seq, uint16 num_nacks, uint32 end_seq, uint32* nack_arr) {
  uint32 index=0, head_pt=0, curr_pt=0, tail_pt=0, head_pt_final=0, curr_pt_final=0;
  uint32 seq_num=0;
  uint16 nack_cnt=0, len=0; 
  uint8 num_retrans=0;
  Status stat;
  TIME start, end;
  static uint32 expect_ack_seq=1;
  static int dup_ack_cnt = 0;

  if (ack_seq < expect_ack_seq)  /** Out of order acks.*/
    return false;
  else
    expect_ack_seq = ack_seq+1;

  if (end_seq == 0)  /** Pocking for the first batch.*/
    return false;

  data_pkt_buf_.LockQueue();
  head_pt = data_pkt_buf_.head_pt();
  curr_pt = data_pkt_buf_.curr_pt();
  tail_pt = data_pkt_buf_.tail_pt();
  head_pt_final = head_pt;
  curr_pt_final = curr_pt;

#ifdef TEST
  // Calculate loss of ack
  TIME curr;
  curr.GetCurrTime();  
  //printf("ack_seq: %u end_seq: %u\n", ack_seq, end_seq);
  static uint32 ack_loss_cnt=0;
  if (expect_ack_seq != ack_seq) {
    ack_loss_cnt += (ack_seq-expect_ack_seq);
    //printf("{Loss ACK: %lf [%u] ", (curr-g_start)/1000., ack_seq-expect_ack_seq);
    for (uint32 i = expect_ack_seq; i < ack_seq; i++) {
      //printf("%u ", i);
    }
    //printf("\n");
  }
#endif

  //PrintNackInfo(type, ack_seq, num_nacks, end_seq, nack_arr);

  if (end_seq-1 < head_pt) {  // dup ack
    //printf("DUP ACK end_seq[%u] head_pt[%u]\n", end_seq, head_pt);
    data_pkt_buf_.UnLockQueue();
    dup_ack_cnt++;
    if (dup_ack_cnt >= kMaxDupAckCnt) { 
      dup_ack_cnt = 0;
      contiguous_time_out_ = max_contiguous_time_out_;
      return true;
    }
    else
      return false; 
  } 

  dup_ack_cnt = 0;
  contiguous_time_out_ = 0;

  if (num_nacks == 0) {
    head_pt_final = end_seq; // point to the first unacked/acked pkt
    // No need to change curr without retrans
  }
  else if (num_nacks > 0) {  // Need to retransmit
    while (nack_arr[nack_cnt]-1 < head_pt_final) {
      nack_cnt++;
      if (nack_cnt == num_nacks)
        break;
    }
    if (nack_cnt < num_nacks) {
      if (nack_arr[nack_cnt]-1 > head_pt_final) {
        head_pt_final = nack_arr[nack_cnt]-1;
      }
    }
  }

  for (index = head_pt; index < head_pt_final; index++) {
    uint32 index_mod = index % BUF_SIZE;    
    data_pkt_buf_.LockElement(index_mod);
    data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
    data_pkt_buf_.UnLockElement(index_mod);
  }

  bool IsFirstUpdate = true;
  //printf("HandleAck head_pt[%u] cur_pt[%u] tail_pt[%u]\n", head_pt, curr_pt, tail_pt);
  if (num_nacks > 0) {    //handle nacked packets if any 
    end.GetCurrTime();
    for (index = head_pt_final; index <= end_seq-1; index++) {
      uint32 index_mod = index % BUF_SIZE;
      data_pkt_buf_.LockElement(index_mod);
      data_pkt_buf_.GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
      if (stat == kOccupiedOutbound) { 
        if (nack_cnt < num_nacks) {
          if (index+1 == nack_arr[nack_cnt]) {  // NACK (packet is lost)
            double interval = (end - start) / 1000.;  // in ms
            if (num_retrans == 0) {
              //printf("HandleAck: Giveup pkt[%u] interval[%gms] rtt[%dms]\n", 
                nack_arr[nack_cnt], interval, rtt_);
              if (head_pt_final == index) {
                head_pt_final++; //  reclaim buffer
              }
              data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
            }
            else if (interval > rtt_ || num_retrans == num_retrans_) {  // Timeout or first retrans
              data_pkt_buf_.GetElementStatus(index_mod) = kOccupiedRetrans;
              //printf("HandleAck: Retransmit pkt[%u] num_retrans[%u] interval[%gms] rtt[%dms]\n", 
                nack_arr[nack_cnt], num_retrans, interval, rtt_);
              if (IsFirstUpdate) {
                IsFirstUpdate = false;
                curr_pt_final = index;  // start retrans from here
              } 
            }
            nack_cnt++;
          }
          else {  // The packet is received (holes) 
            //printf("Receive[%u]\n", index+1);
            data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
            if (head_pt_final == index) {
              head_pt_final++; //  reclaim buffer
            }
          }
        }
        else {  // The packet is received
          //printf("Receive[%u]\n", index+1);
          data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
          if (head_pt_final == index) {
            head_pt_final++; //  reclaim buffer
          }
        }
      }
      else if (stat == kEmpty) {  
        if (nack_cnt < num_nacks) {
          if ((nack_arr[nack_cnt]-1) % BUF_SIZE == index_mod) {  // Pkts already dropped
            nack_cnt++;
          } 
        }
        if (head_pt_final == index) {
          head_pt_final++; //  reclaim buffer
        }
      }
      else {  // kOccupiedNew kOccupiedRetrans
        if (nack_cnt < num_nacks) {
          if (nack_arr[nack_cnt] == seq_num) {
            if (IsFirstUpdate) {
              IsFirstUpdate = false;
              curr_pt_final = index;  // start retrans from here
            } 
            nack_cnt++;
          }
          else { 
            data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
            if (head_pt_final == index) {
              head_pt_final++; //  reclaim buffer
            }
          }
        }
        else {
          data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
          if (head_pt_final == index) {
            head_pt_final++; //  reclaim buffer
          }
        }
      }
      data_pkt_buf_.UnLockElement(index_mod);
    }
    assert(nack_cnt == num_nacks);
  }

  /** Check for packet timeout after end_seq.*/
  /*
  for (index = end_seq; index <= curr_pt; index++) {
    uint32 index_mod = index % BUF_SIZE;
    data_pkt_buf_.LockElement(index_mod);
    data_pkt_buf_.GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
    double interval = (end - start) / 1000.;  // in ms
    double timeout_interval = rtt_ + coherence_time_/1000. * 1.5;
    if (stat == kOccupiedOutbound && interval > timeout_interval) {  // Timeout or first retrans
      data_pkt_buf_.GetElementStatus(index_mod) = kOccupiedRetrans;
      printf("HandleAck: Timeout Retransmit pkt[%u] num_retrans[%u] interval[%gms] timeout_interval[%gms]\n", 
      seq_num, num_retrans, interval, timeout_interval);
      if (IsFirstUpdate) {
        IsFirstUpdate = false;
        curr_pt_final = index;  // start retrans from here
      } 
    }
    data_pkt_buf_.UnLockElement(index_mod);
  }
  */

  if (curr_pt_final < head_pt_final) {
    curr_pt_final = head_pt_final;
  }
  if (head_pt_final > head_pt) {
    data_pkt_buf_.set_head_pt(head_pt_final);
    data_pkt_buf_.SignalEmpty();
  }
  if (curr_pt_final != curr_pt) {
    data_pkt_buf_.set_curr_pt(curr_pt_final);
    data_pkt_buf_.SignalFill();
  }
  data_pkt_buf_.UnLockQueue();
  return false;
}

void WspaceAP::HandleTimeOut() {
  uint32 index=0, head_pt=0, curr_pt=0, tail_pt=0, head_pt_final=0, curr_pt_final=0;
  TIME start, end; 
  uint32 seq_num=0;
  uint16 len=0;
  uint8 num_retrans=0;
  Status stat;
  vector<RawPktSendStatus> status_vec;
  
  data_pkt_buf_.LockQueue();
  head_pt = data_pkt_buf_.head_pt();
  curr_pt = data_pkt_buf_.curr_pt();
  tail_pt = data_pkt_buf_.tail_pt();

  bool IsFirstUpdate=true;
  head_pt_final = head_pt;
  curr_pt_final = curr_pt;
  end.GetCurrTime();
  //printf("timeout head_pt[%u] curr_pt[%u]\n", head_pt, curr_pt);
  bool increment_time_out = false;

  for (index = head_pt; index < tail_pt; index++) {
    uint32 index_mod = index % BUF_SIZE;    
    data_pkt_buf_.LockElement(index_mod);
    data_pkt_buf_.GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
    if (stat == kOccupiedRetrans) {  /** Haven't finished this round of retransmission. */
      if (IsFirstUpdate) {
        assert(seq_num>=1);
        curr_pt_final = seq_num - 1;  // Retransmission starts from the first timeout pkt
        IsFirstUpdate = false;
      }
    }
    else if (stat == kOccupiedOutbound) {
      double interval = (end - start)/1000.;
      if (num_retrans == 0) {  //  Have to drop this packet
        if (head_pt_final == seq_num-1) {
          head_pt_final++; //  reclaim buffer
        }
        data_pkt_buf_.UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
        //printf("HandleTimeOut: Drop pkt[%u] interval[%gms] rtt[%dms]\n", 
            seq_num, interval, rtt_);
      }
      else if (interval > rtt_ * 1.2) {  // set up retransmission
        increment_time_out = true;  /** Ack timeout and there is timeouted packet. */
        if (IsFirstUpdate) {
          assert(seq_num>=1);
          curr_pt_final = seq_num - 1;  // Retransmission starts from the first timeout pkt
          IsFirstUpdate = false;
        }
        data_pkt_buf_.GetElementStatus(index_mod) = kOccupiedRetrans;
        //printf("HandleTimeOut: Retransmit pkt[%u] num_retrans[%u] interval[%gms] rtt[%dms]\n", 
            seq_num, num_retrans, interval, rtt_);
      }
    }
    else if (stat == kEmpty) {
      // See whether we can reclaim more storage
      if (head_pt_final == index) {
        head_pt_final++; 
      }
    }
    data_pkt_buf_.UnLockElement(index_mod);
    if (stat == kOccupiedNew) break;
  }
  if (curr_pt_final < head_pt_final) {
    curr_pt_final = head_pt_final;
  }
  if (head_pt_final > head_pt) {
    data_pkt_buf_.set_head_pt(head_pt_final);
    data_pkt_buf_.SignalEmpty();
  }
  if (curr_pt_final != curr_pt) {
    data_pkt_buf_.set_curr_pt(curr_pt_final);
    data_pkt_buf_.SignalFill();
  }
  data_pkt_buf_.UnLockQueue();

  /** No need the lock to guard between HandleAck and HandleTimeOut because they are serialized. */
  if (increment_time_out)
    contiguous_time_out_++;
  else if (contiguous_time_out_ < max_contiguous_time_out_)
    contiguous_time_out_ = 0;  

  if (contiguous_time_out_ >= max_contiguous_time_out_) {
    //printf("HandleTimeOut: set high loss contiguous_time_out_[%d]\n", contiguous_time_out_);
    contiguous_time_out_ = 0;

    scout_rate_maker_.SetHighLoss();
    Laptop laptops[] = {kFront, kBack};
    for (int i = 0; i < sizeof(laptops)/sizeof(laptops[0]); i++) {
      if (laptops[i] == kFront)  // No front laptop for now
        continue;
      FeedbackHandler *feedback_handler = GetFeedbackHandler(laptops[i]); 
      feedback_handler->raw_pkt_buf_.ClearPktStatus(status_vec, true);
      InsertFeedback(laptops[i], status_vec);
    }
  }
}
  
void* WspaceAP::TxHandleDataAck(void *) {
  uint16 num_nacks=0;
  uint32 ack_seq=0, end_seq=0;
  uint32 *nack_seq_arr = new uint32[ACK_WINDOW];
  char type;
  bool is_ack_available=false; 
  bool dup_ack_timeout = false;
  while (1) {
    is_ack_available = TxHandleAck(data_ack_context_, &type, &ack_seq, &num_nacks, &end_seq, nack_seq_arr);
    if (is_ack_available) {
      dup_ack_timeout = HandleAck(type, ack_seq, num_nacks, end_seq, nack_seq_arr);
      if (dup_ack_timeout) { 
        //printf("Dup ack timeout! contiguous_time_out[%d]\n", contiguous_time_out_); 
        HandleTimeOut();
      }
    }
    else {
      HandleTimeOut();
    }
  }
  delete[] nack_seq_arr;
}

FeedbackHandler* WspaceAP::GetFeedbackHandler(Laptop laptop) {
  FeedbackHandler *feedback_handler;
  if (laptop == kFront)
    feedback_handler = &front_handler_;
  else if (laptop == kBack)
    feedback_handler = &back_handler_;
  else
    Perror("TxHandleRawAck: Invalid laptop[%d]\n", laptop);
  return feedback_handler;
}

void WspaceAP::InsertFeedback(Laptop laptop, const vector<RawPktSendStatus> &status_vec) {
  vector<RawPktSendStatus>::const_iterator it;

  if (status_vec.empty())
    return;

  for (it = status_vec.begin(); it < status_vec.end(); it++) {
/*
    if (laptop == kFront)
      printf("Front_ack ");
    else
      printf("Back_ack ");
    printf("InsertRecord raw_seq[%u] status[%d] rate[%u] len[%u] time[%.3fms]\n", 
      it->seq_, PacketStatus(it->status_), it->rate_, it->len_, it->send_time_.GetMSec());
*/
      scout_rate_maker_.InsertFeedback(laptop, it->seq_, PacketStatus(it->status_), it->rate_, it->len_, it->send_time_);
  }

  /** Ensure close range lookup [start, end] for delayed packets.*/
  MonotonicTimer start = status_vec.front().send_time_ - MonotonicTimer(0, 1);
  MonotonicTimer end = status_vec.back().send_time_ + MonotonicTimer(0, 1);
  scout_rate_maker_.CalcLossRates(laptop, start, end);  /** Calculate loss for delayed feedback. */


  SendLossRate(laptop, client_ids_.front()); // TODO: Enable dynamically assignment of client_id.

}

void* WspaceAP::TxHandleRawAck(void* arg) {
  char type;
  uint16 num_nacks=0, num_pkts=0;
  uint32 ack_seq=0, end_seq=0;
  uint32 *nack_seq_arr = new uint32[ACK_WINDOW];
  vector<RawPktSendStatus> status_vec;
  Laptop laptop; 
  static uint32 expect_ack_seq = 1;
    
  char *pkt_type = (char*)arg;  /** For starting two threads to handle front and back ACKs separately. */

  if (*pkt_type == RAW_FRONT_ACK)
    laptop = kFront;
  else if (*pkt_type == RAW_BACK_ACK)
    laptop = kBack;
  else
    Perror("TxHandleRawAck: Invalid pkt_type[%d]\n", *pkt_type);
  FeedbackHandler *feedback_handler = GetFeedbackHandler(laptop); 

  while (1) {
    bool is_ack_available = TxHandleAck(feedback_handler->raw_ack_context_, &type, &ack_seq, 
              &num_nacks, &end_seq, nack_seq_arr, &num_pkts);
    assert(is_ack_available);  /** No timeout when handling raw ACKs. */
    //PrintNackInfo(type, ack_seq, num_nacks, end_seq, nack_seq_arr, num_pkts);
    if (ack_seq >= expect_ack_seq) {
      expect_ack_seq = ack_seq + 1;
      feedback_handler->raw_pkt_buf_.PopPktStatus(end_seq, num_nacks, num_pkts, nack_seq_arr, status_vec);
      InsertFeedback(laptop, status_vec);  /** For scout. */
    }
    //else
    //  printf("Warning: out of order raw ack seq[%u] expect_seq[%u]\n", ack_seq, expect_ack_seq);
  }
  delete[] nack_seq_arr;
}

bool WspaceAP::TxHandleAck(AckContext &ack_context, char *type, uint32 *ack_seq, uint16 *num_nacks,
        uint32 *end_seq, uint32 *nack_seq_arr, uint16 *num_pkts) {
  char pkt_type;
  ack_context.Lock();
  while (!ack_context.ack_available()) {
    if (ack_context.type() == DATA_ACK) {
      int err = ack_context.WaitFill(ack_time_out_);
      if (err == ETIMEDOUT)
        break;
    }
    else if (ack_context.type() == RAW_FRONT_ACK || ack_context.type() == RAW_BACK_ACK) {
      ack_context.WaitFill();  /** No need to timeout to track raw packets. */
    }
    else {
      Perror("TxHandleAck invalid ACK type: %d\n", ack_context.type());
    }
  }
  bool ack_available = ack_context.ack_available();
  if (ack_context.ack_available()) {
    ack_context.set_ack_available(false);
    (ack_context.pkt())->ParseNack(&pkt_type, ack_seq, num_nacks, end_seq, nack_seq_arr, num_pkts);  
    assert(pkt_type == ack_context.type());
    *type = pkt_type;
  }
  ack_context.SignalEmpty();
  ack_context.UnLock();
  return ack_available;
}

void* WspaceAP::TxRcvCell(void* arg) {
  uint16 nread=0;
  char *buf = new char[PKT_SIZE];
  while (1) {
    nread = tun_.Read(Tun::kCellular, buf, PKT_SIZE);
    char type = *buf;
    if (type == CELL_DATA) {
      tun_.Write(Tun::kControl, buf, nread);
    }
    else if (type == DATA_ACK) {
      RcvAck(data_ack_context_, buf, nread);
    }
    else if (type == RAW_FRONT_ACK) {
      RcvAck(front_handler_.raw_ack_context_, buf, nread);
    }
    else if (type == RAW_BACK_ACK) {
      RcvAck(back_handler_.raw_ack_context_, buf, nread);
    }
    else if (type == GPS) {
      RcvGPS(buf, nread);
    }
    else if (type == CONTROLLER_TO_CLIENT) {
      data_pkt_buf_.EnqueuePkt(nread, (uint8*)buf);
    }
    else {
      Perror("TxRcvCell: Invalid pkt type[%d]\n", type);
    }
  }
  delete[] buf;
}

void WspaceAP::RcvAck(AckContext &ack_context, const char* buf, uint16 len) {
  ack_context.Lock();
  while (ack_context.ack_available()) {
    /** Last ack has not been processed yet. */
    //printf("TxRcvCell ACK overlaps!\n");
    ack_context.SignalFill();
    ack_context.WaitEmpty();
  }
  ack_context.set_ack_available(true);
  memcpy(ack_context.pkt(), buf, len);
  ack_context.SignalFill();
  ack_context.UnLock();
}

void WspaceAP::RcvGPS(const char* buf, uint16 len) {
  static uint32 prev_seq = 0;
  assert(len == GPS_HEADER_SIZE);
  const GPSHeader *hdr = (const GPSHeader*)buf;
  if (hdr->seq() > prev_seq) {
    prev_seq = hdr->seq();
    gps_logger_.LogGPSInfo(*hdr);
    //scout_rate_maker_.set_speed(hdr->speed());
    scout_rate_maker_.set_speed(0.0);
  }
  else {
    //printf("RcvGPS: Warning seq[%u] <= prev_seq[%u]\n", hdr->seq(), prev_seq);
  }
}

#ifdef RAND_DROP
void WspaceAP::GetDropInds(int *drop_cnt, int **inds) {
  int n = encoder_.n();
  *drop_cnt = ceil(n * drop_prob_/100.);
  int rand_ind=-1;
  if (*drop_cnt == 0) {
    *inds = NULL;
  }
  else {
    int *ind_arr = new int[n];
    for (int i = 0; i < n; i++)
      ind_arr[i] = i;
    for (int i = 0; i < *drop_cnt; i++) {
      rand_ind = i + rand() % (n-i);
      SWAP(ind_arr[rand_ind], ind_arr[i], int);
    }
    *inds = ind_arr;
  }
}
#endif

void* LaunchTxReadTun(void* arg) {
  wspace_ap->TxReadTun(arg);
}

void* LaunchTxSendAth(void* arg) {
  wspace_ap->TxSendAth(arg);
}

void* LaunchTxHandleDataAck(void* arg) {
  wspace_ap->TxHandleDataAck(arg);
}

void* LaunchTxHandleRawAck(void* arg) {
  wspace_ap->TxHandleRawAck(arg);
}

void* LaunchTxRcvCell(void* arg) {
  wspace_ap->TxRcvCell(arg);
}

void PrintNackInfo(char type, uint32 ack_seq, uint16 num_nacks, uint32 end_seq, uint32 *nack_arr, uint16 num_pkts) {
  if (type == DATA_ACK)
    printf("data_");
  else if (type == RAW_FRONT_ACK)
    printf("raw_front_");
  else if (type == RAW_BACK_ACK)
    printf("raw_back_");
  printf("ack[%u] end_seq[%u] num_nacks[%u] num_pkts[%u] {", ack_seq, end_seq, num_nacks, num_pkts);
  for (int i = 0; i < num_nacks; i++) {
    printf("%d ", nack_arr[i]);
  }
  printf("}\n");
}
