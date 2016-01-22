#ifndef SCOUT_RATE_H_
#define SCOUT_RATE_H_

#include <map>
#include <algorithm>
#include <iostream>

#include "base_rate.h"
#include "rate_adaptation.h"
#include "monotonic_timer.h"
#include "wspace_asym_util.h"

static const double kAntDist = 1.1;  /** in meter.*/
static const double kDuplicateThresh = 0.7; 
static const double kLossBound = 0.65;
static const MonotonicTimer kFrontWindow = MonotonicTimer(0, 50e6);
static const MonotonicTimer kBackWindow = MonotonicTimer(0, 100e6);
static const MonotonicTimer kTotalWindow = MonotonicTimer(10, 0);
static const int kNumSampleRates = 1;
static const int kSampleBound = 2;

struct LossInfo {
  uint32_t n_sent; 
  uint32_t n_ack;
  double loss;
};

class LossMap {
 public:
  LossMap(const int *rate_arr, int num_rates);
  ~LossMap();

  /**
   * Update the key of the map.
   * Note: Locking is included.
   * @previous_weight: the weight for the history.
   */
  void UpdateLoss(uint16_t rate, double loss, double previous_weight = -1);

  void UpdateSendCnt(uint16_t rate, uint32_t n_sent, uint32_t n_ack);
  
  /**
   * Return the loss rate of a given data rate.
   * Note: Locking is included.
   */
  double GetLossRate(uint16_t rate);

  uint32_t GetNSent(uint16_t rate);

  uint32_t GetNAck(uint16_t rate);

  void Print();

 private:
  void Lock() { Pthread_mutex_lock(&lock_); }

  void UnLock() { Pthread_mutex_unlock(&lock_); }

  std::map<uint16_t, LossInfo> loss_map_; // Store the loss rate of each data rate.
  pthread_mutex_t lock_;
};

class ScoutRateAdaptation {
 public:


  /*
  enum Laptop {
    kInvalid = 0, 
    kFront = 1,
    kBack = 2,
    kFrontScout = 3,
    kAfterCombine = 4,
  };
  */


  enum CombineMode {
    kFrontOnly = 0,
    kBackOnly = 1,
    kDelayCombine = 2,
    kScoutCombine = 3,
  };

  enum TransmitMode {
    kData = 0,
    kTimeOut = 1,
    kRetrans = 2,
  };

  enum SampleMode {
    kNoSample = 0, 
    kSequential,
    kRandom,
  };

  ScoutRateAdaptation(const int32_t *rate_arr, int32_t num_rates, int32_t gf_size, int32_t max_k, 
        double antenna_dist = kAntDist, double duplicate_thresh = kDuplicateThresh, 
        double loss_bound = kLossBound, const MonotonicTimer &front_window = kFrontWindow, 
        const MonotonicTimer &back_window = kBackWindow, const MonotonicTimer &total_window = kTotalWindow);

  ~ScoutRateAdaptation();

  void InsertFeedback(Laptop laptop, uint32_t seq, PacketStatus status, uint16_t rate, uint16_t len, const MonotonicTimer &time);

  /**
   * Both coherence_time and extra_time are in us.
   */
  void MakeDecision(TransmitMode mode, uint32_t coherence_time, uint16_t pkt_size, uint32_t extra_time, 
      int &k, int &n, vector<uint16_t> &rate_arr, bool &is_duplicate);

  /**
   * Determine what loss rates to be used to determine FEC.
   */
  void ConfigLossLookUp();

  void ConfigSampleMode();

  void SetLossRates(Laptop laptop, const vector<uint16_t> &rate_arr, const vector<double> &loss_arr);

  void SetHighLoss();

  /**
   * For lookup delayed loss rates from the front and back antennas.
   */
  void CalcLossRates(Laptop laptop, const MonotonicTimer &start, const MonotonicTimer &end);

  /**
   * For scout-based rate adaptation.
   */
  void CalcLossRates(const MonotonicTimer &front_window, const MonotonicTimer &back_window);

  void CalcLossRatesAfterCombine();

  double CalcLossRatesFrontBack(double loss_front, double loss_back);

  bool IsHighLoss();

  void PrintLossRates(Laptop laptop);

  void PrintLossRatesAfterCombine();

  void PrintFeedbackRecords(Laptop laptop);

  void ApplyRate();

  /**
   * Figure out what rates to sample next.
   */
  void SampleRates(int num_sample_rates, int bound);

  void FindRatesForBatch(int num_data_pkts, vector<uint16_t> &rate_arr, int &num_coded_pkts);

  void ApplyFEC(const TransmitMode &mode, uint32_t coherence_time/*us*/, uint16_t pkt_size, uint32_t extra_time, int &k, int &n);

  void ApplyFECForData(uint32_t coherence_time/*us*/, uint16_t pkt_size, uint32_t extra_time, int &k, int &n);

  void ApplyFECForTimeOut(int k, int &n);

  void ApplyFECForRetransmission(uint32_t coherence_time/*us*/, uint16_t pkt_size, uint32_t extra_time, int k, int &n);

  bool IsFeasible(uint16_t rate);

  vector<uint16_t> & feasible_rates() { return feasible_rates_; }

  double Dist() const { return kAntennaDist; }

  const vector<uint16_t> & sample_rates() const { return sample_rates_; }

  int GetRateInd(uint16_t rate); 

  void PrintRateAdaptVersion() const;
  void PrintCombineMode() const;
  void PrintSampleMode() const;

  void set_use_fec(bool use_fec) { use_fec_ = use_fec; }
  bool use_fec() const { return use_fec_; }

  void set_speed(double speed) { speed_ = speed; }
  double speed() { return speed_; }

  void set_combine_mode(const CombineMode &mode) { combine_mode_ = mode; }
  CombineMode combine_mode() const { return combine_mode_; }

  void set_sample_mode(const SampleMode &mode) { sample_mode_ = mode; }
  SampleMode sample_mode() const { return sample_mode_; }

  void set_rate(uint16_t rate) { rate_ = rate; }
  uint16_t rate() const { return rate_; }
  int rate_ind() { return rate_ind_map_[rate_]; }

  void set_rate_adapt_version(const RateAdaptVersion &version);
  RateAdaptVersion rate_adapt_version() const { return rate_adapt_version_; }

  void set_duplicate_thresh(double duplicate_thresh) { duplicate_thresh_ = duplicate_thresh; }
  double duplicate_thresh() const { return duplicate_thresh_; }

  void set_loss_bound(double loss_bound) { loss_bound_ = loss_bound; }
  double loss_bound() const { return loss_bound_; }

  void set_enable_duplicate(bool enable_duplicate) { enable_duplicate_ = enable_duplicate; }
  bool enable_duplicate() const { return enable_duplicate_; }

  bool IsBootstrapping();  /** No information available at the beginning.*/


  /**
   * Return the loss map of a given laptop.
   */
  LossMap* GetLossMap(Laptop laptop);


 private:
  const MonotonicTimer & front_window() const { return front_window_; }
  const MonotonicTimer & back_window() const { return back_window_; }

  /**
   * Scout-based rate adaptation based on the combination of location-based front feedback and
   * delayed back feedback to determine best rate and FEC. It also trackes the feasible rates at 
   * the back antenna. 
   * @param loss_thresh if the loss rate is greater than it, the rate's throughput will be reset to 0. It's not
   * activated by default.
   * @see CalcLossRatesAfterCombine()
   */
  void ApplyRateScout(double loss_thresh=1.5);

  /**
   * Either sample the sequentially higher data rates or
   * perform random sampling,
   */
  void SampleRatesSequential(int start_ind, int num_sample_rates);

  void SampleRatesRandom(int start_ind, int num_sample_rates, int bound);

  /** Data members.*/
  const double kAntennaDist;
  const int kGFSize;  /** Maximum number of coded packets in a batch. */
  const int kMaxK;    /** Need this parameter to limit the header overhead and set MTU properly. */
  uint16_t rate_;     /** Track the current rate decision.*/
  RateAdaptVersion rate_adapt_version_; 
  CombineMode combine_mode_;
  SampleMode sample_mode_;
  bool use_fec_;
  std::vector<uint16_t> rate_arr_, feasible_rates_;
  std::vector<uint16_t> sample_rates_;
  FeedbackRecords feedback_rec_front_, feedback_rec_back_;   
  LossMap loss_map_front_, loss_map_back_, loss_map_scout_, loss_map_combine_/** After combining the front and back antenna*/; 
  RateAdaptation* rate_adapt_baseline_;  /** Either sample rate or RRAA implemented by Lei.*/
  double speed_; 
  MonotonicTimer front_window_, back_window_;
  double duplicate_thresh_;  /** Threshshold for the loss rate, beyond which the duplicating should happen. */
  double loss_bound_;
  bool enable_duplicate_;   /** Enable duplicating packets over the cellular. */
  std::map<uint16_t, int> rate_ind_map_;
};

template <class T>
int FindMaxInd(const vector<T> &arr) {
  size_t sz = arr.size();
  if (sz == 0)
    return -1;
  int max_ind = 0;
  for (int i = 1; i < sz; i++) 
    if (arr[max_ind] < arr[i])
      max_ind = i;
  return max_ind;
}

template <class T>
void PrintVector(const vector<T> &arr) {
  class vector<T>::const_iterator it;
  cout << "size[" << arr.size() << "] ";
  for (it = arr.begin(); it != arr.end(); it++) 
    cout << *it << " ";
  cout << endl;
}

inline double ScoutRateAdaptation::CalcLossRatesFrontBack(double loss_front, double loss_back) {
  double loss = INVALID_LOSS_RATE;
  if (loss_front == INVALID_LOSS_RATE || loss_back == INVALID_LOSS_RATE)
    loss = max(loss_front, loss_back);
  else if (loss_front == 0 || loss_back == 0)
    loss = max(loss_front, loss_back) / 2.;
  else
    loss = loss_front * loss_back;
  return loss;
}

#endif 
