#ifndef LOSS_RATE_PARSER_H_
#define LOSS_RATE_PARSER_H_

#include <cassert>
#include <string>
#include <unordered_map>

using namespace std;

class LossRateParser {
 public:
  typedef unordered_map<int, double> LossTable;
 
  LossRateParser();
  ~LossRateParser();
  
  // <client_id, <bs_id, filename> >.
  void ParseLossRates(const vector<int> &rate_arr, unordered_map<int, unordered_map<int, string> > &filename_tbl);
  // @yijing: Pop queue for all the <client_id and bs_id>
  // With lock.
  void UpdateLossRate();
  // @yijing: Get top of the queue.
  // With lock.
  double GetLossRate(int client_id, int bs_id);

 private:
  void ParseLossRates(const string &filename);
  void Lock();
  void UnLock();
    
  unordered_map<int, unordered_map<int, queue<LossTable> > > loss_tbl_;  // <client_id, <bs_id, loss_table> >.
  pthread_mutex_t lock_;
};

#endif
