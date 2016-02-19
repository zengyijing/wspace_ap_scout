#include "loss_rate_parser.h"

void LossRateParser::ParseLossRates(const string &filename) {
  ifstream input(filename.c_str());
  string line;
  int line_count = 0;
  while(getline(input, line)) {
    if(line_count++ > 0)
      ParseLine(line);
  }
  printf("read from file %s lines:%d\n", filename.c_str(), loss_queue_.size());
}

void LossRateParser::ParseLine(const string &line) {
  stringstream ss(line);
  double loss = -1.0;
  int cnt = 0;
  string s;
  LossTable table;
  while (getline(ss, s, ' ')) {
    if (++cnt <= 3) {
      continue;
    }
    loss = atof(s.c_str());
    assert(loss <= 1 && loss >= 0);
    table.push_back(loss);
  }
  assert(cnt - 3 == mac80211abg_num_rates);
  loss_queue_.push(table);
}

vector<double> LossRateParser::GetNextLossRates() {
  LossTable table;
  if (loss_queue_.size() > 0) {
    table = loss_queue_.front();
    loss_queue_.pop();
  }
  return table;
}

