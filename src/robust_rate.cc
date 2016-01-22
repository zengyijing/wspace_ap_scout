#include "robust_rate.h"


RobustRate::RobustRate() {
  rraa_alpha = 1.25;
  rraa_beta = 2.0;
  rraa_difs = 80;
  packet_len = 1000;

  InitRateTable();
}


RobustRate::~RobustRate() {}


void RobustRate::InitRateTable() {
  /*
  int32_t rate[8] = {10, 20, 55, 110, 180, 240, 360, 540};
  int32_t window_size[8] = {6,6,6,10,20,40,40,40};
  int32_t num_rates = 8;
  */
  int32_t rate[] = {10, 20, 55, 110, 120};
  int32_t window_size[] = {6,6,6,10,20};
  int32_t num_rates = sizeof(rate)/sizeof(rate[0]);

  rraa_rate_table.clear();
  RRAARateInfo rate_info;
  for(int i = 0; i < num_rates; ++i) {
    rate_info.rate = rate[i];
    rate_info.wnd_size = window_size[i];
    if(i)
      rate_info.p_mtl = rraa_alpha * (1.0 - (8*10.0*packet_len/rate[i] + rraa_difs)/(8*10.0*packet_len/rate[i-1] + rraa_difs));
    else
      rate_info.p_mtl = 1;
    rraa_rate_table.push_back(rate_info);
  }
  for(int i = 0; i < num_rates - 1; ++i) {
    rraa_rate_table[i].p_ori = rraa_rate_table[i+1].p_mtl/rraa_beta;
  }
  rraa_rate_table[num_rates-1].p_ori = 0;
  //cur_rate_pos = num_rates - 1;
  cur_rate_pos = 0;
  wnd_counter = rraa_rate_table[cur_rate_pos].wnd_size;

  //PrintRateTable();

}


int32_t RobustRate::ApplyRate(int32_t& case_num) {
  if(update_rraa_rate_table) {
    update_rraa_rate_table = 0;
    UpdateRateTable();
  }

  int32_t new_pos = cur_rate_pos;
  --wnd_counter;
  int32_t sz = rraa_rate_table.size();
  if(0 == wnd_counter) {
    double lr = rraa_rate_table[cur_rate_pos].loss_ratio;
    if(lr > rraa_rate_table[cur_rate_pos].p_mtl && rraa_rate_table[cur_rate_pos].num_sent > 0) {
      new_pos = max(0, cur_rate_pos - 1);
    }
    else if(lr < rraa_rate_table[cur_rate_pos].p_ori && rraa_rate_table[cur_rate_pos].num_sent > 0) {
      new_pos = min(sz-1, cur_rate_pos + 1);
    }
    else {
      new_pos = cur_rate_pos;
    }
    wnd_counter = rraa_rate_table[new_pos].wnd_size;
  }

  if(cur_rate_pos!=new_pos) {
    cur_rate_pos = new_pos;
    //PrintRateTable(); 
    //cout<<"Update Rate to:"<<rraa_rate_table[new_pos].rate/10.0<<"Mbps"<<endl;
  }
  /**
  if(new_pos == 0) {
    cur_rate_pos = sz - 1;
    return -1;
  }
  */
  //cout << "RRAA: rate[" << rraa_rate_table[new_pos].rate << "]" << " window[" << wnd_counter << "]" << endl;
  return rraa_rate_table[new_pos].rate;
}

void RobustRate::InsertIntoRateTable(const PacketInfo& pkt) {
  //record the time that the first packet is received and inserted into rate table
  if(starting) {
    starting = 0;
    sr_start = MonotonicTimer();
  }

  PacketStatus status = pkt.status;
  int32_t rate = pkt.rate;
  int32_t len = pkt.length;

  for(int32_t i = 0, sz = rraa_rate_table.size(); i < sz; ++i) {
    if(rraa_rate_table[i].rate!=rate)
      continue;
    rraa_rate_table[i].num_sent++;
    rraa_rate_table[i].len_sent+=len;

    if(status==kACKed) {
      rraa_rate_table[i].num_acked++;
      rraa_rate_table[i].len_acked+=len;
    }
    else {
      /*num_sent == num_acked + "rest"*/
    }
    break;
  }
  update_rraa_rate_table = 1;
}

void RobustRate::RemoveFromRateTable(const PacketInfo& pkt) {
  PacketStatus status = pkt.status;
  int32_t len = pkt.length;
  int32_t sz = rraa_rate_table.size();
  for(int32_t i = 0; i < sz; ++i) {
    if(rraa_rate_table[i].rate!=pkt.rate)
      continue;
    rraa_rate_table[i].num_sent--;      
    rraa_rate_table[i].len_sent-=len;
    if(status==kACKed) {
      rraa_rate_table[i].num_acked--;
      rraa_rate_table[i].len_acked-=len;
    }
    else {
      /* */
    }
    break;
  }
  update_rraa_rate_table = 1;
}

void RobustRate::UpdateRateTable() {
  int32_t sz = rraa_rate_table.size();
  for(int32_t i = 0; i < sz; ++i) { 
    if(abs(rraa_rate_table[i].len_sent) < 1e-9)
      rraa_rate_table[i].len_sent = 0;
    if(abs(rraa_rate_table[i].len_acked) < 1e-9)
      rraa_rate_table[i].len_acked = 0;

    double len_sent = rraa_rate_table[i].len_sent;
    double len_loss = rraa_rate_table[i].len_sent - rraa_rate_table[i].len_acked;
    if(len_sent > 0)
      rraa_rate_table[i].loss_ratio = len_loss/len_sent;
    else
      rraa_rate_table[i].loss_ratio = 1;
  }
}


void RobustRate::PrintRateTable() {
  cout<<"Robust Rate Table:"<<endl;
  cout<<"rate(Mbps), num_sent, num_acked, loss_ratio, \t p_ori, \t p_mtl, wnd_size"<<endl;
  int sz = rraa_rate_table.size();
  for(int i = 0; i < sz; ++i) {
    rraa_rate_table[i].PrintRateInfo();
  }
}


