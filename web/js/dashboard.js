define(['ajax'], function(ajax) {
    'use strict';
    function update_beacon_table (beacons) {
	var table = document.getElementById('beacons_table');
	var old_tbody = table.getElementsByTagName('tbody')[0];
	var new_tbody = document.createElement('tbody');
	for (var i = 0, l = beacons.length; i < l; i++) {
	    var beacon = beacons[i];
	    var row = document.createElement('tr');
	    row.innerHTML += "<td>"+beacon.major+"/"+beacon.minor+"</td>";
            row.innerHTML += "<td>"+beacon.distance+"</td>";
	    row.innerHTML += "<td>"+beacon.error+"</td>";
	    new_tbody.appendChild(row);
	}
	table.replaceChild(new_tbody, old_tbody);
	window.setTimeout(fetch_beacons, 1000);
    }
    function fetch_beacons() {
	ajax.get('beacons.json').then(update_beacon_table, function (resp) {
	    console.log("Failed to get recent beacons:", resp);
	    window.setTimeout(fetch_beacons, 1000);
	});
    }
    function populate_netstatus () {
	ajax.get('net_status.json').then(
	    function (net) {
		for (var type in net) {
		    if (net.hasOwnProperty(type)) {
			var dl = document.getElementById(type+"_status");
			dl.innerHTML = "";
			for (var prop in net[type]) {
			    if (net[type].hasOwnProperty(prop)){
				dl.innerHTML += "<dt>"+prop+"</dt>";
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
	ajax.get('server.json').then(function (server) {
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
