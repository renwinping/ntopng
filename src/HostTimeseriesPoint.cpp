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

string HostTimeseriesPoint::json(NetworkInterface *iface)
{
	string sJson = "";
	json_object* point = toJsonObject(iface);
	if (point)
	{
		/* JSON string */
		char* rsp = strdup(json_object_to_json_string(point));

		/* Free memory */
		json_object_put(point);

		sJson = rsp;
		free(rsp);
	}
	return sJson;
}


std::string HostTimeseriesPoint::json()
{
	return "";
}

json_object* HostTimeseriesPoint::toJsonObject(NetworkInterface *iface){
	json_object *my_object;
	char buf[64], jsonbuf[64], *c;
	time_t t;

	if ((my_object = json_object_new_object()) == NULL) return(NULL);

	json_object_object_add(my_object, "timestamp", json_object_new_int64(timestamp));
	Host* pHost = host_stats->getHost();

	if (pHost)
	{
		char buf[64], buf_id[64], *host_id = buf_id;
		char ip_buf[64], *ipaddr = NULL;
		json_object_object_add(my_object, "ip", json_object_new_string(pHost->get_ip()->print(ip_buf,sizeof(ip_buf))));
		json_object_object_add(my_object, "mac", json_object_new_string(pHost->getMac()->print(buf, sizeof(buf))));
		json_object_object_add(my_object, "vlan", /*vlan_id*/json_object_new_int64(pHost->get_vlan_id()));
		json_object_object_add(my_object, "ipkey", json_object_new_int64(pHost->get_ip()->key()));
		json_object_object_add(my_object, "name", json_object_new_string(pHost->get_visual_name(buf, sizeof(buf))));
	}


	if (iface)
	{
		json_object* _hostJson = host_stats->getJSONObject(iface, true, false, true);
		if (_hostJson) {
			json_object_object_add(my_object, "hostJson", _hostJson);
		}
	}


	json_object* tcp_packet_stats_sent_json = tcp_packet_stats_sent.getJSONObject();
	if (tcp_packet_stats_sent_json) {
		json_object_object_add(my_object, "tcpPacketStats.sent", tcp_packet_stats_sent_json);
	}

	json_object* tcp_packet_stats_rcvd_json = tcp_packet_stats_rcvd.getJSONObject();
	if (tcp_packet_stats_rcvd_json) {
		json_object_object_add(my_object, "tcpPacketStats.rcvd", tcp_packet_stats_rcvd_json);
	}

	if (dns)
	{
		json_object* dns_json = dns->getJSONObject();
		if (dns_json) {
			json_object_object_add(my_object, "dns", dns_json);
		}
	}

	if (icmp) {
		//to-do 
	}

	return(my_object);
}

