/*
 * bandwidthshaper.{cc,hh} -- element limits number of successful pulls per
 * second to a given rate (bytes/s)
 * Benjie Chen, Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology.
 * Copyright (c) 2000 Mazu Networks, Inc.
 *
 * This software is being provided by the copyright holders under the GNU
 * General Public License, either version 2 or, at your discretion, any later
 * version. For more information, see the `COPYRIGHT' file in the source
 * distribution.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "bandwidthshaper.hh"

BandwidthShaper *
BandwidthShaper::clone() const
{
  return new BandwidthShaper;
}

Packet *
BandwidthShaper::pull(int)
{
  Packet *p = 0;
  struct timeval now;
  click_gettimeofday(&now);
  if (_rate.need_update(now)) {
    if ((p = input(0).pull()))
      _rate.update_with(p->length());
  }
  return p;
}

EXPORT_ELEMENT(BandwidthShaper)
