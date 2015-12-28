#include "monotonic_timer.h"


time_t MonotonicTimer::GetSec() const
{
	return timer.tv_sec;
}

long long MonotonicTimer::GetNSec() const
{
	return timer.tv_sec * 1e9 + timer.tv_nsec;
}


double MonotonicTimer::GetMSec() const
{
	double msec = double(timer.tv_nsec)/double(1e6);
	return timer.tv_sec * 1e3 + msec;
}

bool MonotonicTimer::operator==(const MonotonicTimer& t) const
{
	return (timer.tv_sec == t.timer.tv_sec && timer.tv_nsec == t.timer.tv_nsec);
}


bool MonotonicTimer::operator<(const MonotonicTimer& t) const
{
	if(timer.tv_sec < t.timer.tv_sec)
		return true;
	else if(timer.tv_sec == t.timer.tv_sec && timer.tv_nsec < t.timer.tv_nsec)
		return true;
	else
		return false;
}

bool MonotonicTimer::operator>(const MonotonicTimer& t) const
{
	if(timer.tv_sec > t.timer.tv_sec)
		return true;
	else if(timer.tv_sec == t.timer.tv_sec && timer.tv_nsec > t.timer.tv_nsec)
		return true;
	else
		return false;
}

bool MonotonicTimer::operator>=(const MonotonicTimer& t) const
{
	if(timer.tv_sec > t.timer.tv_sec)
		return true;
	else if(timer.tv_sec == t.timer.tv_sec && timer.tv_nsec >= t.timer.tv_nsec)
		return true;
	else
		return false;
}

bool MonotonicTimer::operator<=(const MonotonicTimer& t) const
{
	if(timer.tv_sec < t.timer.tv_sec)
		return true;
	else if(timer.tv_sec == t.timer.tv_sec && timer.tv_nsec <= t.timer.tv_nsec)
		return true;
	else
		return false;
}

MonotonicTimer& MonotonicTimer::operator=(const MonotonicTimer& t)
{
	if(this == &t)
		return *this;
	this->timer = {t.timer.tv_sec, t.timer.tv_nsec};
	return *this; 
}


MonotonicTimer& MonotonicTimer::operator-=(const MonotonicTimer& t)
{
	if(*this < t)
		cout<<"In monotonic_timer.cc: (operator-=)"<<"right value is larger than left value"<<endl;
	//assert(!(*this < t));
	this->timer.tv_sec-=t.timer.tv_sec;
	this->timer.tv_nsec-=t.timer.tv_nsec;
	if(this->timer.tv_nsec < 0)
		this->timer.tv_sec-=1, this->timer.tv_nsec+=BILLION;
	return *this;
}

MonotonicTimer& MonotonicTimer::operator+=(const MonotonicTimer& t)
{
	this->timer.tv_sec+=t.timer.tv_sec;
	this->timer.tv_nsec+=t.timer.tv_nsec;
	if(this->timer.tv_nsec >= BILLION)
		this->timer.tv_sec+=1, this->timer.tv_nsec-=BILLION;
	return *this;
}


const MonotonicTimer MonotonicTimer::operator+ (const MonotonicTimer& t1) const
{
	return MonotonicTimer(*this)+=t1;
}

const MonotonicTimer MonotonicTimer::operator- (const MonotonicTimer& t1) const
{
	//assert(!(*this < t1));
	if(*this < t1)
		cout<<"In monotonic_timer.cc: (operator-)"<<"left is larger than right!"<<endl;
	return MonotonicTimer(*this)-=t1;
}


MonotonicTimer& MonotonicTimer::operator/=(const int32_t n)
{
	this->timer.tv_nsec = this->timer.tv_nsec/n + (this->timer.tv_sec%n)*(BILLION/n);
	this->timer.tv_sec = this->timer.tv_sec/n;
	return *this;
}

const MonotonicTimer MonotonicTimer::operator/ (const int32_t n) const
{
	return MonotonicTimer(*this)/=n;
}


void MonotonicTimer::PrintTimer(bool line/* = false*/)
{ 
	cout<<this->timer.tv_sec<<","<<this->timer.tv_nsec;
	if(line)
		cout<<endl;
}
