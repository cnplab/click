#ifndef RATEDUNQUEUE_HH
#define RATEDUNQUEUE_HH
#include "element.hh"
#include "gaprate.hh"

/*
 * =c
 * RatedUnqueue(RATE)
 * =s pull-to-push converter
 * V<packet scheduling>
 * =d
 * 
 * Pulls packets at the given RATE in packets per second, and pushes them out
 * its single output.
 *
 * =a BandwidthRatedUnqueue, Unqueue, Shaper, RatedSplitter */

class RatedUnqueue : public Element { protected:

  GapRate _rate;

 public:
  
  RatedUnqueue() : Element(1, 1)		{ }
  
  const char *class_name() const		{ return "RatedUnqueue"; }
  const char *processing() const		{ return PULL_TO_PUSH; }
  
  RatedUnqueue *clone() const			{ return new RatedUnqueue; }
  int configure(const Vector<String> &, ErrorHandler *);
  int initialize(ErrorHandler *);
  void uninitialize();
  void add_handlers();
  
  void run_scheduled();

  unsigned rate() const				{ return _rate.rate(); }
  void set_rate(unsigned, ErrorHandler * = 0);
  
};

#endif
