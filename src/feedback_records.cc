#include "feedback_records.h"

FeedbackRecords::FeedbackRecords(const int32_t *rate_arr, int32_t num_rates, MonotonicTimer t /*=MonotonicTimer(2 , 0)*/) {
  duration_ = t;
  for(int i = 0; i < num_rates; ++i) {
    uint16_t rate = (uint16_t)rate_arr[i];
    struct RateInfo info = {0, 0};
    rate_table_[rate] = info;
  }
  Pthread_mutex_init(&lock_, NULL);
}

FeedbackRecords::~FeedbackRecords() {
  Pthread_mutex_destroy(&lock_);
}

void FeedbackRecords::SetWindowSize(MonotonicTimer t) {
  Lock();
  duration_ = t; 
  UnLock();
}

bool FeedbackRecords::InsertPacket(uint32_t seq, PacketStatus status, uint16_t rate, MonotonicTimer time /*=MonotonicTimer(0, 0)*/) {
  /*assert legal packet status*/
  assert(status==kACKed || status==kNAKed || status==kTimeOut);
  /*Insert this packet if it is legal*/
  MonotonicTimer now;
  if (time.GetMSec()) now = time;
  PacketInfo pkt = {now, seq, rate, 0, status};
  Lock();
  records_.push_back(pkt);
  /*remove the old packets*/
  bool bb = 0;
  while(!records_.empty()) {
    pkt = records_.front();
    if(pkt.timer + duration_ >= now)
      break;
    records_.pop_front();
    bb = 1;  
  }
  UnLock();
  return bb;
}

int32_t FeedbackRecords::BinarySearchRecords(const MonotonicTimer& t) {
  int32_t sz = records_.size();
  assert(sz > 0);
  if(t > records_[sz-1].timer)
    return sz;
  if(t < records_[0].timer)
    return 0;
  int32_t left = 0, right = sz - 1;
  int32_t mid;
  while(left<right) {
    mid = left + (right - left)/2;
    if(records_[mid].timer == t)
      return mid;
    else if(records_[mid].timer > t)
      right = mid;
    else
      left = mid + 1;
  }
  return right;
}  

bool FeedbackRecords::CalcLossRates(const MonotonicTimer& start, const MonotonicTimer& end, 
          const vector<uint16_t>& rates, vector<double>& loss_rate, bool is_print) {
  /*ensure legal input*/
  assert(start <= end);
  
  Lock();
  int32_t sz = records_.size();
  int32_t num = rates.size();

  if (sz == 0 || end < records_[0].timer || start > records_[sz-1].timer) {
    UnLock();
    return false;
  }

  ResetRateTable();
  /*traverse the records_ to get the records in the [start, end] range*/
  int32_t left = BinarySearchRecords(start);
  int32_t right = BinarySearchRecords(end);
  //cout<<left<<"\t"<<right<<endl;
  for(int i = left; i < right; ++i) {
    uint16_t rate = records_[i].rate; 
    rate_table_[rate].n_sent_++;
    if(records_[i].status==kACKed)
      rate_table_[rate].n_ack_++;
    if (is_print) {
      printf("FeedbackRecord::CalcLossRates: time[%.3f] seq_num[%u] rate[%u] length[%u] status[%d]\n", 
      records_[i].timer.GetMSec(), records_[i].seq_num, records_[i].rate, records_[i].length, records_[i].status);
    }
  }
  loss_rate.clear();
  for(int i = 0; i < num; ++i) {
    uint16_t rate = rates[i];
    if(rate_table_[rate].n_sent_==0)
      loss_rate.push_back(-1.0);
    else
      loss_rate.push_back(1.0 - (double)rate_table_[rate].n_ack_/(double)rate_table_[rate].n_sent_);
    /*printf("---CalcLossRates rate[%u] n_sent[%u] n_ack_[%u] loss[%g]\n", 
      rate, rate_table_[rate].n_sent_, rate_table_[rate].n_ack_, loss_rate.back());*/
  }
  UnLock();
  return true;
}

void FeedbackRecords::CalcLossRates(const MonotonicTimer& start, const MonotonicTimer& end, 
          uint16_t rate, double *loss_rate) {
  vector<uint16_t> rates;
  vector<double> loss_rates;
  rates.push_back(rate);
  bool ret = CalcLossRates(start, end, rates, loss_rates);
  if (!ret)
    *loss_rate = -1.0;
  else
    *loss_rate = loss_rates[0];
}

bool FeedbackRecords::FindSuccessRates(const MonotonicTimer& start, const MonotonicTimer& end, 
          vector<uint16_t>& rates, bool is_loss_available) {
  const uint32_t kMinSentCnt = 2;
  assert(start <= end);
  rates.clear();
  Lock();
  int32_t sz = records_.size();
  /*ensure legal input*/
  if (sz == 0 || end < records_[0].timer || start > records_[sz-1].timer) {
    UnLock();
    return false;
  }
  if (!is_loss_available) {
    /*clear the rate table*/
    ResetRateTable();
    int32_t left = BinarySearchRecords(start);
    int32_t right = BinarySearchRecords(end);
    //cout<<left<<"\t"<<right<<endl;
    for(int i = left; i < right; ++i) {
      uint16_t rate = records_[i].rate; 
      rate_table_[rate].n_sent_++;
      if(records_[i].status==kACKed)
        rate_table_[rate].n_ack_++;
//      printf("FeedbackRecord::FindSuccessRates: time[%.3f] seq_num[%u] rate[%u] length[%u] status[%d]\n", 
//      records_[i].timer.GetMSec(), records_[i].seq_num, records_[i].rate, records_[i].length, records_[i].status);
    }
  }
  map<uint16_t, struct RateInfo>::iterator it_map;
  for (it_map = rate_table_.begin(); it_map != rate_table_.end(); it_map++) {
    if (it_map->second.n_ack_ > 0 || it_map->second.n_sent_ < kMinSentCnt)
      rates.push_back(it_map->first);
  }
  UnLock();
  return true;
}

void FeedbackRecords::ResetRateTable() {
  map<uint16_t, struct RateInfo>::iterator it_map;
  for (it_map = rate_table_.begin(); it_map != rate_table_.end(); it_map++) {
    it_map->second.n_sent_ = 0;
    it_map->second.n_ack_ = 0;
  }
}

void FeedbackRecords::ClearRecords() {
  Lock();
  records_.clear();
  UnLock();
}

void FeedbackRecords::PrintRecords() {
  Lock();
  int32_t sz = records_.size();
  cout << "No" << "\t" << "Time(ms)" << "Seq" << "\t" << "Rate" << "\t" << "Status" << endl;
  for(int32_t i = 0; i < sz; ++i) {
    printf("%d \t %.3f \t %u \t %u \t %d\n", i+1, records_[i].timer.GetMSec(), records_[i].seq_num,
        records_[i].rate, int(records_[i].status));
  }
  UnLock();
}

