#ifndef MONOTONIC_TIMER_
#define MONOTONIC_TIMER_

#include "cpp_lib.h"
#include "c_lib.h"


#define BILLION 1000000000


/**
 * rule 1: all the timer should be >= 0
 */

class MonotonicTimer
{
	struct timespec timer;
public:
	/**
	 * Default Constructor, recording current time
	 */
	MonotonicTimer() {clock_gettime(CLOCK_MONOTONIC, &timer);};
	/**
	 * Constructor
	 */
	MonotonicTimer(time_t s, long long ns)
	{
		if(ns > BILLION || s<0 && ns<0)
			cout<<"invalid timer\n";
		timer.tv_sec = s;
		timer.tv_nsec = ns;
	};
	MonotonicTimer(struct timespec t) {timer.tv_sec = t.tv_sec; timer.tv_nsec = t.tv_nsec;};
	MonotonicTimer(long long ns) { timer.tv_sec = ns / 1e9; timer.tv_nsec = ns % long(1e9); }
	~MonotonicTimer() { };

	time_t GetSec() const;
	long long GetNSec() const;
	double GetMSec() const;

	/**
	* All comparisons
	*/
	bool operator==(const MonotonicTimer& t) const;
	bool operator<(const MonotonicTimer& t) const;
	bool operator>(const MonotonicTimer& t) const;
	bool operator<=(const MonotonicTimer& t) const;
	bool operator>=(const MonotonicTimer& t) const;

	MonotonicTimer& operator= (const MonotonicTimer& t);	
	MonotonicTimer& operator-= (const MonotonicTimer& t);
	MonotonicTimer& operator+= (const MonotonicTimer& t);

	const MonotonicTimer operator+ (const MonotonicTimer& t1) const;
	const MonotonicTimer operator- (const MonotonicTimer& t1) const;

	MonotonicTimer& operator/= (const int32_t n);
	const MonotonicTimer operator/ (const int32_t n) const;

	void PrintTimer(bool line = false);
};

#endif
