#ifndef PACKET_DROP_MANAGER_H_
#define PACKET_DROP_MANAGER_H_

#include <cassert>
#include <fstream>
#include <string>
#include <map>
#include <queue>
#include <vector>

#include "base_rate.h"
#include "pthread_wrapper.h"

using namespace std;

class PacketDropManager {
 public:
  typedef map<int, double> LossTable; // <rate, loss_rate>.
 
  PacketDropManager();
  ~PacketDropManager();
  
  void ParseLossRates(const vector<string> &filenames, const vector<int> &client_ids);
  void ParseLossRates(const vector<double> &loss_rates, const vector<int> &client_ids);
  double GetLossRate(const int &client_id, const int &rate);
  void UpdateLossRates();

 private:
  bool use_trace_file_;
  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }
  void ParseLine(const string &line, const int &client_id);
  map<int, queue<LossTable> > loss_queues_; // <client_id, queue>.
  pthread_mutex_t lock_; 
};

#endif
