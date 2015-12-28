#ifndef _TIME_UTIL_H_
#define _TIME_UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <string>
#include <iostream>

#define TIME_MAXSIZE 100

class TIME
{
public:
	TIME() {}
	~TIME() {}

	TIME(int sec, int usec)
	{
		d_time.tv_sec = sec;
		d_time.tv_usec = usec;
	}

	TIME(const TIME &other)
	{
		d_time = other.d_time;
		strncpy(d_timestamp, other.d_timestamp, TIME_MAXSIZE);
	}

	void GetCurrTime()
	{
		gettimeofday(&d_time, NULL);
	}

	char* CvtCurrTime(); //Return the readable time format in string

	/*Calculate time duration in us*/
	friend double operator-(TIME end, TIME start)
	{
		double interval = ((end.d_time.tv_sec - start.d_time.tv_sec)*1000000.0 + (end.d_time.tv_usec - start.d_time.tv_usec));
		return interval;
	}

private:
	struct timeval d_time; /*Current time*/
	char d_timestamp[TIME_MAXSIZE];
};

#endif

