/*
 * ratedsource.{cc,hh} -- generates configurable rated stream of packets.
 * Benjie Chen, Eddie Kohler (based on udpgen.o)
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "ratedsource.hh"
#include "confparse.hh"
#include "error.hh"
#include "timer.hh"
#include "router.hh"
#include "scheduleinfo.hh"
#include "glue.hh"

RatedSource::RatedSource()
{
  _packet = 0;
  add_output();
}

RatedSource *
RatedSource::clone() const
{
  return new RatedSource;
}

int
RatedSource::configure(const Vector<String> &conf, ErrorHandler *errh)
{
  String data = 
    "Random bullshit in a packet, at least 64 bytes long. Well, now it is.";
  unsigned rate = 10;
  int limit = -1;
  bool active = true;
  if (cp_va_parse(conf, this, errh,
		  cpOptional,
		  cpString, "packet data", &data,
		  cpUnsigned, "sending rate (packets/s)", &rate,
		  cpInteger, "total packet count", &limit,
		  cpBool, "active?", &active,
		  0) < 0)
    return -1;
  
  _data = data;
  _rate.set_rate(rate, errh);
  _limit = (limit >= 0 ? limit : NO_LIMIT);
  _active = active;
  
  if (_packet) _packet->kill();
  // note: if you change `headroom', change `click-align'
  unsigned int headroom = 16+20+24;
  _packet = Packet::make(headroom, (const unsigned char *)_data.data(), 
      			 _data.length(), 
			 Packet::default_tailroom(_data.length()));
  return 0;
}

int
RatedSource::initialize(ErrorHandler *errh)
{
  _count = 0;
  ScheduleInfo::join_scheduler(this, errh);
  return 0;
}

void
RatedSource::uninitialize()
{
  unschedule();
  _packet->kill();
  _packet = 0;
}

void
RatedSource::run_scheduled()
{
  if (!_active || (_limit != NO_LIMIT && _count >= _limit))
    return;
  
  struct timeval now;
  click_gettimeofday(&now);
  if (_rate.need_update(now)) {
    _rate.update();
    output(0).push(_packet->clone());
    _count++;
  }

  reschedule();
}


String
RatedSource::read_param(Element *e, void *vparam)
{
  RatedSource *rs = (RatedSource *)e;
  switch ((int)vparam) {
   case 0:			// data
    return rs->_data;
   case 1:			// rate
    return String(rs->_rate.rate()) + "\n";
   case 2:			// limit
    return (rs->_limit != NO_LIMIT ? String(rs->_limit) + "\n" : String("-1\n"));
   case 3:			// active
    return cp_unparse_bool(rs->_active) + "\n";
   case 4:			// count
    return String(rs->_count) + "\n";
   default:
    return "";
  }
}

int
RatedSource::change_param(const String &in_s, Element *e, void *vparam,
			  ErrorHandler *errh)
{
  RatedSource *rs = (RatedSource *)e;
  String s = cp_uncomment(in_s);
  switch ((int)vparam) {

   case 1: {			// rate
     unsigned rate;
     if (!cp_unsigned(s, &rate))
       return errh->error("rate parameter must be integer >= 0");
     if (rate > GapRate::MAX_RATE)
       // report error rather than pin to max
       return errh->error("rate too large; max is %u", GapRate::MAX_RATE);
     rs->_rate.set_rate(rate);
     rs->set_configuration_argument(1, in_s);
     break;
   }

   case 2: {			// limit
     int limit;
     if (!cp_integer(s, &limit))
       return errh->error("limit parameter must be integer");
     rs->_limit = (limit < 0 ? NO_LIMIT : limit);
     break;
   }
   
   case 3: {			// active
     bool active;
     if (!cp_bool(s, &active))
       return errh->error("active parameter must be boolean");
     rs->_active = active;
     if (!rs->scheduled() && active) {
       rs->_rate.reset();
       rs->reschedule();
     }
     break;
   }

   case 5: {			// reset
     rs->_count = 0;
     rs->_rate.reset();
     if (!rs->scheduled() && rs->_active)
       rs->reschedule();
     break;
   }

  }
  return 0;
}

void
RatedSource::add_handlers()
{
  add_read_handler("data", read_param, (void *)0);
  add_write_handler("data", reconfigure_write_handler, (void *)0);
  add_read_handler("rate", read_param, (void *)1);
  add_write_handler("rate", change_param, (void *)1);
  add_read_handler("limit", read_param, (void *)2);
  add_write_handler("limit", change_param, (void *)2);
  add_read_handler("active", read_param, (void *)3);
  add_write_handler("active", change_param, (void *)3);
  add_read_handler("count", read_param, (void *)4);
  add_write_handler("reset", change_param, (void *)5);
}

EXPORT_ELEMENT(RatedSource)
