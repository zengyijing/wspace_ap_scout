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
 
  PacketDropManager(int32_t* rates, int size);
  ~PacketDropManager();
  
  void ParseLossRates(const vector<string> &filenames, const vector<int> &client_ids);
  void ParseLossRates(const vector<double> &loss_rates, const vector<int> &client_ids);
  void GetLossRate(int client_id, int rate, double* loss_rate);
  bool PopLossRates();

 private:
  LossTable ParseLine(const string line);
  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }

  vector<int32_t> rate_arr_;
  map<int, queue<LossTable> > loss_queues_; // <client_id, queue>.
  pthread_mutex_t lock_; 
};

#endif
