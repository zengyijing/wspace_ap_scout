#include "scout_rate.h"

using namespace std;

/** LossMap */
LossMap::LossMap(const int *rate_arr, int num_rates) {
  LossInfo info = {0, 0, INVALID_LOSS_RATE};
  for (int i = 0; i < num_rates; i++)
    loss_map_[rate_arr[i]] = info;
  Pthread_mutex_init(&lock_, NULL);
}

LossMap::~LossMap() {
  Pthread_mutex_destroy(&lock_);
}

void LossMap::UpdateLoss(uint16_t rate, double loss, double previous_weight) {
  Lock();
  assert(loss_map_.count(rate) > 0);  /** Ensure rate is in the map. */
  assert(loss == INVALID_LOSS_RATE || (loss >= 0 && loss <= 1));
  assert(previous_weight <= 1);
  if (previous_weight > 0 && loss != INVALID_LOSS_RATE && loss_map_[rate].loss != INVALID_LOSS_RATE) {
    loss_map_[rate].loss = loss_map_[rate].loss * previous_weight + loss * (1 - previous_weight);
  }
  else if (previous_weight <= 0 || loss_map_[rate].loss == INVALID_LOSS_RATE/** initial phase */) {
    loss_map_[rate].loss = loss;
  }
  else { // if loss == -1, don't update and use the previous loss rate.
  }
//  if (loss_map_[rate].loss != INVALID_LOSS_RATE)
//    loss_map_[rate].loss = loss_map_[rate].loss / 2.;
  UnLock();
}

void LossMap::UpdateSendCnt(uint16_t rate, uint32_t n_sent, uint32_t n_ack) {
  Lock();
  assert(loss_map_.count(rate) > 0);  /** Ensure rate is in the map. */
  loss_map_[rate].n_sent = n_sent;
  loss_map_[rate].n_ack = n_ack;
  UnLock();
}

double LossMap::GetLossRate(uint16_t rate) {
  double loss;
  Lock();
  assert(loss_map_.count(rate) > 0);  
  loss = loss_map_[rate].loss;
  UnLock();
  return loss;
}

uint32_t LossMap::GetNSent(uint16_t rate) {
  uint32_t n_sent;  
  Lock();
  assert(loss_map_.count(rate) > 0);  
  n_sent = loss_map_[rate].n_sent;
  UnLock();
  return n_sent;
}

uint32_t LossMap::GetNAck(uint16_t rate) {
  uint32_t n_ack;  
  Lock();
  assert(loss_map_.count(rate) > 0);  
  n_ack = loss_map_[rate].n_ack;
  UnLock();
  return n_ack;
}

void LossMap::Print() {
  Lock();
  map<uint16_t, LossInfo>::const_iterator it_map;
  for (it_map = loss_map_.begin(); it_map != loss_map_.end(); it_map++)
    printf("%u\t%.3f\n", it_map->first, it_map->second.loss);
  UnLock();
}

ScoutRateAdaptation::ScoutRateAdaptation(const int32_t *rate_arr, int32_t num_rates, 
            int32_t gf_size, int32_t max_k, 
            double antenna_dist, double duplicate_thresh, double loss_bound,  
            const MonotonicTimer &front_window, 
            const MonotonicTimer &back_window, 
            const MonotonicTimer &total_window)
    : feedback_rec_front_(rate_arr, num_rates, total_window), 
    feedback_rec_back_(rate_arr, num_rates, total_window), 
    loss_map_front_(rate_arr, num_rates), loss_map_back_(rate_arr, num_rates), 
    loss_map_scout_(rate_arr, num_rates), loss_map_combine_(rate_arr, num_rates), 
    kGFSize(gf_size - kNumSampleRates), kMaxK(max_k), 
    rate_(rate_arr[0]), rate_adapt_version_(kScoutSeq), use_fec_(true), 
    rate_adapt_baseline_(NULL), speed_(-1.0), loss_bound_(loss_bound), 
    kAntennaDist(antenna_dist), duplicate_thresh_(duplicate_thresh), 
    front_window_(front_window), back_window_(back_window), enable_duplicate_(true) {
  for (int i = 0; i < num_rates; i++) {
    rate_arr_.push_back(rate_arr[i]);
    rate_ind_map_[rate_arr[i]] = i;
  }
  
  srand(time(NULL));  
}

ScoutRateAdaptation::~ScoutRateAdaptation() {
  if (rate_adapt_baseline_) delete rate_adapt_baseline_;
}


LossMap* ScoutRateAdaptation::GetLossMap(Laptop laptop) {
  if(laptop==kFront)
    return &loss_map_front_;
  else if(laptop==kBack)
    return &loss_map_back_;
  else if(laptop==kFrontScout)
    return &loss_map_scout_;
  else if(laptop==kAfterCombine)
    return &loss_map_combine_;
  else//laptop==kInvalid
    assert(0);
}


void ScoutRateAdaptation::set_rate_adapt_version(const RateAdaptVersion &version) { 
  rate_adapt_version_ = version; 
  if ((version == kSampleRate || version == kRRAA) && !rate_adapt_baseline_)
    rate_adapt_baseline_ = new RateAdaptation(version);
}

void ScoutRateAdaptation::InsertFeedback(Laptop laptop, uint32_t seq, PacketStatus status, 
          uint16_t rate, uint16_t len, const MonotonicTimer &time) {
  if (laptop == kFront)
    feedback_rec_front_.InsertPacket(seq, status, rate, time);
  else if (laptop == kBack) {
    feedback_rec_back_.InsertPacket(seq, status, rate, time);
    if (rate_adapt_version_ == kSampleRate || rate_adapt_version_ == kRRAA) {
      //printf("Baseline InsertRecord raw_seq[%u] status[%d] rate[%u] len[%u]\n", seq, status, rate, len);
      rate_adapt_baseline_->InsertRecord(seq, status, rate, len);  /** Baseline alg selects rate based on the back feedback.*/
    }
  }
  else
    Perror("ScoutRateAdaptation::InsertFeedback: invalid laptop type[%d]\n", laptop);
}

void ScoutRateAdaptation::MakeDecision(TransmitMode transmit_mode, uint32_t coherence_time, 
            uint16_t pkt_size, uint32_t extra_time, 
          int &k, int &n, vector<uint16_t> &rate_arr, bool &is_duplicate) {
  int n_no_sampling = -1;

  if (transmit_mode != kTimeOut) {
    /** Figure out the effective loss rates after packet combining.*/
    CalcLossRatesAfterCombine();  
    //PrintLossRatesAfterCombine();

    /** Make rate decision for the first packet in the batch.*/
    //if (transmit_mode == kRetrans) set_rate(rate_arr_[0]);
    //else ApplyRate();  
    ApplyRate();
  }

  /** Apply FEC. */
  ApplyFEC(transmit_mode, coherence_time, pkt_size, extra_time, k, n_no_sampling);
  
  /** Decide whether to offload to cellular. */
  if (enable_duplicate() && (IsHighLoss() || transmit_mode == kRetrans || IsBootstrapping()))
    is_duplicate = true;
  else
    is_duplicate = false;

  //printf("MakeDecision: is_duplicate %d, IsHighLoss:%d, transmit_mode:%d, IsBootstrapping():%d\n", (int)is_duplicate, (int)IsHighLoss(), (int)transmit_mode, (int)IsBootstrapping());

  //printf("MakeDecision: enable_duplicate[%d] high_loss[%d] transmit_mode[%d]\n", 
  //enable_duplicate(), IsHighLoss(), transmit_mode);

  if (is_duplicate) {
    n_no_sampling = k; /** Don't slow down the cellular link.*/
    SampleRates(1, kSampleBound);  /** Don't waste time to sample higher data rates.*/
  }
  else {
    SampleRates(kNumSampleRates, kSampleBound);
  }

  /** Construct rates for the rest of the packets in the batch. */
  FindRatesForBatch(n_no_sampling, rate_arr, n);
}

void ScoutRateAdaptation::SetLossRates(Laptop laptop, const vector<uint16_t> &rate_arr, const vector<double> &loss_arr) {
  LossMap *loss_map;
  assert(rate_arr.size() == loss_arr.size());
  size_t sz = rate_arr.size();

  switch (laptop) {
    case kFront:
      loss_map = &loss_map_front_;
      break;
    case kBack:
      loss_map = &loss_map_back_;
      break;
    case kFrontScout:
      loss_map = &loss_map_scout_;
      break;
    case kAfterCombine:
      loss_map = &loss_map_combine_;
      break;
    default:
      assert(0);
  }

  for (size_t i = 0; i < sz; i++) {
    //printf("Insert rate[%u] loss[%g]\n", rate_arr[i], loss_arr[i]);
    loss_map->UpdateLoss(rate_arr[i], loss_arr[i]);
  }
}

void ScoutRateAdaptation::SetHighLoss() {
  vector<double> loss_arr;
  for (size_t i = 0; i < rate_arr_.size(); i++) 
    loss_arr.push_back(1.0);
  SetLossRates(kFront, rate_arr_, loss_arr);
  SetLossRates(kBack, rate_arr_, loss_arr);
  SetLossRates(kFrontScout, rate_arr_, loss_arr);
}

void ScoutRateAdaptation::CalcLossRates(Laptop laptop, const MonotonicTimer &start, const MonotonicTimer &end) {
  static const double kPrevWeight = 0.2;
  FeedbackRecords *feedback_rec;
  LossMap *loss_map;
  vector<double> loss_arr;

  if (laptop == kFront) {
    feedback_rec = &feedback_rec_front_;
    loss_map = &loss_map_front_;
    //printf("CalcLossRates: front! start[%.3fms] end[%.3fms]\n", start.GetMSec(), end.GetMSec());
  }
  else if (laptop == kBack) {
    feedback_rec = &feedback_rec_back_;
    loss_map = &loss_map_back_;
    //printf("CalcLossRates: back start[%.3fms] end[%.3fms]!\n", start.GetMSec(), end.GetMSec());
  }
  else
    Perror("ScoutRateAdaptation::CalcLossRates: invalid laptop type[%d]\n", laptop);

  bool is_available = feedback_rec->CalcLossRates(start, end, rate_arr_, loss_arr);
  size_t sz = rate_arr_.size();
  for (size_t i = 0; i < sz; i++) {
    double loss = is_available ? loss_arr[i] : INVALID_LOSS_RATE;
    loss_map->UpdateLoss(rate_arr_[i], loss, kPrevWeight);
  }
}

void ScoutRateAdaptation::CalcLossRates(const MonotonicTimer &front_window, const MonotonicTimer &back_window) {
  static const double kPrevWeight = -1.0;
  bool is_available = false;
  MonotonicTimer now;
  vector<double> loss_arr;
  bool is_print = false;

  if (speed_ > 0) {
    double lookup_duration = kAntennaDist/speed_;  // in s
    MonotonicTimer time_same_loc = now - MonotonicTimer((long long)(lookup_duration * 1e9));
    MonotonicTimer start = time_same_loc - front_window/2;
    MonotonicTimer end = time_same_loc + front_window/2;
    printf("CalcLossRates: dist[%.3f] speed[%.3f] lookup[%.3fs] now[%.3f] time_same_loc[%.3f] start[%.3f] end[%.3f]\n", 
    kAntennaDist, speed_, lookup_duration, now.GetMSec(), time_same_loc.GetMSec(), start.GetMSec(), end.GetMSec());
    is_available = feedback_rec_front_.CalcLossRates(start, end, rate_arr_, loss_arr, is_print);
  }
  size_t sz = rate_arr_.size();
  for (size_t i = 0; i < sz; i++) {
    double loss = is_available ? loss_arr[i] : INVALID_LOSS_RATE;
    loss_map_scout_.UpdateLoss(rate_arr_[i], loss, kPrevWeight);
  }

  is_available = feedback_rec_back_.FindSuccessRates(now - back_window, now, feasible_rates_);
  if (!is_available)
    feasible_rates_ = rate_arr_;  /** No stats available so we can try all the data rates.*/
}

void ScoutRateAdaptation::CalcLossRatesAfterCombine() {
  double loss = INVALID_LOSS_RATE;
  double loss_front = INVALID_LOSS_RATE;
  double loss_front_delay = INVALID_LOSS_RATE;
  double loss_back = INVALID_LOSS_RATE;
  double kWeightCombine = -1.0;

  ConfigLossLookUp();

  //printf("front_window[%.3fms] back_window[%.3fms]\n", front_window().GetMSec(), back_window().GetMSec());
  if (combine_mode() == kScoutCombine)
    CalcLossRates(front_window(), back_window());  /** For location-based lookup.*/

  for (size_t i = 0; i < rate_arr_.size(); i++) {
    uint16_t rate = rate_arr_[i];
    switch(combine_mode()) {
      case kFrontOnly:
        loss = loss_map_front_.GetLossRate(rate);
        break;

      case kBackOnly:
        loss = loss_map_back_.GetLossRate(rate);
        break;

      case kDelayCombine:
        loss_front = loss_map_front_.GetLossRate(rate);
        loss_back = loss_map_back_.GetLossRate(rate);
        loss = CalcLossRatesFrontBack(loss_front, loss_back);
        break;

      case kScoutCombine:
        loss_front = loss_map_scout_.GetLossRate(rate);
        loss_front_delay = loss_map_front_.GetLossRate(rate);
        loss_back = loss_map_back_.GetLossRate(rate);
        loss = CalcLossRatesFrontBack(max(loss_front, loss_front_delay), loss_back);
        break;

      default: 
        assert(0);
    }

    //printf("UpdateLoss: rate[%u] loss_front[%g] loss_back[%g] loss[%g]\n", rate, loss_front, loss_back, loss);
    loss_map_combine_.UpdateLoss(rate, loss, kWeightCombine);
  }
}

bool ScoutRateAdaptation::IsHighLoss() {
  double loss = loss_map_combine_.GetLossRate(rate_);
  bool is_duplicate = (loss > duplicate_thresh());
  //printf("IsHighLoss: rate[%u] loss[%g] thresh[%g] is_dup[%d]\n", rate_, loss, duplicate_thresh(), (int)is_duplicate);
  /** This would only happen to the lowest data rate as we won't choose a higher rate with loss > 0.65.*/
  //if (is_duplicate) assert(rate_ == rate_arr_[0]);  
  return is_duplicate;
}

void ScoutRateAdaptation::PrintLossRatesAfterCombine() {
  switch(combine_mode()) {
    case kFrontOnly:
      PrintLossRates(kFront);
      break;

    case kBackOnly:
      PrintLossRates(kBack);
      break;

    case kDelayCombine:
      PrintLossRates(kFront);
      PrintLossRates(kBack);
      break;

    case kScoutCombine:
      PrintLossRates(kFrontScout);
      PrintLossRates(kFront);
      PrintLossRates(kBack);
      break;

    default: 
      assert(0);
  }
  PrintLossRates(kAfterCombine);
}

void ScoutRateAdaptation::ConfigLossLookUp() {
  switch(rate_adapt_version()) {
    case kSampleRate:

    case kRRAA:

    case kFixed:
      set_combine_mode(kBackOnly);
      break;

    case kBatchSeq:

    case kBatchRandom:
      set_combine_mode(kDelayCombine);
      //set_combine_mode(kBackOnly);
      break;

    case kScoutSeq:
    
    case kScoutRandom:

    case kScoutBoundLossSeq:

    case kScoutBoundLossRandom:
      set_combine_mode(kScoutCombine);
      break;
      
    default: 
      assert(0);
  }

  //PrintCombineMode();
}

void ScoutRateAdaptation::ConfigSampleMode() {
  switch(rate_adapt_version()) {
    case kBatchSeq:

    case kScoutSeq:

    case kScoutBoundLossSeq:
      set_sample_mode(kSequential);
      break;

    case kBatchRandom:

    case kScoutRandom:

    case kScoutBoundLossRandom:
      set_sample_mode(kRandom);
      break;

    default: 
      set_sample_mode(kNoSample);
  }
}

void ScoutRateAdaptation::PrintLossRates(Laptop laptop) {
  LossMap *loss_map;
  if (laptop == kFront) {
    loss_map = &loss_map_front_;
    printf("---Front Loss Map---\n");
  }
  else if (laptop == kBack) {
    loss_map = &loss_map_back_;
    printf("---Back Loss Map---\n");
  }
  else if (laptop == kFrontScout) {
    loss_map = &loss_map_scout_;
    printf("---Scout Loss Map---\n");
  }
  else if (laptop == kAfterCombine) {
    loss_map = &loss_map_combine_;
    printf("---Combine Loss Map---\n");
  }
  else
    assert(0);
  loss_map->Print();
}

void ScoutRateAdaptation::PrintFeedbackRecords(Laptop laptop) {
  FeedbackRecords *feedback_rec;
  if (laptop == kFront) {
    printf("===Front Feedback Records===\n");
    feedback_rec = &feedback_rec_front_;
  }
  else {
    printf("===Back Feedback Records===\n");
    feedback_rec = &feedback_rec_back_;
  }
  feedback_rec->PrintRecords();
}

void ScoutRateAdaptation::ApplyRate() {
  int case_num;
  switch (rate_adapt_version_) {
    case kSampleRate:

    case kRRAA:
      rate_ = rate_adapt_baseline_->ApplyRate(case_num);
      break;

    case kFixed:  /** Use the rate set initially. */
      break;

    case kBatchSeq:

    case kBatchRandom:

    case kScoutSeq:

    case kScoutRandom:
      ApplyRateScout();
      break;

    case kScoutBoundLossSeq:

    case kScoutBoundLossRandom:
      ApplyRateScout(loss_bound());
      break;

    default:
      Perror("ScoutRateAdaptation::ApplyRate invalid version number!\n");
  }
}

void ScoutRateAdaptation::FindRatesForBatch(int num_data_pkts, vector<uint16_t> &rate_arr, int &num_coded_pkts) {
  uint16_t rate = 10;
  int case_num;
  rate_arr.clear();
  switch (rate_adapt_version()) {
    case kSampleRate:

    case kRRAA:
      rate_arr.push_back(rate_);
      for (int i = 1; i < num_data_pkts; i++) {
        rate = rate_adapt_baseline_->ApplyRate(case_num);
        rate_arr.push_back(rate);
      }
      break;

    case kFixed:
      for (int i = 0; i < num_data_pkts; i++)
        rate_arr.push_back(rate_);
      break;

    case kBatchSeq:

    case kBatchRandom:

    case kScoutSeq:

    case kScoutRandom:

    case kScoutBoundLossSeq:

    case kScoutBoundLossRandom:
      for (int i = 0; i < num_data_pkts; i++)
        rate_arr.push_back(rate_);
      for (size_t i = 0; i < sample_rates_.size(); i++) 
        rate_arr.push_back(sample_rates_[i]);
      sort(rate_arr.begin(), rate_arr.end());
      break;
      
    default: 
      assert(0);
  }
  num_coded_pkts = rate_arr.size();
}

void ScoutRateAdaptation::ApplyRateScout(double loss_thresh) {
  vector<double> throughput_arr;
  size_t sz = rate_arr_.size();

  for (size_t i = 0; i < sz; i++) {
    uint16_t rate_tmp = rate_arr_[i];
    double loss = loss_map_combine_.GetLossRate(rate_tmp);
    double throughput = -1.0;
    if (loss == -1.0) {
      if (rate_tmp > rate_)
        throughput = 0.0;  /** We won't use a higher rate that is not tried before.*/
      else
        throughput = rate_tmp/10.0;
    }
    else {
      assert(loss >= 0 && loss <= 1);
      throughput = double(rate_tmp) / 10.0 * (1 - loss);
    }
    /** Consult the candidate rate set as well*/
    if (!IsFeasible(rate_tmp) || loss > loss_thresh) throughput = 0.0;
    throughput_arr.push_back(throughput);
  }
/*
  printf("---Feasible rates---\n");
  PrintVector(feasible_rates());
  printf("---throughput_arr---\n");
  PrintVector(throughput_arr);
*/
  /** Find the index of the rate with the highest throughput. */
  int max_ind = FindMaxInd(throughput_arr);
  assert(max_ind >= 0 && max_ind < sz);
  rate_ = rate_arr_[max_ind];
}

void ScoutRateAdaptation::SampleRates(int num_sample_rates, int bound) {
  sample_rates_.clear();
  if (num_sample_rates == 0)
    return;

  ConfigSampleMode();
  
  int start_ind = rate_ind() + 1; /** Sample data rates higher than the current rate.*/

  switch(sample_mode()) {
    case kNoSample:
      return;

    case kSequential:
      SampleRatesSequential(start_ind, num_sample_rates);
      break;

    case kRandom:
      SampleRatesRandom(start_ind, num_sample_rates, bound);
      break;

    default:
      assert(0);
  }

  /** Make sure we have enough protection for the highest data rates.*/
  int sample_left = num_sample_rates - sample_rates_.size();
  if (sample_left) {
    int sample_ind = max(start_ind - 2, 0);  /** Rate lower than the current selected rate.*/
    for (int i = 0; i < sample_left; i++) 
      sample_rates_.push_back(rate_arr_[sample_ind]);
  }
}

void ScoutRateAdaptation::SampleRatesSequential(int start_ind, int num_sample_rates) {
  size_t sz = rate_arr_.size();
  sample_rates_.clear();

  for (int i = 0; i < num_sample_rates; i++) {
    int sample_ind = start_ind + i;
    if (sample_ind < sz) {
      uint16_t sample_rate = rate_arr_[sample_ind];
      if (IsFeasible(sample_rate))
        sample_rates_.push_back(sample_rate);
    }
    else
      break;
  }
}

void ScoutRateAdaptation::SampleRatesRandom(int start_ind, int num_sample_rates, int bound) {
  vector<uint16_t> rates;
  size_t sz = rate_arr_.size();
  assert(num_sample_rates <= bound);  /** We don't want to sample duplicate rates for now. */
  sample_rates_.clear();

  for (int i = 0; i < bound; i++) {
    int rate_ind = start_ind + i;
    if (rate_ind < sz) {
      uint16_t rate = rate_arr_[rate_ind];
      if (IsFeasible(rate))
        rates.push_back(rate);
    }
    else
      break;
  }

  for (int i = 0; i < num_sample_rates; i++) {
    if (rates.empty())
      break;
    int rand_ind = rand() % rates.size();
    sample_rates_.push_back(rates[rand_ind]);
    rates.erase(rates.begin() + rand_ind);
  }
}

bool ScoutRateAdaptation::IsFeasible(uint16 rate) {
  if (feasible_rates_.size() == 0)
    return true;  /** Antenna not aligned.*/
  vector<uint16>::iterator it = find(feasible_rates_.begin(), feasible_rates_.end(), rate);
  return !(it == feasible_rates_.end());
}

void ScoutRateAdaptation::ApplyFEC(const TransmitMode &mode, uint32_t coherence_time/*us*/, 
          uint16_t pkt_size, uint32_t extra_time, int &k, int &n) {
  switch (mode) {
    case kData:
      ApplyFECForData(coherence_time, pkt_size, extra_time, k, n);
      break;

    case kTimeOut:
      ApplyFECForTimeOut(k, n);
      break;

    case kRetrans:
      ApplyFECForRetransmission(coherence_time, pkt_size, extra_time, k, n);
      break;

    default:
      assert(0);
  }
}

bool ScoutRateAdaptation::IsBootstrapping() {
  return (rate_ == rate_arr_[0]);
  if (rate_ == rate_arr_[0]) {
    return (loss_map_combine_.GetLossRate(rate_) == INVALID_LOSS_RATE);
  }
  else
    return false;
}

void ScoutRateAdaptation::ApplyFECForData(uint32_t coherence_time/*us*/, uint16_t pkt_size, 
          uint32_t extra_time, int &k, int &n) {
  /** If loss = -1 (no information) for this data rate.*/
  static const double kStartRedundancy = 0.6;    
  /** Extra redundancy to handle bursty loss.*/
  static const double kExtraRedundancy = 0.05;   
  /** If extra redundancy doesn't increase enough, we will add more packet directly.*/ 
  static const int kExtraNumPkts = 1;            
  static const double kMaxLoss = 0.9;
  double redundancy=-1.0;
  int k_no_extra=-1;
  uint32_t pkt_duration = pkt_size * 8.0 / (rate_/10.) + extra_time;
  n = coherence_time/pkt_duration;
  if (!use_fec()) {
    if (n > kMaxK)
      n = kMaxK;
    k = n;
    /*printf("ApplyFEC[Data] size[%u] rate[%u] pkt_duration[%u] extra[%uus] k[%d] n[%d] kMaxK[%d]\n", 
      pkt_size, rate_, pkt_duration, extra_time, k, n, kMaxK);*/
    return;
  }
  double loss = loss_map_combine_.GetLossRate(rate_);
  if (loss == INVALID_LOSS_RATE) {
    redundancy = kStartRedundancy + kExtraRedundancy;
  }
  else {
    redundancy = loss + kExtraRedundancy;
  }
  k = floor(n * (1 - redundancy));
  if (k > kMaxK) {
    k = kMaxK;
    n = ceil(k/(1 - redundancy));
  }
  if (n > kGFSize) n = kGFSize;
  if (loss <= kMaxLoss && loss != INVALID_LOSS_RATE) {
    k_no_extra = floor(n * (1 - loss));
    if (k_no_extra - kExtraNumPkts < k)
      k = k_no_extra - kExtraNumPkts;
  }
  if (k <= 0)
    k = 1;
  /*printf("ApplyFEC[Data] size[%u] rate[%u] pkt_duration[%u] extra[%uus] loss[%g] redundancy[%g] k[%d] n[%d] k_no_extra[%d]\n", 
    pkt_size, rate_, pkt_duration, extra_time, loss, redundancy, k, n, k_no_extra);*/
  assert(k > 0 && k <= kMaxK && n >= k && n <= kGFSize);
}

void ScoutRateAdaptation::ApplyFECForTimeOut(int k, int &n) {
  /** If loss = -1 (no information) for this data rate.*/
  static const double kStartRedundancy = 0.8;    
  /** Extra redundancy to handle bursty loss.*/
  static const double kExtraRedundancy = 0.05;   
  /** If extra redundancy doesn't increase enough, we will add more packet directly.*/ 
  static const int kExtraNumPkts = 1;            
  static const double kMaxLoss = 0.9;
  double max_redundancy = 0.0;
  double redundancy=-1.0;
  int n_no_extra_redundancy = -1;
  assert(k > 0 && k < kMaxK);  /** For batch_time_out only.*/
  if (!use_fec()) {
    n = k;
    //printf("ApplyFEC[Timeout] k[%d] n[%d] kMaxK[%d]\n", k, n, kMaxK);
    return;
  }
  double loss = loss_map_combine_.GetLossRate(rate_);
  max_redundancy = 1 - k * 1.0 / (kGFSize - 1);  /** minus one is for the ceil function.*/
  if (loss == INVALID_LOSS_RATE) {
    /** No samples are available at this rate, use very high redundancy
    for bootstrapping. */
    redundancy = kStartRedundancy + kExtraRedundancy;
  }
  else {
    redundancy = loss + kExtraRedundancy;
  }
  if (redundancy > max_redundancy)
    redundancy = max_redundancy;  
  if (loss <= kMaxLoss && loss != INVALID_LOSS_RATE) {
    n_no_extra_redundancy = ceil(double(k)/(1 - loss));
  }
  n = ceil(double(k)/(1 - redundancy));
  /** Ensure to put at least kExtraNumPkts extra packet.*/
  if (kExtraRedundancy > 0 && n < n_no_extra_redundancy + kExtraNumPkts && 
      n_no_extra_redundancy + kExtraNumPkts <= kGFSize) 
    n = n_no_extra_redundancy + kExtraNumPkts; 
  if (n > kGFSize)
    n = kGFSize;
  /*printf("ApplyFEC[Timeout] loss[%g] redundancy[%g] k[%d] n[%d] n_no_extra_total[%d]\n", 
    loss, redundancy, k, n, n_no_extra_redundancy + kExtraNumPkts);*/
}

void ScoutRateAdaptation::ApplyFECForRetransmission(uint32_t coherence_time/*us*/, uint16_t pkt_size,
              uint32_t extra_time, int k, int &n) {
  /** If loss = -1 (no information) for this data rate.*/
  static const double kStartRedundancy = 0.8;    
  /** Extra redundancy to handle bursty loss.*/
  static const double kExtraRedundancy = 0.05;   
  /** If extra redundancy doesn't increase enough, we will add more packet directly.*/ 
  static const int kExtraNumPkts = 1;            
  static const double kMaxLoss = 0.9;
  double redundancy=-1.0;
  int n_no_extra_redundancy = -1;
  uint32_t pkt_duration = pkt_size * 8.0 / (rate_/10.) + extra_time;
  const int kMaxN = min(int(coherence_time/pkt_duration), kGFSize);
  assert(k > 0 && k <= kMaxN && k <= kMaxK);  /** Actually k should be 1 for retransmission case only.*/
  if (!use_fec()) {
    n = k;
    //printf("ApplyFEC[Retrans] k[%d] n[%d] kMaxK[%d] kMaxN[%d]\n", k, n, kMaxK, kMaxN);
    return;
  }
  double loss = loss_map_combine_.GetLossRate(rate_);
  if (loss == INVALID_LOSS_RATE) {
    redundancy = kStartRedundancy + kExtraRedundancy;
  }
  else {
    redundancy = loss + kExtraRedundancy;
  }
  if (loss <= kMaxLoss && loss != INVALID_LOSS_RATE) {
    n_no_extra_redundancy = ceil(double(k)/(1 - loss));
  }
  if (redundancy < 1)
    n = ceil(double(k)/(1 - redundancy));
  else
    n = kMaxN;
  /** Ensure to put at least kExtraNumPkts extra packet.*/
  if (kExtraRedundancy > 0 && n < n_no_extra_redundancy + kExtraNumPkts && 
      n_no_extra_redundancy + kExtraNumPkts <= kMaxN) 
    n = n_no_extra_redundancy + kExtraNumPkts; 
  if (n > kMaxN)
    n = kMaxN;
  /*printf("ApplyFEC[Retrans] size[%u] rate[%u] pkt_duration[%u] extra[%uus] loss[%g] redundancy[%g] k[%d] n[%d] kMaxN[%d] n_no_extra_total[%d]\n", pkt_size, rate_, pkt_duration, extra_time, loss, redundancy, k, n, kMaxN, n_no_extra_redundancy + kExtraNumPkts);*/
  assert(n >= k && n <= kMaxN);
}
  
void ScoutRateAdaptation::PrintRateAdaptVersion() const {
  printf("Rate adaptation version: ");
  switch(rate_adapt_version()) {
    case kSampleRate:
      printf("Sample Rate\n");
      break;

    case kRRAA:
      printf("RRAA\n");
      break;

    case kFixed:
      printf("Fix rate\n");
      break;

    case kBatchSeq:
      printf("Batch sequential\n");
      break;

    case kBatchRandom:
      printf("Batch random\n");
      break;

    case kScoutSeq:
      printf("Scout sequential\n");
      break;

    case kScoutRandom:
      printf("Scout random\n");
      break;

    case kScoutBoundLossSeq:
      printf("Scout bound loss sequential\n");
      break;
      
    case kScoutBoundLossRandom:
      printf("Scout bound loss random\n");
      break;

    default: 
      assert(0);
  }
}

void ScoutRateAdaptation::PrintCombineMode() const {
  printf("Combine mode: ");
  switch(combine_mode()) {
    case kFrontOnly:
      printf("Front only\n");
      break;

    case kBackOnly:
      printf("Back only\n");
      break;

    case kDelayCombine:
      printf("Delay based combine\n");
      break;

    case kScoutCombine:
      printf("Scout based combine\n");
      break;

    default: 
      assert(0);
  }
}

void ScoutRateAdaptation::PrintSampleMode() const {
  printf("Sample mode: ");
  switch(sample_mode()) {
    case kNoSample:
      printf("No sample\n");
      break;

    case kSequential:
      printf("Sequential\n");
      break;

    case kRandom:
      printf("Random\n");
      break;

    default: 
      assert(0);
  }
}

int ScoutRateAdaptation::GetRateInd(uint16_t rate) {
  vector<uint16_t>::iterator it = find(rate_arr_.begin(), rate_arr_.end(), rate);  
  if (it == rate_arr_.end())
    return -1;
  else
    return int(it - rate_arr_.begin());
}
