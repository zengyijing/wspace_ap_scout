#include "packet_drop_manager.h"

PacketDropManager::PacketDropManager() {
  use_trace_file_ = false;
  Pthread_mutex_init(&lock_, NULL);
}

PacketDropManager::~PacketDropManager() {
  Pthread_mutex_destroy(&lock_);
}

void PacketDropManager::ParseLossRates(const vector<string> &filenames, const vector<int> &client_ids) {
  assert(filenames.size() == client_ids.size());
  use_trace_file_ = true;
  for(int i = 0; i < client_ids.size(); ++i) {
    ifstream input(filenames[i].c_str());
    string line;
    int line_count = 0;
    while(getline(input, line)) {
      if(line_count++ > 0)
        ParseLine(line, client_ids[i]);
    }
    printf("read from file %s lines:%d for client:%d\n", filenames[i].c_str(), loss_queues_[client_ids[i]].size(), client_ids[i]);
  }
}


void PacketDropManager::ParseLossRates(const vector<double> &loss_rates, const vector<int> &client_ids) {
  assert(loss_rates.size() == client_ids.size());
  LossTable table;
  for (int i = 0; i < client_ids.size(); ++i) {
    for (int j = 0; j < mac80211abg_num_rates; ++j) {
     table[mac80211abg_rate[j]] = loss_rates[i];
    }
    loss_queues_[client_ids[i]].push(table);
  }
}

void PacketDropManager::ParseLine(const string &line, const int &client_id) {
  stringstream ss(line);
  double loss = -1.0;
  int cnt = 0;
  string s;
  LossTable table;
  int i = 0;
  while (getline(ss, s, ' ')) {
    if (++cnt <= 3) {
      continue;
    }
    loss = atof(s.c_str());
    assert(loss <= 1 && loss >= 0);
    table[mac80211abg_rate[i++]] = loss;
  }
  assert(cnt - 3 == mac80211abg_num_rates);
  loss_queues_[client_id].push(table);
}

void PacketDropManager::UpdateLossRates() {
  if (use_trace_file_) {
    Lock();
    for (map<int, queue<LossTable> >::iterator it = loss_queues_.begin(); it != loss_queues_.end(); ++it) {
      if (it->second.size() > 1) {
        it->second.pop();
      } else {
        printf("current trace files has been gone over, exit\n");
        assert(false);
      }
    }
    UnLock();
  }
}

double PacketDropManager::GetLossRate(const int &client_id, const int &rate) {
  double loss_rate = 0;
  Lock();
  if(loss_queues_.count(client_id) > 0) {
    if (loss_queues_[client_id].front().count(rate) == 0)
      printf("input rate is :%d\n");
    assert(loss_queues_[client_id].front().count(rate) > 0);
    loss_rate = loss_queues_[client_id].front()[rate];
  }
  UnLock();
  return loss_rate;
}
