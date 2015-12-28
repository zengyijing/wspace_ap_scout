#include "time_util.h"
/*
int main(int argc, char **argv)
{
	TIME start, end;
	start.getCurrTime();
	sleep(1);
	end.getCurrTime();
	double duration = end - start;
	cout << "start: " << start.cvtCurrTime() << " end: " << end.cvtCurrTime() << " Duration: " << duration << endl;	
	return 0;
}
*/

/*
Purpose: Return a string of the readable format for the current time
*/
char* TIME::CvtCurrTime()
{
	char time_string[TIME_MAXSIZE];
	struct tm* ptm;
	ptm = localtime(&d_time.tv_sec);
	strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", ptm);
	snprintf(d_timestamp, TIME_MAXSIZE, "%s.%06ld", time_string, (long)d_time.tv_usec);
	return (char*)d_timestamp;
}
