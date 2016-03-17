define(['ajax'], function(ajax) {
    'use strict';
    function update_beacon_table (beacons) {
	var table = document.getElementById('beacons_table');
	var old_tbody = table.getElementsByTagName('tbody')[0];
	var new_tbody = document.createElement('tbody');
	for (var i = 0, l = beacons.length; i < l; i++) {
	    var beacon = beacons[i];
	    var row = document.createElement('tr');
	    row.innerHTML += "<td>"+beacon.major+" / "+beacon.minor+"</td>";
            row.innerHTML += "<td>"+Math.round(beacon.distance*100)/100+"m</td>";
	    row.innerHTML += "<td>"+Math.round(Math.sqrt(beacon.error)*100)/100+"m</td>";
	    new_tbody.appendChild(row);
	}
	table.replaceChild(new_tbody, old_tbody);
	window.setTimeout(fetch_beacons, 2500);
    }
    function fetch_beacons() {
	ajax.get_json('beacons.json').then(update_beacon_table, function (resp) {
	    console.log("Failed to get recent beacons:", resp);
	    window.setTimeout(fetch_beacons, 5000);
	});
    }
    
    function pretty_print_uci_key (key) {
	var map = {
	    "ifname": "Interface",
	    "proto": "IP Type",
	    "ipaddr": "IP Address",
	    "netmask": "Netmask",
	    "gateway": "Gateway",
	    "dns": "DNS Servers",
	    "device": "Device",
	    "network": "Network",
	    "mode": "Mode",
	    "ssid": "SSID",
	    "encryption": "Encryption"};
	if (map.hasOwnProperty(key)) {
	    return map[key];
	}
	return key;
    }
    
    function populate_netstatus () {
	ajax.get_json('net_status.json').then(
	    function (net) {
		for (var type in net) {
		    if (net.hasOwnProperty(type)) {
			var dl = document.getElementById(type+"_status");
			dl.innerHTML = "";
			for (var prop in net[type]) {
			    if (net[type].hasOwnProperty(prop)){
				dl.innerHTML += "<dt>"+pretty_print_uci_key(prop)+"</dt>";
				dl.innerHTML += "<dd>"+net[type][prop]+"</dd>";
			    }
			}
		    }
		}
		var wireless_status = document.getElementById('wireless_status');
		
	    },
	    function (resp) {
	    });
    }
    function populate_svrstatus () {
	ajax.get_json('server.json').then(function (server) {
	    var dl = document.getElementById("server_status");
	    dl.innerHTML = "<dt>Hostname</dt><dd>"+server["host"]+"</dd>";
	    dl.innerHTML += "<dt>Last ACK</dt><dd>"+server["last_seen"]+"</dd>";
	    document.getElementById("unit-id").textContent = server["listener_id"];
	});
    }
    populate_netstatus();
    populate_svrstatus();
    fetch_beacons();
});
