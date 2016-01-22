#ifndef GPS_PARSER_H_
#define GPS_PARSER_H_

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <iostream>
#include "pthread_wrapper.h"

struct Location {
  double latitude;
  double longitude;
};

using namespace std;

class GPSParser {
 public:
  GPSParser();
  ~GPSParser();

  bool ParseLine(const string &line);

  double time() const { return time_; }

  double speed() const { return speed_; }

  const Location& location() const { return location_; }

  void Print();

 private:
  void* ParseGPSReadings(void*);  

  void SplitLine(const string &line, const string &separator=" ,\t");

  bool IsValidLine();

  bool GetValue(const string &phrase, const string &key, double *val);

  string kStartPhrase;
  double time_;
  Location location_;
  double speed_;
  vector<string> phrases_;
};

#endif
