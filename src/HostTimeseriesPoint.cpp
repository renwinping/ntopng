/*
 *
 * (C) 2013-20 - ntop.org
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "ntop_includes.h"

/* *************************************** */

HostTimeseriesPoint::HostTimeseriesPoint(const LocalHostStats * const hs) : TimeseriesPoint() {
  Host *host = hs->getHost();
  host_stats = new TimeseriesStats(*hs);
  dns = hs->getDNSstats() ? new DnsStats(*hs->getDNSstats()) : NULL;
  icmp = NULL;

  if(hs->getICMPStats() && (hs->getHost()->get_ip()->isIPv4())) {
    icmp = (ts_icmp_stats*) calloc(1, sizeof(ts_icmp_stats));

    if(icmp)
      hs->getICMPStats()->getTsStats(icmp);
  }

  active_flows_as_client = host->getNumOutgoingFlows();
  active_flows_as_server = host->getNumIncomingFlows();
  contacts_as_client = host->getNumActiveContactsAsClient();
  contacts_as_server = host->getNumActiveContactsAsServer();
  engaged_alerts = host->getNumTriggeredAlerts();
  tcp_packet_stats_sent = *host->getTcpPacketSentStats();
  tcp_packet_stats_rcvd = *host->getTcpPacketRcvdStats();
}

/* *************************************** */

HostTimeseriesPoint::~HostTimeseriesPoint() {
  delete host_stats;
  if(dns) delete dns;
  if(icmp) free(icmp);
}

/* *************************************** */

/* NOTE: Return only the minimal information needed by the timeseries
 * to avoid slowing down the periodic scripts too much! */
void HostTimeseriesPoint::lua(lua_State* vm, NetworkInterface *iface) {
  host_stats->luaStats(vm, iface, true /* host details */, true /* verbose */, true /* tsLua */);

  lua_push_uint64_table_entry(vm, "active_flows.as_client", active_flows_as_client);
  lua_push_uint64_table_entry(vm, "active_flows.as_server", active_flows_as_server);
  lua_push_uint64_table_entry(vm, "contacts.as_client", contacts_as_client);
  lua_push_uint64_table_entry(vm, "contacts.as_server", contacts_as_server);
  lua_push_uint64_table_entry(vm, "engaged_alerts", engaged_alerts);

  tcp_packet_stats_sent.lua(vm, "tcpPacketStats.sent");
  tcp_packet_stats_rcvd.lua(vm, "tcpPacketStats.rcvd");

  if(dns) dns->lua(vm, false /* NOT verbose */);

  if(icmp) {
    lua_push_uint64_table_entry(vm, "icmp.echo_pkts_sent", icmp->echo_packets_sent);
    lua_push_uint64_table_entry(vm, "icmp.echo_pkts_rcvd", icmp->echo_packets_rcvd);
    lua_push_uint64_table_entry(vm, "icmp.echo_reply_pkts_sent", icmp->echo_reply_packets_sent);
    lua_push_uint64_table_entry(vm, "icmp.echo_reply_pkts_rcvd", icmp->echo_reply_packets_rcvd);
  }
}

string HostTimeseriesPoint::json()
{
	char tempChar[256] = "";
	snprintf(tempChar, 255, "%u", active_flows_as_client);
	return tempChar;
	json_object* point = toJsonObject();
	if (point)
	{
		/* JSON string */
		char* rsp = strdup(json_object_to_json_string(point));

		/* Free memory */
		json_object_put(point);

		free(rsp);
	}else
	{
		return "";
	}
}

json_object* HostTimeseriesPoint::toJsonObject(){
	json_object *my_object;
	char buf[64], jsonbuf[64], *c;
	time_t t;

	if ((my_object = json_object_new_object()) == NULL) return(NULL);

	json_object_object_add(my_object, "@timestamp", json_object_new_string(buf));

	return(my_object);
}

// char* HostTimeseriesPoint::serialize() {
// 	json_object *my_object;
// 	char *rsp;
// 
// 	if ((my_object = toJsonObject()) != NULL) {
// 
// 		/* JSON string */
// 		rsp = strdup(json_object_to_json_string(my_object));
// 
// 		/* Free memory */
// 		json_object_put(my_object);
// 	}
// 	else
// 		rsp = NULL;
// 	return(rsp);
// }
