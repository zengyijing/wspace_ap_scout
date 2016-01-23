#include "sample_rate.h"

SampleRate::SampleRate() {

  counting = 0;
  srand (time(NULL));
  update_interval = MonotonicTimer(10, 0);

  InitRateTable();
  update_rate_table = 1; 
  /* if there is insert or delete in the rate table, update_rate_table will be set to 1
  and to be updated only when calculating the sending rate(this will cause extra running time, 
  but prevent calculating the rate when inserting or deleting)
  */

}


void SampleRate::InitRateTable() {
  //  starting = 1;
  pkt_count = 0; 
  cur_rate_pos = 0; 

  statistic.clear();

  PacketStat hs; 
  rate_table.clear();
  for(int i = 0; i < mac80211abg_num_rates; ++i) {
    rate_table.push_back(RateHistory(mac80211abg_rate[i], hs));
  }
}

SampleRate::~SampleRate() {

}

static inline bool Is80211b(uint16_t rate) {
  return (10==rate || 20==rate || 55==rate || 110==rate);
}

int32_t SampleRate::ApplyRate(int32_t& case_num) {
  /*
  • If no packets have been successfully acknowledged, return the highest bit-rate that
  has not had 4 successive failures.
  • Increment the number of packets sent over the link.
  • If the number of packets sent over the link is a multiple of ten, select a random bit-rate
  from the bit-rates that have not failed four successive times and that have a minimum
  packet transmission time lower than the current bit-rate’s average transmission time.
  • Otherwise, send the packet at the bit-rate that has the lowest average transmission
  time.
  */
  if(update_rate_table) {
    update_rate_table = 0;
    UpdateRateTable();
  }

  //int32_t case_num = 0;
  uint32_t new_pos = cur_rate_pos;

  int32_t sz = rate_table.size();
  ++pkt_count; 

  if(0==statistic.num_acked) {
#ifdef UP_TO_DOWN
    new_pos = sz - 1;
    for(int32_t i = sz - 1; i >= 0; --i) {
      if(rate_table[i].history.num_conti_loss < 4) {
        new_pos = i;
        break;
      }
    }
#else if DOWN_TO_UP
    new_pos = 0;
    for(int32_t i = 0; i < sz; ++i) {
      if(rate_table[i].history.num_conti_loss < 4) {
        new_pos = i;
        break;
      }
    }
#endif
    case_num = 1;
  }
  else if(0==pkt_count%10) {
    pkt_count = 0;
    int32_t counter = 0;
    for(int32_t i = 0; i < sz && i < cur_rate_pos + 3; ++i)
            if(rate_table[i].history.num_conti_loss < 4 && i != cur_rate_pos && rate_table[i].history.min_avg_tx_time < rate_table[cur_rate_pos].history.avg_tx_time) 
        counter++;
    if(0!=counter) {
      int32_t random = rand()%counter;
      //for(int32_t i = 0; i < sz && i < cur_rate_pos + 3; ++i) {
      for(int32_t i = 0; i < sz; ++i) {
        if(rate_table[i].history.num_conti_loss < 4 && i != cur_rate_pos && rate_table[i].history.min_avg_tx_time < rate_table[cur_rate_pos].history.avg_tx_time) {
          if(0==random) {
            new_pos = i;
            break;
          }
          random--;
        }
      }
    }
    case_num = 2;
  }
  else {
    double min_tx_time = 1e10;
    for(int32_t i = 0; i < sz; ++i) {
      if(min_tx_time > rate_table[i].history.avg_tx_time) {
        min_tx_time = rate_table[i].history.avg_tx_time;
        new_pos = i;
      }
    }
    case_num = 3;
  }
    
  if(cur_rate_pos!=new_pos) {
    cur_rate_pos = new_pos;
    //MonotonicTimer now = MonotonicTimer();
    //now-=sr_start;
    //now.PrintTimer(1);   
    //PrintRateTable(); 
    //cout<<" Update to rate:"<<rate_table[cur_rate_pos].rate/10.0<<"Mbps\t by case:"<<case_num<<endl;
  }
//  cout<<"using rate :"<<rate_table[new_pos].rate/10.0<<"\tnew pos:"<<new_pos<<"\tcur_pos:"<<cur_rate_pos<<"\tcase:"<<case_num<<endl;
//  PrintRateTable();


  //if(cur_rate_pos == 0 && case_num == 3) return -1;

  return rate_table[new_pos].rate;
}



void SampleRate::InsertIntoRateTable(const PacketInfo& pkt) {
  //record the time that the first packet is received and inserted into rate table
  /*
  if(starting) {
    starting = 0;
    sr_start = MonotonicTimer();
  }
  */

  PacketStatus status = pkt.status;
  int32_t rate = pkt.rate;
  int32_t len = pkt.length;

  double difs;
  int32_t sz = rate_table.size();
  if(Is80211b(rate))
    difs = DIFS_80211b;
  else
    difs = DIFS_80211ag;

  for(int32_t i = 0; i < sz; ++i) {
    if(rate_table[i].rate!=rate)
      continue;
    rate_table[i].history.num_sent++;
    rate_table[i].history.len_sent+=len;
    rate_table[i].history.total_tx_time += (((len*80.0)/rate) + difs);

    if(status==kACKed) {
      statistic.num_acked++;
      rate_table[i].history.num_acked++;
      rate_table[i].history.len_acked+=len;
      rate_table[i].history.num_conti_loss = 0;
    }
    else if(status==kNAKed) {
      rate_table[i].history.num_naked++;
      rate_table[i].history.len_naked+=len;
      rate_table[i].history.num_conti_loss++;
    }
    else if(status==kTimeOut) {
      rate_table[i].history.num_timeout++;
      rate_table[i].history.len_timeout+=len;
      rate_table[i].history.num_conti_loss++;
    }
    else {}
    break;
  }
  update_rate_table = 1;
}

void SampleRate::RemoveFromRateTable(const PacketInfo& pkt) {
  PacketStatus status = pkt.status;
  int32_t len = pkt.length;
  int32_t sz = rate_table.size();
  for(int32_t i = 0; i < sz; ++i) {
    if(rate_table[i].rate!=pkt.rate)
      continue;

    double difs;
    if(Is80211b(pkt.rate))
      difs = DIFS_80211b;
    else
      difs = DIFS_80211ag;

    rate_table[i].history.num_sent--;      
    rate_table[i].history.len_sent-=len;
    rate_table[i].history.total_tx_time -= (((len*80.0)/pkt.rate) + difs);
    if(status==kACKed) {
      statistic.num_acked--;
      rate_table[i].history.num_acked--;
      rate_table[i].history.len_acked-=len;
    }
    else if(status==kNAKed) {
      rate_table[i].history.num_naked--;
      rate_table[i].history.len_naked-=len;
    }
    else if(status==kTimeOut) {
      rate_table[i].history.len_timeout-=len;
      rate_table[i].history.num_timeout--;
    }
    else {}

    break;
  }
  update_rate_table = 1;
}

void SampleRate::UpdateRateTable() {

  MonotonicTimer now = MonotonicTimer();
  if(now > last_update + update_interval) {
    last_update = now;
    int32_t sz = rate_table.size();
    for(int32_t i = 0; i < sz; ++i)
      rate_table[i].history.num_conti_loss = 0;
  }

  int32_t sz = rate_table.size();
  int32_t sent, acked;
  for(int32_t i = 0; i < sz; ++i) { 
    //sent = rate_table[i].history.num_sent;
    double len_sent = rate_table[i].history.len_sent;
    if(abs(rate_table[i].history.len_sent) < 1e-9)
      len_sent = 0, rate_table[i].history.len_sent = 0;
    if(abs(rate_table[i].history.total_tx_time) < 1e-9)
      rate_table[i].history.total_tx_time = 0;
    else if (len_sent > 0)
      rate_table[i].history.min_avg_tx_time = rate_table[i].history.total_tx_time/len_sent;
    //acked = rate_table[i].history.num_acked;
    double len_acked = rate_table[i].history.len_acked;
    if(abs(rate_table[i].history.len_acked) < 1e-9)
      len_acked = 0, rate_table[i].history.len_acked = 0;
    if(0==len_acked)
      rate_table[i].history.avg_tx_time = 1e10;
    if(len_acked > 0 && rate_table[i].history.total_tx_time >= rate_table[i].history.min_avg_tx_time) {
      rate_table[i].history.avg_tx_time = rate_table[i].history.total_tx_time/len_acked;
    }
  }
}


void SampleRate::PrintRateTable() {
  statistic.PrintStat();
  cout<<"Sample Rate Table:"<<endl;
  cout<<"rate(Mbps), num_sent, num_acked, timeout+naked, num_conti_loss, total_tx_time(us), avg_tx_time(us), min_avg_time(us)"<<endl;
  int sz = rate_table.size();
  for(int i = 0; i < sz; ++i) {
    cout<<double(rate_table[i].rate)/10<<":\t\t";
    rate_table[i].history.PrintStat();
  }
}



