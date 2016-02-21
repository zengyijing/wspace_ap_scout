#include "packet_drop_manager.h"

PacketDropManager::PacketDropManager(int32_t* rates, int size) {
  Pthread_mutex_init(&lock_, NULL);
  for (int i = 0; i < size; ++i) {
    rate_arr_.push_back(rates[i]);
  }
}

PacketDropManager::~PacketDropManager() {
  Pthread_mutex_destroy(&lock_);
}

void PacketDropManager::ParseLossRates(const vector<string> &filenames, const vector<int> &client_ids) {
  assert(filenames.size() == client_ids.size());
  for(int i = 0; i < client_ids.size(); ++i) {
    ifstream input(filenames[i].c_str());
    string line;
    while(getline(input, line)) {
      LossTable table = ParseLine(line);
      loss_queues_[client_ids[i]].push(table);
    }
    printf("read from file %s lines:%d for client:%d\n", filenames[i].c_str(), loss_queues_[client_ids[i]].size(), client_ids[i]);
  }
}


void PacketDropManager::ParseLossRates(const vector<double> &loss_rates, const vector<int> &client_ids) {
  assert(loss_rates.size() == client_ids.size());
  LossTable table;
  for (int i = 0; i < client_ids.size(); ++i) {
    for (int j = 0; j < rate_arr_.size(); ++j) {
     table[rate_arr_[j]] = loss_rates[i];
    }
    loss_queues_[client_ids[i]].push(table);
  }
}

map<int, double> PacketDropManager::ParseLine(const string line) {
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
    table[rate_arr_[i++]] = loss;
  }
  assert(cnt - 3 == rate_arr_.size());
  return table;
}

bool PacketDropManager::PopLossRates() {
  bool successful_pop = true;
  Lock();
  for (map<int, queue<LossTable> >::iterator it = loss_queues_.begin(); it != loss_queues_.end(); ++it) {
    if (!it->second.empty()) {
      it->second.pop();
    } else {
      successful_pop = false;
    }
  }
  UnLock();
  return successful_pop;
}

bool PacketDropManager::GetLossRate(int client_id, int rate, double* loss_rate) {
  bool get_loss = false;
  Lock();
  if(loss_queues_.count(client_id) > 0 && !loss_queues_[client_id].empty()) {
    assert(loss_queues_[client_id].front().count(rate) > 0);
    *loss_rate = loss_queues_[client_id].front()[rate];
    get_loss = true;
  }
  UnLock();
  return get_loss;
}
