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
  printf("sizeof(BSStatsPkt):%d\n", sizeof(BSStatsPkt));
  printf("sizeof(ControllerToClientHeader):%d\n", sizeof(ControllerToClientHeader));
  printf("sizeof(CellDataHeader):%d\n", sizeof(CellDataHeader));
  printf("sizeof(double):%d\n",sizeof(double));
  printf("sizeof(int):%d\n",sizeof(int));
  const char* opts = "r:R:t:T:i:I:S:s:C:c:P:p:r:B:b:d:D:V:v:m:M:O:f:n:o:F:";
  wspace_ap = new WspaceAP(argc, argv, opts);
  wspace_ap->Init();

  Pthread_create(&wspace_ap->p_tx_read_tun_, NULL, LaunchTxReadTun, NULL);
  Pthread_create(&wspace_ap->p_tx_rcv_cell_, NULL, LaunchTxRcvCell, NULL);
  Pthread_create(&wspace_ap->p_tx_send_probe_, NULL, LaunchTxSendProbe, NULL);
  for(vector<int>::iterator it = wspace_ap->client_ids_.begin(); it != wspace_ap->client_ids_.end(); ++it) {
    Pthread_create(wspace_ap->client_context_tbl_[*it]->p_tx_send_ath(), NULL, LaunchTxSendAth, &(*it));
    Pthread_create(wspace_ap->client_context_tbl_[*it]->p_tx_handle_raw_ack(), NULL, LaunchTxHandleRawAck, &(*it));
    Pthread_create(wspace_ap->client_context_tbl_[*it]->p_tx_handle_data_ack(), NULL, LaunchTxHandleDataAck, &(*it));
  }
#ifdef RAND_DROP
  if (wspace_ap->use_loss_trace_) {
    Pthread_create(&wspace_ap->p_tx_update_loss_rates_, NULL, LaunchUpdateLossRates, NULL);
  }
#endif

  Pthread_join(wspace_ap->p_tx_read_tun_, NULL);
  Pthread_join(wspace_ap->p_tx_rcv_cell_, NULL);
  Pthread_join(wspace_ap->p_tx_send_probe_, NULL);
  for(vector<int>::iterator it = wspace_ap->client_ids_.begin(); it != wspace_ap->client_ids_.end(); ++it) {
    Pthread_join(*(wspace_ap->client_context_tbl_[*it]->p_tx_send_ath()), NULL);
    Pthread_join(*(wspace_ap->client_context_tbl_[*it]->p_tx_handle_raw_ack()), NULL);
    Pthread_join(*(wspace_ap->client_context_tbl_[*it]->p_tx_handle_data_ack()), NULL);
  }
#ifdef RAND_DROP
  if (wspace_ap->use_loss_trace_) {
    Pthread_join(wspace_ap->p_tx_update_loss_rates_, NULL);
  }
#endif
  delete wspace_ap;
  return 0;
}

WspaceAP::WspaceAP(int argc, char *argv[], const char *optstring) 
    : num_retrans_(0), coherence_time_(0), max_contiguous_time_out_(5),
      probe_pkt_size_(10), probing_interval_(1000000) {
#ifdef RAND_DROP
  use_loss_trace_ = false;
  packet_drop_manager_ = new PacketDropManager(mac80211abg_rate, mac80211abg_num_rates);
#endif
  int option;
  uint16 rate;
  bool use_fec = true;
  RateAdaptVersion rate_adapt_version;
  bool enable_duplicate = true;

  while ((option = getopt(argc, argv, optstring)) > 0) {
    switch(option) {
      case 'R':  /** Number of retransmission. */
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting data_pkt_buf_ of client_context_tbl_\n");
        num_retrans_ = (uint8)atoi(optarg);
        for (map<int, ClientContext*>::iterator it = client_context_tbl_.begin(); it != client_context_tbl_.end(); ++it) {
          it->second->data_pkt_buf()->set_num_retrans(num_retrans_);
        }
        printf("NUM_RETRANS: %d\n", num_retrans_);
        break;
      case 'r': {  /** For data rate*/
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting scout_rate_maker_ of client_context_tbl_\n");
        string s;
        stringstream ss(optarg);
        vector<int>::iterator it = client_ids_.begin();
        int count = 1;
        while(getline(ss, s, ',')) {
          if(count > client_context_tbl_.size())
            Perror("Too many input rate.\n");
          int rate = atoi(s.c_str());
          client_context_tbl_[*it]->scout_rate_maker()->set_rate(rate);
          printf("Rate: %gMbps\n", rate/10.);
          ++it;
          ++count;
        }
        break;
      }
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
        bs_id_ = atoi(optarg);
        printf("bs_id_: %d\n", bs_id_);
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
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting scout_rate_maker_ of client_context_tbl_\n");
        for (map<int, ClientContext*>::iterator it = client_context_tbl_.begin(); it != client_context_tbl_.end(); ++it) {
          it->second->scout_rate_maker()->set_use_fec(use_fec);
        }
        printf("Use fec: %d\n", use_fec);
        break;
      case 'v':
        rate_adapt_version = (RateAdaptVersion)atoi(optarg);
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting scout_rate_maker_ of client_context_tbl_\n");
        for (map<int, ClientContext*>::iterator it = client_context_tbl_.begin(); it != client_context_tbl_.end(); ++it) {
          it->second->scout_rate_maker()->set_rate_adapt_version(rate_adapt_version);
          it->second->scout_rate_maker()->PrintRateAdaptVersion();
        }
        break;
      case 'O':
        enable_duplicate = (bool)atoi(optarg);
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting scout_rate_maker_ of client_context_tbl_\n");
        for (map<int, ClientContext*>::iterator it = client_context_tbl_.begin(); it != client_context_tbl_.end(); ++it) {
          it->second->scout_rate_maker()->set_enable_duplicate(enable_duplicate);
          printf("enable_duplicate[%d] Threshold for duplicating over cellular: %g\n", 
            it->second->scout_rate_maker()->enable_duplicate(), it->second->scout_rate_maker()->duplicate_thresh());
        }
        break;
      case 'f':
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting gps_logger_ of client_context_tbl_\n");
        for (map<int, ClientContext*>::iterator it = client_context_tbl_.begin(); it != client_context_tbl_.end(); ++it) {
          it->second->gps_logger()->ConfigFile(optarg);
        }
        printf("GPS log file: %s\n", client_context_tbl_.begin()->second->gps_logger()->filename().c_str());
        break;
      case 'n':
        max_contiguous_time_out_ = atoi(optarg);
        printf("max_contiguous_time_out: %d\n", max_contiguous_time_out_);
        assert(max_contiguous_time_out_ > 0);
        break;
#ifdef RAND_DROP
      case 'd': {
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting drop_prob_ of client_context_tbl_\n");
        string s;
        stringstream ss(optarg);
        vector<double> loss;
        while(getline(ss, s, ',')) {
          double p = atof(s.c_str());
          if(p > 1 || p < 0)
            Perror("Invalid random drop probability.\n");
          printf("Packet corrupt probability: %3f\n", p);
          loss.push_back(p);
        }
        packet_drop_manager_->ParseLossRates(loss, client_ids_);
        break;
      }
      case 'D': {
        if ( client_ids_.size() == 0 )
          Perror("Need to set client ids before setting drop ratio trace file of client_context_tbl_\n");
        use_loss_trace_ = true;
        string s;
        stringstream ss(optarg);
        vector<string> input_files;
        while(getline(ss, s, ',')) {
          if(input_files.size() >= client_context_tbl_.size())
            Perror("Too many input files.\n");
          input_files.push_back(s);
        }
        packet_drop_manager_->ParseLossRates(input_files, client_ids_);
        break;
      }
#endif
      case 'c': {
        string addr;
        stringstream ss(optarg);
        while(getline(ss, addr, ',')) {
          if(atoi(addr.c_str()) == 1)
              Perror("id 1 is reserved by controller\n");
          client_ids_.push_back(atoi(addr.c_str()));
          client_context_tbl_[atoi(addr.c_str())] = new ClientContext;
        }
        break;
      }
      case 'b': {
        ParseIP(client_ids_, tun_.client_ip_tbl_);
        break;
      }
      case 'o': {
        probe_pkt_size_ = atoi(optarg);
        assert(probe_pkt_size_ < PKT_SIZE);
        break;
      }
      case 'F': {
        probing_interval_ = atoi(optarg);
        break;
      }
      default:
        Perror("Usage: %s -i tun0/tap0 -S server_eth_ip -s server_ath_ip -C client_eth_ip -c client_ath_ip -m tcp/udp\n", argv[0]);
    }
  }

  assert(tun_.if_name_[0] && tun_.broadcast_ip_ath_[0] && tun_.server_ip_eth_[0] && tun_.server_ip_ath_[0] && tun_.controller_ip_eth_[0] && bs_id_ && tun_.client_ip_tbl_.size());
  for (map<int, string>::iterator it = tun_.client_ip_tbl_.begin(); it != tun_.client_ip_tbl_.end(); ++it) {
    assert(strlen(it->second.c_str()));
  }
  assert(coherence_time_ > 0);
#ifdef RAND_DROP
  srand(time(NULL));
#endif
}

WspaceAP::~WspaceAP() {
  for (vector<int>::iterator it = client_ids_.begin(); it != client_ids_.end(); ++it) {
    delete client_context_tbl_[*it];
  }
#ifdef RAND_DROP
  delete packet_drop_manager_;
#endif
}

void WspaceAP::Init() {
  tun_.Init();
}

void WspaceAP::ParseIP(const vector<int> &ids, map<int, string> &ip_table) {
  if (ids.empty()) {
    Perror("WspaceAP::ParseIP: Need to indicate ids first!\n");
  }
  vector<int>::const_iterator it = ids.begin();
  string addr;
  stringstream ss(optarg);
  while(getline(ss, addr, ',')) {
    if (it == ids.end())
      Perror("WspaceAP::ParseIP: Too many input addresses\n");
    int id = *it;
    ip_table[id] = addr;
    ++it;
  }
}

void WspaceAP::SendLossRate(int client_id) {
  char type = BS_STATS;
  double throughput = 0;
  double loss_rate = 0;
  double th = 0;
  LossMap *loss_map = client_context_tbl_[client_id]->scout_rate_maker()->GetLossMap(ScoutRateAdaptation::kBack);
  //printf("Update loss rate\n");
  for (int i = 0; i < mac80211abg_num_rates; i++) {
    loss_rate = loss_map->GetLossRate(mac80211abg_rate[i]);
    if(loss_rate == INVALID_LOSS_RATE)
      continue;
    th = mac80211abg_rate[i] * (1 - loss_rate) / 10.0;
    //printf("mac80211abg_rate[%d], loss_rate:%3f\n", mac80211abg_rate[i], loss_rate);
    if(th > throughput)
      throughput = th;
  }
  BSStatsPkt pkt;
  pkt.Init(++client_context_tbl_[client_id]->bsstats_seq_, bs_id_, client_id, throughput);
  //pkt.Print();
  tun_.Write(Tun::kControl, (char *)&pkt, sizeof(pkt));
}


void* WspaceAP::TxReadTun(void* arg) {
  return (void*)NULL;
}

//bool not_drop = false;

void WspaceAP::SendCodedBatch(uint32 extra_wait_time, bool is_duplicate, const vector<uint16> &rate_arr, int client_id,
        int drop_cnt, int *drop_inds) {
  uint8 *encoded_payload=NULL;
  uint32 batch_duration=0;
  vector<uint32> seq_arr;
  vector<RawPktSendStatus> status_vec;

  uint8 *pkt = new uint8[PKT_SIZE];
  AthCodeHeader *hdr = (AthCodeHeader*)pkt;

  assert(rate_arr.size() == client_context_tbl_[client_id]->encoder()->n());

  for (int j = 0; j < client_context_tbl_[client_id]->encoder()->n(); j++) {
    uint16 send_len=0;
    uint16 rate = rate_arr[j];
    hdr->SetHeader(client_context_tbl_[client_id]->raw_seq_++, client_context_tbl_[client_id]->batch_id_, client_context_tbl_[client_id]->encoder()->start_seq(), ATH_CODE, j, client_context_tbl_[client_id]->encoder()->k(), client_context_tbl_[client_id]->encoder()->n(), client_context_tbl_[client_id]->encoder()->lens(), bs_id_, client_id);
    hdr->SetRate(rate);
    assert(client_context_tbl_[client_id]->encoder()->PopPkt(&encoded_payload, &send_len));
    memcpy(hdr->GetPayloadStart(), encoded_payload, send_len);
    send_len += hdr->GetFullHdrLen();
    uint32 pkt_duration = (send_len * 8.0) / (rate / 10.0) + extra_wait_time;  /** in us.*/
#if 0
    /** Update the sending time of each data packet to determine retransmission. */
    if (j == 0) {  /** Before send the first packet, udpate the timing for the entire batch.*/
      batch_duration = pkt_duration * client_context_tbl_[client_id]->encoder()->n();
      client_context_tbl_[client_id]->encoder()->GetSeqArr(seq_arr);  /** Get the sequence number of all the packets in this batch.*/
      client_context_tbl_[client_id]->data_pkt_buf()->UpdateBatchSendTime(batch_duration, seq_arr);
    }
#endif

    /** Store raw packet info into the raw packet buffer. */
    RawPktSendStatus status(hdr->raw_seq(), hdr->GetRate(), send_len, RawPktSendStatus::kUnknown);
    client_context_tbl_[client_id]->feedback_handler()->raw_pkt_buf_.PushPktStatus(status_vec, status);
    InsertFeedback(status_vec, client_id);


    if (is_duplicate && j < client_context_tbl_[client_id]->encoder()->k()) { // only duplicate data packets + 1 redundant packet.
#ifdef RAND_DROP
      hdr->set_is_good(true);
#endif
      tun_.Write(Tun::kControl, (char*)hdr, send_len, client_id);/*
      printf("Duplicate: client_id %d pkt_type:%d raw_seq_: %u batch_id_: %u seq_num: %u start_seq: %u coding_index: %d length: %u\n", 
      client_id, (char*)hdr->GetPayloadStart()[0], hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len);*/
    }


#ifdef RAND_DROP
    if (IsDrop(client_id, rate) /*|| ((hdr->raw_seq() > 20000 && hdr->raw_seq() < 20040) || (hdr->raw_seq() > 20050 && hdr->raw_seq() < 25000))*/) { 
    //if (IsDrop(drop_cnt, drop_inds, j)) {
      hdr->set_is_good(false); /*
      printf("Bad pkt: client_id: %d pkt_type:%d raw_seq_: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d length: %u rate: %u\n", client_id,  (char*)hdr->GetPayloadStart()[0], hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len, hdr->GetRate());*/
    }
    else { 
      hdr->set_is_good(true); /*
      printf("Good pkt: client_id: %d pkt_type:%d raw_seq_: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d length: %u rate: %u\n", client_id,  (char*)hdr->GetPayloadStart()[0], hdr->raw_seq(), hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len, hdr->GetRate());*/
    }
#else /*
    printf("Send: client_context_tbl_[%d]->raw_seq_: %u client_context_tbl_[%d]->batch_id_: %u seq_num: %u start_seq: %u coding_index: %d length: %u rate: %u\n", client_id, hdr->raw_seq(), client_id, hdr->batch_id(), hdr->start_seq_ + hdr->ind_, hdr->start_seq_, hdr->ind_, send_len, hdr->GetRate());*/
#endif
    //printf("send_len: %d\n", send_len);
    tun_.Write(Tun::kWspace, (char*)hdr, send_len);
    //not_drop = false;

    /** Flow control.*/
    //usleep(pkt_duration);
    //printf("pkt_duration: %u\n", pkt_duration);
  }

  client_context_tbl_[client_id]->batch_id_++;
  delete[] pkt;
}

void* WspaceAP::TxSendAth(void* arg) {
  int *client_id = (int*)arg;
  printf("TxSendAth start, client_id:%d\n", *client_id);
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
        is_timeout = client_context_tbl_[*client_id]->data_pkt_buf()->DequeuePkt(batch_time_out_, &seq_num, &len, &pkt_status, &num_retrans, &index, &buf_addr);
        if (is_timeout) { 
          state = kHandlePartialBatch;
          break;
        } 
        if (pkt_status == kOccupiedNew) {
          if (coding_pkt_cnt == 0) {  /** First packet */
            pkt_size = ATH_CODE_HEADER_SIZE + MAX_BATCH_SIZE * sizeof(uint16) + len;
            client_context_tbl_[*client_id]->scout_rate_maker()->MakeDecision(ScoutRateAdaptation::kData, coherence_time_, pkt_size, 
                    kExtraWaitTime, k_local, n_local, rate_arr, is_duplicate_cell);
            client_context_tbl_[*client_id]->encoder()->SetCodeInfo(k_local, n_local, seq_num);
          }
          //if (is_duplicate_cell) client_context_tbl_[*client_id]->data_pkt_buf()->DisableRetransmission(index);
          /** Copy the packet for encoding. */
          assert(client_context_tbl_[*client_id]->encoder()->PushPkt(len, buf_addr));
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
          client_context_tbl_[*client_id]->scout_rate_maker()->MakeDecision(ScoutRateAdaptation::kTimeOut, 0, 0, 0, k_local, n_local, rate_arr, is_duplicate_cell);
          client_context_tbl_[*client_id]->encoder()->SetCodeInfo(k_local, n_local);  /** the start sequence number has not changed. */
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
        client_context_tbl_[*client_id]->scout_rate_maker()->MakeDecision(ScoutRateAdaptation::kRetrans, coherence_time_, pkt_size, 
                kExtraWaitTime, k_local, n_local, rate_arr, is_duplicate_cell);
        client_context_tbl_[*client_id]->encoder()->SetCodeInfo(k_local, n_local, seq_num);  /** Sequence number of the retransmitted packet.*/
        //if (is_duplicate_cell) client_context_tbl_[*client_id]->data_pkt_buf()->DisableRetransmission(index);
        assert(client_context_tbl_[*client_id]->encoder()->PushPkt(len, buf_addr));  
        coding_pkt_cnt++;
        /** Duplicate packets over the cellular if this is the last retransmission.*/
        //if (pkt_status == kOccupiedRetrans && num_retrans == 0) not_drop = true;
        state = kHandleEncoding;
        break;

      case kHandleEncoding:
        assert(coding_pkt_cnt > 0);
        client_context_tbl_[*client_id]->encoder()->EncodeBatch();
#ifdef RAND_DROP
/*
        int drop_cnt, *drop_inds;
        GetDropInds(&drop_cnt, &drop_inds, *client_id);
        //printf("drop_cnt: %d\n", drop_cnt);
        SendCodedBatch(kExtraWaitTime, is_duplicate_cell, rate_arr, *client_id, drop_cnt, drop_inds);
        if (drop_inds)
          delete[] drop_inds;
*/
        SendCodedBatch(kExtraWaitTime, is_duplicate_cell, rate_arr, *client_id);
#else
        SendCodedBatch(kExtraWaitTime, is_duplicate_cell, rate_arr, *client_id);
#endif
        coding_pkt_cnt = 0;
        client_context_tbl_[*client_id]->encoder()->ClearInfo();
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

void* WspaceAP::TxSendProbe(void* arg) {
  char buf[PKT_SIZE] = {0};
  *buf = ATH_PROBE;
  while(1) {
    char* pkt_content = new char[probe_pkt_size_];
    memcpy(buf + 1, pkt_content, probe_pkt_size_);
    for(vector<int>::iterator it = wspace_ap->client_ids_.begin(); it != wspace_ap->client_ids_.end(); ++it) {
      client_context_tbl_[*it]->data_pkt_buf()->EnqueuePkt(probe_pkt_size_ + 1, (uint8*)buf);
    }
    delete pkt_content;
    usleep(probing_interval_);
  }
}

bool WspaceAP::HandleDataAck(char type, uint32 ack_seq, uint16 num_nacks, uint32 end_seq, uint32* nack_arr, int client_id) {
  uint32 index=0, head_pt=0, curr_pt=0, tail_pt=0, head_pt_final=0, curr_pt_final=0;
  uint32 seq_num=0;
  uint16 nack_cnt=0, len=0; 
  uint8 num_retrans=0;
  Status stat;
  TIME start, end;


  if (ack_seq < client_context_tbl_[client_id]->expect_data_ack_seq_)  /** Out of order acks.*/
    return false;
  else
    client_context_tbl_[client_id]->expect_data_ack_seq_ = ack_seq+1;

  if (end_seq == 0)  /** Pocking for the first batch.*/
    return false;

  client_context_tbl_[client_id]->data_pkt_buf()->LockQueue();
  head_pt = client_context_tbl_[client_id]->data_pkt_buf()->head_pt();
  curr_pt = client_context_tbl_[client_id]->data_pkt_buf()->curr_pt();
  tail_pt = client_context_tbl_[client_id]->data_pkt_buf()->tail_pt();
  head_pt_final = head_pt;
  curr_pt_final = curr_pt;

#ifdef TEST
  // Calculate loss of ack
  TIME curr;
  curr.GetCurrTime();  
  //printf("ack_seq: %u end_seq: %u\n", ack_seq, end_seq);
  if (client_context_tbl_[client_id]->expect_data_ack_seq_ != ack_seq) {
    client_context_tbl_[client_id]->data_ack_loss_cnt_ += (ack_seq - client_context_tbl_[client_id]->expect_data_ack_seq_); /*
    printf("{Loss ACK: %lf [%u] ", (curr-g_start)/1000., ack_seq - client_context_tbl_[client_id]->expect_data_ack_seq_);
    for (uint32 i = client_context_tbl_[client_id]->expect_data_ack_seq_; i < ack_seq; i++) {
      printf("%u ", i);
    }
    printf("\n"); */
  }
#endif

  //PrintNackInfo(type, ack_seq, num_nacks, end_seq, nack_arr);

  if (end_seq-1 < head_pt) {  // dup ack
    //printf("DUP ACK end_seq[%u] head_pt[%u]\n", end_seq, head_pt);
    client_context_tbl_[client_id]->data_pkt_buf()->UnLockQueue();
    client_context_tbl_[client_id]->dup_data_ack_cnt_++;
    if (client_context_tbl_[client_id]->dup_data_ack_cnt_ >= kMaxDupAckCnt) { 
      client_context_tbl_[client_id]->dup_data_ack_cnt_ = 0;
      client_context_tbl_[client_id]->contiguous_time_out_ = max_contiguous_time_out_;
      return true;
    }
    else
      return false; 
  } 

  client_context_tbl_[client_id]->dup_data_ack_cnt_ = 0;
  client_context_tbl_[client_id]->contiguous_time_out_ = 0;

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
    client_context_tbl_[client_id]->data_pkt_buf()->LockElement(index_mod);
    client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
    client_context_tbl_[client_id]->data_pkt_buf()->UnLockElement(index_mod);
  }

  bool IsFirstUpdate = true;
  //printf("HandleDataAck head_pt[%u] cur_pt[%u] tail_pt[%u]\n", head_pt, curr_pt, tail_pt);
  if (num_nacks > 0) {    //handle nacked packets if any 
    end.GetCurrTime();
    for (index = head_pt_final; index <= end_seq-1; index++) {
      uint32 index_mod = index % BUF_SIZE;
      client_context_tbl_[client_id]->data_pkt_buf()->LockElement(index_mod);
      client_context_tbl_[client_id]->data_pkt_buf()->GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
      if (stat == kOccupiedOutbound) { 
        if (nack_cnt < num_nacks) {
          if (index+1 == nack_arr[nack_cnt]) {  // NACK (packet is lost)
            double interval = (end - start) / 1000.;  // in ms
            if (num_retrans == 0) {
              /*printf("HandleDataAck: Giveup pkt[%u] interval[%gms] rtt[%dms]\n", 
                nack_arr[nack_cnt], interval, rtt_);*/
              if (head_pt_final == index) {
                head_pt_final++; //  reclaim buffer
              }
              client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
            }
            else if (interval > rtt_ || num_retrans == num_retrans_) {  // Timeout or first retrans
              client_context_tbl_[client_id]->data_pkt_buf()->GetElementStatus(index_mod) = kOccupiedRetrans;
              /*printf("HandleDataAck: Retransmit pkt[%u] num_retrans[%u] interval[%gms] rtt[%dms]\n", 
                nack_arr[nack_cnt], num_retrans, interval, rtt_);*/
              if (IsFirstUpdate) {
                IsFirstUpdate = false;
                curr_pt_final = index;  // start retrans from here
              } 
            }
            nack_cnt++;
          }
          else {  // The packet is received (holes) 
            //printf("Receive[%u]\n", index+1);
            client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
            if (head_pt_final == index) {
              head_pt_final++; //  reclaim buffer
            }
          }
        }
        else {  // The packet is received
          //printf("Receive[%u]\n", index+1);
          client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
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
            client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
            if (head_pt_final == index) {
              head_pt_final++; //  reclaim buffer
            }
          }
        }
        else {
          client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
          if (head_pt_final == index) {
            head_pt_final++; //  reclaim buffer
          }
        }
      }
      client_context_tbl_[client_id]->data_pkt_buf()->UnLockElement(index_mod);
    }
    if (nack_cnt != num_nacks) {
      //PrintNackInfo(type, ack_seq, num_nacks, end_seq, nack_arr);
      printf("WARNING: HandleDataAck client_id[%d] nack_cnt[%d] != num_nacks[%d]\n", client_id, nack_cnt, num_nacks);
    }
    //assert(nack_cnt == num_nacks);
  }

  /** Check for packet timeout after end_seq.*/
  /*
  for (index = end_seq; index <= curr_pt; index++) {
    uint32 index_mod = index % BUF_SIZE;
    client_context_tbl_[client_id]->data_pkt_buf()->LockElement(index_mod);
    client_context_tbl_[client_id]->data_pkt_buf()->GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
    double interval = (end - start) / 1000.;  // in ms
    double timeout_interval = rtt_ + coherence_time_/1000. * 1.5;
    if (stat == kOccupiedOutbound && interval > timeout_interval) {  // Timeout or first retrans
      client_context_tbl_[client_id]->data_pkt_buf()->GetElementStatus(index_mod) = kOccupiedRetrans;
      printf("HandleDataAck: Timeout Retransmit pkt[%u] num_retrans[%u] interval[%gms] timeout_interval[%gms]\n", 
      seq_num, num_retrans, interval, timeout_interval);
      if (IsFirstUpdate) {
        IsFirstUpdate = false;
        curr_pt_final = index;  // start retrans from here
      } 
    }
    client_context_tbl_[client_id]->data_pkt_buf()->UnLockElement(index_mod);
  }
  */

  if (curr_pt_final < head_pt_final) {
    curr_pt_final = head_pt_final;
  }
  if (head_pt_final > head_pt) {
    client_context_tbl_[client_id]->data_pkt_buf()->set_head_pt(head_pt_final);
    client_context_tbl_[client_id]->data_pkt_buf()->SignalEmpty();
  }
  if (curr_pt_final != curr_pt) {
    client_context_tbl_[client_id]->data_pkt_buf()->set_curr_pt(curr_pt_final);
    client_context_tbl_[client_id]->data_pkt_buf()->SignalFill();
  }
  client_context_tbl_[client_id]->data_pkt_buf()->UnLockQueue();
  return false;
}

void WspaceAP::HandleTimeOut(int client_id) {
  uint32 index=0, head_pt=0, curr_pt=0, tail_pt=0, head_pt_final=0, curr_pt_final=0;
  TIME start, end; 
  uint32 seq_num=0;
  uint16 len=0;
  uint8 num_retrans=0;
  Status stat;
  vector<RawPktSendStatus> status_vec;

  client_context_tbl_[client_id]->data_pkt_buf()->LockQueue();
  head_pt = client_context_tbl_[client_id]->data_pkt_buf()->head_pt();
  curr_pt = client_context_tbl_[client_id]->data_pkt_buf()->curr_pt();
  tail_pt = client_context_tbl_[client_id]->data_pkt_buf()->tail_pt();

  bool IsFirstUpdate=true;
  head_pt_final = head_pt;
  curr_pt_final = curr_pt;
  end.GetCurrTime();
  //printf("timeout head_pt[%u] curr_pt[%u] tail_pt[%u]\n", head_pt, curr_pt, tail_pt);
  bool increment_time_out = false;

  for (index = head_pt; index < tail_pt; index++) {
    uint32 index_mod = index % BUF_SIZE;    
    client_context_tbl_[client_id]->data_pkt_buf()->LockElement(index_mod);
    client_context_tbl_[client_id]->data_pkt_buf()->GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
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
        client_context_tbl_[client_id]->data_pkt_buf()->UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
        /*printf("HandleTimeOut: Drop pkt[%u] interval[%gms] rtt[%dms]\n", 
            seq_num, interval, rtt_);*/
      }
      else if (interval > rtt_ * 1.2) {  // set up retransmission
        increment_time_out = true;  /** Ack timeout and there is timeouted packet. */
        if (IsFirstUpdate) {
          assert(seq_num>=1);
          curr_pt_final = seq_num - 1;  // Retransmission starts from the first timeout pkt
          IsFirstUpdate = false;
        }
        client_context_tbl_[client_id]->data_pkt_buf()->GetElementStatus(index_mod) = kOccupiedRetrans;
        /*printf("HandleTimeOut: Retransmit pkt[%u] num_retrans[%u] interval[%gms] rtt[%dms]\n", 
            seq_num, num_retrans, interval, rtt_);*/
      }
    }
    else if (stat == kEmpty) {
      // See whether we can reclaim more storage
      if (head_pt_final == index) {
        head_pt_final++; 
      }
    }
    client_context_tbl_[client_id]->data_pkt_buf()->UnLockElement(index_mod);
    if (stat == kOccupiedNew) break;
  }
  if (curr_pt_final < head_pt_final) {
    curr_pt_final = head_pt_final;
  }
  if (head_pt_final > head_pt) {
    client_context_tbl_[client_id]->data_pkt_buf()->set_head_pt(head_pt_final);
    client_context_tbl_[client_id]->data_pkt_buf()->SignalEmpty();
  }
  if (curr_pt_final != curr_pt) {
    client_context_tbl_[client_id]->data_pkt_buf()->set_curr_pt(curr_pt_final);
    client_context_tbl_[client_id]->data_pkt_buf()->SignalFill();
  }
  client_context_tbl_[client_id]->data_pkt_buf()->UnLockQueue();

  /** No need the lock to guard between HandleDataAck and HandleTimeOut because they are serialized. */
  if (increment_time_out)
    client_context_tbl_[client_id]->contiguous_time_out_++;
  else if (client_context_tbl_[client_id]->contiguous_time_out_ < max_contiguous_time_out_)
    client_context_tbl_[client_id]->contiguous_time_out_ = 0;  

  //printf("contiguous_time_out_:%d, max_contiguous_time_out_:%d\n", client_context_tbl_[client_id]->contiguous_time_out_, max_contiguous_time_out_);
  if (client_context_tbl_[client_id]->contiguous_time_out_ >= max_contiguous_time_out_) {
    //printf("HandleTimeOut: set high loss client[%d] contiguous_time_out_[%d]\n", client_id, client_context_tbl_[client_id]->contiguous_time_out_);
    client_context_tbl_[client_id]->contiguous_time_out_ = 0;

    client_context_tbl_[client_id]->scout_rate_maker()->SetHighLoss();
    client_context_tbl_[client_id]->feedback_handler()->raw_pkt_buf_.ClearPktStatus(status_vec, true);
    InsertFeedback(status_vec, client_id);
  }
}
  
void* WspaceAP::TxHandleDataAck(void *arg) {
  int *client_id = (int*)arg;
  printf("TxHandleDataAck start, client_id:%d\n", *client_id);
  uint16 num_nacks=0;
  uint32 ack_seq=0, end_seq=0;
  uint32 *nack_seq_arr = new uint32[ACK_WINDOW];
  char type;
  bool is_ack_available=false; 
  bool dup_ack_timeout = false;
  int bs_id = 0;
  while (1) {
    is_ack_available = TxHandleAck(*(client_context_tbl_[*client_id]->data_ack_context()), &type, &ack_seq, &num_nacks, &end_seq, *client_id, &bs_id, nack_seq_arr);
    if (is_ack_available) {
      dup_ack_timeout = HandleDataAck(type, ack_seq, num_nacks, end_seq, nack_seq_arr, *client_id);
      if (dup_ack_timeout) { 
        //printf("Dup ack timeout! client_context_tbl_[%d]->contiguous_time_out_[%d]\n", *client_id, client_context_tbl_[*client_id]->contiguous_time_out_); 
        HandleTimeOut(*client_id);
      }
    }
    else {
      HandleTimeOut(*client_id);
    }
  }
  delete[] nack_seq_arr;
}

void WspaceAP::InsertFeedback(const vector<RawPktSendStatus> &status_vec, int client_id) {
  vector<RawPktSendStatus>::const_iterator it;

  if (status_vec.empty())
    return;

  for (it = status_vec.begin(); it < status_vec.end(); it++) {/*
    printf("InsertRecord raw_seq[%u] status[%d] rate[%u] len[%u] time[%.3fms]\n", 
      it->seq_, PacketStatus(it->status_), it->rate_, it->len_, it->send_time_.GetMSec());*/
    client_context_tbl_[client_id]->scout_rate_maker()->InsertFeedback(ScoutRateAdaptation::kBack, it->seq_, PacketStatus(it->status_), it->rate_, it->len_, it->send_time_);
  }

  /** Ensure close range lookup [start, end] for delayed packets.*/
  MonotonicTimer start = status_vec.front().send_time_ - MonotonicTimer(0, 1);
  MonotonicTimer end = status_vec.back().send_time_ + MonotonicTimer(0, 1);
  client_context_tbl_[client_id]->scout_rate_maker()->CalcLossRates(ScoutRateAdaptation::kBack, start, end);  /** Calculate loss for delayed feedback. */

  SendLossRate(client_id);

}

void* WspaceAP::TxHandleRawAck(void* arg) {
  int *client_id = (int*)arg;
  printf("TxHandleRawAck start, client_id:%d\n", *client_id);
  char type;
  uint16 num_nacks=0, num_pkts=0;
  uint32 ack_seq=0, end_seq=0;
  uint32 *nack_seq_arr = new uint32[ACK_WINDOW];
  vector<RawPktSendStatus> status_vec;
  int bs_id = 0;

  while (1) {
    bool is_ack_available = TxHandleAck(client_context_tbl_[*client_id]->feedback_handler()->raw_ack_context_, &type, &ack_seq, 
              &num_nacks, &end_seq, *client_id, &bs_id, nack_seq_arr, &num_pkts);
    assert(is_ack_available);  /** No timeout when handling raw ACKs. */
    //PrintNackInfo(type, ack_seq, num_nacks, end_seq, nack_seq_arr, num_pkts);
    if (ack_seq >= client_context_tbl_[*client_id]->expect_raw_ack_seq_) {
      client_context_tbl_[*client_id]->expect_raw_ack_seq_ = ack_seq + 1;
      client_context_tbl_[*client_id]->feedback_handler()->raw_pkt_buf_.PopPktStatus(end_seq, num_nacks, num_pkts, nack_seq_arr, status_vec);
      InsertFeedback(status_vec, *client_id);  /** For scout. */
    }/*
    else
      printf("Warning: out of order raw ack seq[%u] expect_seq[%u]\n", ack_seq, client_context_tbl_[*client_id]->expect_raw_ack_seq_);*/
  }
  delete[] nack_seq_arr;
}

bool WspaceAP::TxHandleAck(AckContext &ack_context, char *type, uint32 *ack_seq, uint16 *num_nacks,
        uint32 *end_seq, int client_id, int* bs_id, uint32 *nack_seq_arr, uint16 *num_pkts) {
  int client = 0;
  char pkt_type;
  ack_context.Lock();
  while (!ack_context.ack_available()) {
    if (ack_context.type() == DATA_ACK) {
      int err = ack_context.WaitFill(ack_time_out_);
      if (err == ETIMEDOUT)
        break;
    }
    else if (ack_context.type() == RAW_ACK) {
      ack_context.WaitFill();  /** No need to timeout to track raw packets. */
    }
    else {
      Perror("TxHandleAck invalid ACK type: %d\n", ack_context.type());
    }
  }
  bool ack_available = ack_context.ack_available();
  if (ack_context.ack_available()) {
    ack_context.set_ack_available(false);
    (ack_context.pkt())->ParseNack(&pkt_type, ack_seq, num_nacks, end_seq, &client, bs_id, nack_seq_arr, num_pkts);
    assert(client == client_id && *bs_id == bs_id_);
    //printf("TxHandleAck: pkt_type:%d, client_id:%d, bs_id:%d\n", (int)pkt_type, client_id, *bs_id);
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
      AckHeader *hdr = (AckHeader*)buf;
      RcvAck(*(client_context_tbl_[hdr->client_id()]->data_ack_context()), buf, nread);
    }
    else if (type == RAW_ACK) {
      AckHeader *hdr = (AckHeader*)buf;
      RcvAck(client_context_tbl_[hdr->client_id()]->feedback_handler()->raw_ack_context_, buf, nread);
    }
    else if (type == GPS) {
      GPSHeader *hdr = (GPSHeader*)buf;
      RcvGPS(buf, nread, hdr->client_id());
    }
    else if (type == CONTROLLER_TO_CLIENT) {
      ControllerToClientHeader* hdr = (ControllerToClientHeader*)buf;
      //printf("CONTROLLER_TO_CLIENT pkt client_id: %d seq_num: %u len: %u\n", hdr->client_id(), hdr->o_seq(), nread);
      client_context_tbl_[hdr->client_id()]->data_pkt_buf()->EnqueuePkt(nread, (uint8*)buf);
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

void WspaceAP::RcvGPS(const char* buf, uint16 len, int client_id) {
  assert(len == GPS_HEADER_SIZE);
  const GPSHeader *hdr = (const GPSHeader*)buf;
  if (hdr->seq() > client_context_tbl_[client_id]->prev_gps_seq_) {
    client_context_tbl_[client_id]->prev_gps_seq_ = hdr->seq();
    client_context_tbl_[client_id]->gps_logger()->LogGPSInfo(*hdr);
    //client_context_tbl_[client_id]->scout_rate_maker()->set_speed(hdr->speed());
    client_context_tbl_[client_id]->scout_rate_maker()->set_speed(0.0);
  }/*
  else {
    printf("RcvGPS: Warning seq[%u] <= client_context_tbl_[%d]->prev_gps_seq_[%u]\n", hdr->seq(), client_id, client_context_tbl_[client_id]->prev_gps_seq_);
  }*/
}

#ifdef RAND_DROP
void* WspaceAP::UpdateLossRates(void* arg) {
  sleep(1);
  while (true) {/*
    for(vector<int>::iterator it = client_ids_.begin(); it != client_ids_.end(); ++it) {
      printf("UpdateLossRate::client[%d]: ", *it);
      for(int i = 0; i < mac80211abg_num_rates; ++i) {
        double loss = 0;
        packet_drop_manager_->GetLossRate(*it, mac80211abg_rate[i], &loss);
        printf("%3f ", loss);
      }
      printf("\n");
    }*/
    if (!packet_drop_manager_->PopLossRates()) {
      printf("WspaceAP::UpdateLossRates:Running out of data in packet loss trace.\n");
      assert(false);
    }
    sleep(1);
  }
}

bool WspaceAP::IsDrop(int client_id, uint16 rate) {
  double loss_rate = 0;
  if( packet_drop_manager_->GetLossRate(client_id, (int)rate, &loss_rate)) {
    bool drop = rand() % 100 /100.0 < loss_rate;
    return drop;
  } else {
    return false;
  }
}

/*
void WspaceAP::GetDropInds(int *drop_cnt, int **inds, int client_id) {
  int n = client_context_tbl_[client_id]->encoder()->n();
  *drop_cnt = ceil(n * client_context_tbl_[client_id]->drop_prob_);
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
*/
void* LaunchUpdateLossRates(void* arg) {
  wspace_ap->UpdateLossRates(arg);
}
#endif

void* LaunchTxReadTun(void* arg) {
  wspace_ap->TxReadTun(arg);
}

void* LaunchTxSendAth(void* arg) {
  wspace_ap->TxSendAth(arg);
}

void* LaunchTxSendProbe(void* arg) {
  wspace_ap->TxSendProbe(arg);
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
  else if (type == RAW_ACK)
    printf("raw_");
  printf("ack[%u] end_seq[%u] num_nacks[%u] num_pkts[%u] {", ack_seq, end_seq, num_nacks, num_pkts);
  for (int i = 0; i < num_nacks; i++) {
    printf("%d ", nack_arr[i]);
  }
  printf("}\n");
}
