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

void NetworkInterfaceTsPoint::lua(lua_State* vm, NetworkInterface *iface) {
  ndpi.lua(iface, vm, true /* with categories */);
  local_stats.lua(vm);
  tcpPacketStats.lua(vm, "tcpPacketStats");
  packetStats.lua(vm, "pktSizeDistribution");

  lua_newtable(vm);
  lua_push_uint64_table_entry(vm, "hosts", hosts);
  lua_push_uint64_table_entry(vm, "local_hosts", local_hosts);
  lua_push_uint64_table_entry(vm, "devices", devices);
  lua_push_uint64_table_entry(vm, "flows", flows);
  lua_push_uint64_table_entry(vm, "http_hosts", http_hosts);
  lua_push_uint64_table_entry(vm, "engaged_alerts", engaged_alerts);
  l4Stats.luaStats(vm);
  lua_pushstring(vm, "stats");
  lua_insert(vm, -2);
  lua_settable(vm, -3);
}

string NetworkInterfaceTsPoint::json(NetworkInterface *iface)
{
	string sJson = "";
	json_object *my_object;
	char *rsp;

	if ((my_object = toJsonObject(iface)) != NULL) {

		/* JSON string */
		rsp = strdup(json_object_to_json_string_ext(my_object, JSON_C_TO_STRING_PLAIN));

		/* Free memory */
		json_object_put(my_object);

		if (rsp)		{
			sJson = rsp;
			free(rsp);
		}
	}
	return sJson;
}


std::string NetworkInterfaceTsPoint::json()
{
	return "";
}

json_object* NetworkInterfaceTsPoint::toJsonObject(NetworkInterface *iface)
{
	json_object *my_object;
	char buf[64], jsonbuf[64], *c;
	time_t t;
	if ((my_object = json_object_new_object()) == NULL) return(NULL);

	//时间JSON
	//snprintf(buf, sizeof(buf), "%u", this->timestamp);


	json_object_object_add(my_object, "timestamp", json_object_new_int64(timestamp));
	json_object_object_add(my_object, "pre_timestamp", json_object_new_int64(pretimestamp));
	json_object_object_add(my_object, "INTERFACE", json_object_new_string(iface->get_name()));
	json_object_object_add(my_object, "packets", json_object_new_int64(ethStats.getNumPackets()));
	json_object_object_add(my_object, "bytes", json_object_new_int64(ethStats.getNumBytes()));
#ifdef DELTA_STATS_VALUE
	float delta = abs(timestamp - pretimestamp);//转换为秒时间
	float thpt = ethStats.getNumBytes() / delta;
	json_object_object_add(my_object, "throughput_bps", json_object_new_double(thpt));
	float pps_thpt = ethStats.getNumPackets() / delta;
	json_object_object_add(my_object, "throughput_pps", json_object_new_double(pps_thpt));
#else
	json_object_object_add(my_object, "throughput_bps", json_object_new_double(bytes_thpt.getThpt()));
	json_object_object_add(my_object, "throughput_pps", json_object_new_double(pkts_thpt.getThpt()));
#endif

	json_object_object_add(my_object, "hosts", json_object_new_int64(hosts));
	//json_object_object_add(my_object, "local_hosts", json_object_new_int64(local_hosts));
	//json_object_object_add(my_object, "devices", json_object_new_int64(devices));
	json_object_object_add(my_object, "flows", json_object_new_int64(flows));

	static u_int32_t if_send_json_id = 0;
	json_object_object_add(my_object, "json_id", json_object_new_int64(if_send_json_id++));

	//添加ndpi统计JSON
// 	json_object*  ndpi_json = ndpi.getJSONObject(if);
// 	if (ndpi_json)	{
// 		json_object_object_add(my_object, "ndpi_stats",ndpi_json);
// 	}
	//添加ndpi统计JSON
// 	json_object*  local_stats_json = local_stats.getJSONObject();
// 	if (local_stats_json) {
// 		json_object_object_add(my_object, "local_stats", local_stats_json);
// 	}

	json_object*  tcpPacketStats_json = tcpPacketStats.getJSONObject();
	if (tcpPacketStats_json) {
		json_object_object_add(my_object, "tcpPacketBadStats", tcpPacketStats_json);
	}

	json_object*  packetStats_json = packetStats.getJSONObject();
	if (packetStats_json) {
		json_object_object_add(my_object, "packetStats", packetStats_json);
	}

	json_object*  l4Stats_json = json_object_new_object();
	l4Stats.serialize(l4Stats_json);
	if (l4Stats_json) {
		json_object_object_add(my_object, "l4Stats", l4Stats_json);
	}

	if (iface)
	{
		json_object* ndpi_json = ndpi.getJSONObject(iface,false);
		if (ndpi_json) {
			json_object_object_add(my_object, "ndpi", ndpi_json);
		}
	}

	return my_object;
}

char* NetworkInterfaceTsPoint::serialize() {
	json_object *my_object;
	char *rsp;

	if ((my_object = toJsonObject(NULL)) != NULL) {

		/* JSON string */
		rsp = strdup(json_object_to_json_string(my_object));

		/* Free memory */
		json_object_put(my_object);
	}
	else
		rsp = NULL;
	return(rsp);
}

void NetworkInterfaceTsPoint::setPreTime(time_t oldtime)
{
	pretimestamp = oldtime;
}
