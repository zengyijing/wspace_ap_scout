#ifndef LOSS_RATE_PARSER_H_
#define LOSS_RATE_PARSER_H_

#include <cassert>
#include <fstream>
#include <string>
#include <map>
#include <queue>
#include <vector>

#include "base_rate.h"

using namespace std;

class LossRateParser {
 public:
  typedef vector<double> LossTable;
 
  LossRateParser() {}
  ~LossRateParser() {}
  
  void ParseLossRates(const string &filename);
  void ParseLine(const string &line);
  LossTable GetNextLossRates();

 private:
  queue<LossTable> loss_queue_;
};

#endif
