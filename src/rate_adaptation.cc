#include "rate_adaptation.h"

ofstream outFile ("trace.dat");
ofstream testFile ("statistic.dat");


RateAdaptation::RateAdaptation(const RateAdaptVersion &version)
{
	duration = MonotonicTimer(10, 500*1e6);
	assert(outFile.is_open());
	assert(testFile.is_open());
	
	switch (version)
	{
		case kSampleRate:
			sample_rate = new SampleRate();
			base_rate = sample_rate;
			break;

		case kRRAA:
			robust_rate = new RobustRate();
			base_rate = robust_rate;
			break;

		default:
			assert(0);
	}

	counting = 0;
	record_start = MonotonicTimer(0,0);
	record_end = MonotonicTimer(0,0);
	record_sum = MonotonicTimer(0,0);
	record_min = MonotonicTimer(10,0);
	record_max = MonotonicTimer(0 ,0);

	break_number = 0;
	setting = 0;

	packet_sent = new MonotonicTimer[1000];
	packet_ack = new MonotonicTimer[1000];
}


RateAdaptation::~RateAdaptation()
{
	outFile.close();
	testFile.close();
	delete base_rate;
}

/*
There is one case for re-transmission we did not consider to handle
If packet 1 is sent, and naked, we re-transmit, if the packet is naked,
we do not know whether this nak is for the first transmission or the second 
*/
////////////public entrance, only insert NAK/ACK/TIMEOUT///////////////////
void RateAdaptation::InsertRecord(uint32_t seq, PacketStatus status, uint16_t rate, uint16_t len)
{
/*
	MonotonicTimer tt;
	cout<<tt.GetSec()<<":"<<tt.GetNSec()<<","<<seq<<","<<status<<endl;
*/
/*  ////////////////////////////////////////////
//	MonotonicTimer tt;
	//outFile<<seq<<"\t"<<status<<"\t"<<tt.GetSec()<<":"<<tt.GetNSec()<<endl;
	if(seq<=1000) {
		MonotonicTimer t;
	  if(status==kSent) packet_sent[seq-1] = t;
	  else packet_ack[seq-1] = t;
  } else if(seq > 1000) {
	  for(int i = 0;i < 1000; ++i) {
			MonotonicTimer diff = packet_ack[i] - packet_sent[i];
			MonotonicTimer zero = MonotonicTimer(0,0);
			if(diff<zero) diff=zero; 
			 
			outFile<<i+1<<","<<diff.GetMSec()<<"\t"<<packet_ack[i].GetSec()<<","<<packet_ack[i].GetNSec()<<"\t"<<packet_sent[i].GetSec()<<","<<packet_sent[i].GetNSec()<<endl;
		}
		assert(0);
	}
	return;
*/	//////////////////////////////////////////

	if(status == kSent)
		return; //reserved for future usage
#ifdef TEST_RATE
	if(seq < break_number)
		return;

	int32_t random = rand()%100;
	int32_t threshold = base_rate->GetDropThreshold(rate);
	bool kDrop = random < threshold ? 1 : 0;
	if (kDrop == 1 && status == kACKed)
		status = kNAKed;
#endif

	MonotonicTimer now;
	PacketInfo pkt = {now, seq, rate, len, status};
	history_records.push_back(pkt);  
	base_rate->InsertIntoRateTable(pkt);
	UpdateRecords(now);
}

void RateAdaptation::UpdateRecords(const MonotonicTimer& now)
{
	PacketInfo pkt;
	while(!history_records.empty())
	{
		pkt = history_records.front();
		if(pkt.timer + duration > now)
			break;
		history_records.pop_front();
		base_rate->RemoveFromRateTable(pkt);
	}
}

void RateAdaptation::SetBreakSeqNum(uint32_t seq_num)
{
	if(!setting)
		return;
	setting = 0;
	break_number = seq_num;
	//cout<<"set break number to "<<seq_num<<endl;
}

int32_t RateAdaptation::ApplyRate(int32_t& case_num)
{
	int32_t rate = base_rate->ApplyRate(case_num);
////////
/*
  if(rate == -1) {
    counting++;    
    record_end = MonotonicTimer();
    MonotonicTimer diff = record_end - record_start;
    record_start = record_end;
    if(counting > 1) {  
      cout<<diff.GetMSec()<<endl;
      record_sum += diff;
      if(diff > record_max) record_max = diff;
      if(diff < record_min) record_min = diff;
    }

    if(counting >= 20 + 1) {
      MonotonicTimer avg = record_sum/(counting - 1);
      cout<<record_sum.GetMSec()<<endl;
      cout<<"\t"<<avg.GetMSec()<<"\t"<<record_max.GetMSec()<<"\t"<<record_min.GetMSec()<<endl;
      assert(0);
    }
    //base_rate->PrintRateTable();
    base_rate->InitRateTable();
    history_records.clear();
    setting = 1;

    //base_rate->PrintRateTable();
  }

//////
*/
	return rate;
}


