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
  void ParseLossRates(unordered_map<int, unordered_map<int, string> > &filename_tbl);
  double GetNextLossRate(int bs_id, int client_id);

 private:
  void ParseLossRates(const string &filename);
    
  vector<int> rate_arr_; 
  unordered_map<int, unordered_map<int, LossTable> > loss_tbl_;  // <client_id, <bs_id, loss_table> >.
};

#endif
