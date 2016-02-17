#include "loss_rate_parser.h"

void LossRateParser::ParseLossRates(const string &filename) {
}

void LossRateParser::ParseLine(const string &line) {
  stringstream ss(line);
  double loss = -1.0;
  int cnt = 0;
  unordered_map<int, double> loss_tbl;
  while (ss >> loss) {
    if (++cnt <= 3) {
      continue;
    }
    loss_tbl[rate_arr[cnt-4]] = 
}
}
