/*
 * ipaddrpairrewriter.{cc,hh} -- rewrites packet source and destination
 * Eddie Kohler
 *
 * Copyright (c) 2004 Regents of the University of California
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>
#include "iprw.hh"
#include "ipaddrpairrewriter.hh"
#include <clicknet/ip.h>
#include <click/confparse.hh>
#include <click/straccum.hh>
#include <click/error.hh>
#include <click/llrpc.h>
#include <click/router.hh>
CLICK_DECLS

void
IPAddrPairRewriter::IPAddrPairMapping::apply(WritablePacket *p)
{
    click_ip *iph = p->ip_header();
    assert(iph);
  
    // IP header
    iph->ip_src = _mapto.saddr();
    iph->ip_dst = _mapto.daddr();
    if (_dst_anno)
	p->set_dst_ip_anno(_mapto.daddr());

    uint32_t sum = (~iph->ip_sum & 0xFFFF) + _ip_csum_delta;
    sum = (sum & 0xFFFF) + (sum >> 16);
    iph->ip_sum = ~(sum + (sum >> 16));
  
    mark_used();
}

String
IPAddrPairRewriter::IPAddrPairMapping::unparse() const
{
    IPFlowID rev_rev = reverse()->flow_id().rev();
    StringAccum sa;
    sa << '(' << rev_rev.saddr() << ", " << rev_rev.daddr() << ") => ("
       << flow_id().saddr() << ", " << flow_id().daddr() << ") ["
       << output() << ']';
    return sa.take_string();
}

IPAddrPairRewriter::IPAddrPairRewriter()
    : _map(0), _timer(this)
{
    // no MOD_INC_USE_COUNT; rely on IPRw
}

IPAddrPairRewriter::~IPAddrPairRewriter()
{
    // no MOD_DEC_USE_COUNT; rely on IPRw
}

void *
IPAddrPairRewriter::cast(const char *n)
{
    if (strcmp(n, "IPRw") == 0)
	return (IPRw *)this;
    else if (strcmp(n, "IPAddrPairRewriter") == 0)
	return (IPAddrPairRewriter *)this;
    else
	return 0;
}

void
IPAddrPairRewriter::notify_noutputs(int n)
{
    if (n < 1)
	set_noutputs(1);
    else if (n > 256)
	set_noutputs(256);
    else
	set_noutputs(n);
}

int
IPAddrPairRewriter::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (conf.size() == 0)
	return errh->error("too few arguments; expected 'INPUTSPEC, ...'");
    set_ninputs(conf.size());

    int before = errh->nerrors();
    for (int i = 0; i < conf.size(); i++) {
	InputSpec is;
	if (parse_input_spec(conf[i], is, "input spec " + String(i), errh) >= 0)
	    _input_specs.push_back(is);
    }
    return (errh->nerrors() == before ? 0 : -1);
}

int
IPAddrPairRewriter::initialize(ErrorHandler *)
{
    _timer.initialize(this);
    _timer.schedule_after_ms(GC_INTERVAL_SEC * 1000);

    // release memory to system on cleanup
    _map.set_arena(router()->arena_factory());
  
    return 0;
}

void
IPAddrPairRewriter::cleanup(CleanupStage)
{
    clear_map(_map);

    for (int i = 0; i < _input_specs.size(); i++)
	if (_input_specs[i].kind == INPUT_SPEC_PATTERN)
	    _input_specs[i].u.pattern.p->unuse();
    _input_specs.clear();
}

int
IPAddrPairRewriter::notify_pattern(Pattern *p, ErrorHandler *errh)
{
    if (!p->allow_nat())
	return errh->error("IPAddrPairRewriter cannot accept IPRewriter patterns");
    return IPRw::notify_pattern(p, errh);
}

void
IPAddrPairRewriter::run_timer()
{
    clean_map(_map, GC_INTERVAL_SEC * 1000);
    _timer.schedule_after_ms(GC_INTERVAL_SEC * 1000);
}

IPAddrPairRewriter::IPAddrPairMapping *
IPAddrPairRewriter::apply_pattern(Pattern *pattern, int,
			      const IPFlowID &in_flow, int fport, int rport)
{
    assert(fport >= 0 && fport < noutputs() && rport >= 0 && rport < noutputs());

    IPFlowID flow(in_flow.saddr(), 0, in_flow.daddr(), 0);
    IPAddrPairMapping *forward = new IPAddrPairMapping(true);
    IPAddrPairMapping *reverse = new IPAddrPairMapping(true);

    if (forward && reverse) {
	if (!pattern)
	    Mapping::make_pair(0, flow, flow, fport, rport, forward, reverse);
	else if (!pattern->create_mapping(0, flow, fport, rport, forward, reverse, _map))
	    goto failure;

	IPFlowID reverse_flow = forward->flow_id().rev();
	_map.insert(flow, forward);
	_map.insert(reverse_flow, reverse);
	return forward;
    }

  failure:
    delete forward;
    delete reverse;
    return 0;
}

void
IPAddrPairRewriter::push(int port, Packet *p_in)
{
    WritablePacket *p = p_in->uniqueify();
    click_ip *iph = p->ip_header();
  
    IPFlowID flow(iph->ip_src, 0, iph->ip_dst, 0);
    IPAddrPairMapping *m = static_cast<IPAddrPairMapping *>(_map.find(flow));

    if (!m) {			// create new mapping
	const InputSpec &is = _input_specs[port];
	switch (is.kind) {

	  case INPUT_SPEC_NOCHANGE:
	    output(is.u.output).push(p);
	    return;

	  case INPUT_SPEC_DROP:
	    break;

	  case INPUT_SPEC_KEEP:
	  case INPUT_SPEC_PATTERN: {
	      Pattern *pat = is.u.pattern.p;
	      int fport = is.u.pattern.fport;
	      int rport = is.u.pattern.rport;
	      m = IPAddrPairRewriter::apply_pattern(pat, 0, flow, fport, rport);
	      break;
	  }

	  case INPUT_SPEC_MAPPER: {
	      m = static_cast<IPAddrPairMapping*>(is.u.mapper->get_map(this, 0, flow, p));
	      break;
	  }
     
	}
	if (!m) {
	    p->kill();
	    return;
	}
    }
  
    m->apply(p);
    output(m->output()).push(p);
}


String
IPAddrPairRewriter::dump_mappings_handler(Element *e, void *)
{
    IPAddrPairRewriter *rw = (IPAddrPairRewriter *)e;
  
    StringAccum sa;
    for (Map::iterator iter = rw->_map.begin(); iter; iter++) {
	Mapping *m = iter.value();
	if (m->is_primary())
	    sa << m->unparse() << "\n";
    }
    return sa.take_string();
}

String
IPAddrPairRewriter::dump_nmappings_handler(Element *e, void *)
{
    IPAddrPairRewriter *rw = (IPAddrPairRewriter *)e;
    return String(rw->_map.size()) + "\n";
}

String
IPAddrPairRewriter::dump_patterns_handler(Element *e, void *)
{
    IPAddrPairRewriter *rw = (IPAddrPairRewriter *)e;
    String s;
    for (int i = 0; i < rw->_input_specs.size(); i++)
	if (rw->_input_specs[i].kind == INPUT_SPEC_PATTERN)
	    s += rw->_input_specs[i].u.pattern.p->unparse() + "\n";
    return s;
}

void
IPAddrPairRewriter::add_handlers()
{
    add_read_handler("mappings", dump_mappings_handler, (void *)0);
    add_read_handler("nmappings", dump_nmappings_handler, (void *)0);
    add_read_handler("patterns", dump_patterns_handler, (void *)0);
}

ELEMENT_REQUIRES(IPRw IPRewriterPatterns)
EXPORT_ELEMENT(IPAddrPairRewriter)
#include <click/bighashmap.cc>
#include <click/vector.cc>
CLICK_ENDDECLS