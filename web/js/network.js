define(['ajax'], function (ajax) {
    function track_wired_type () {
	var select = document.getElementById('ip_type');
	select.addEventListener("change", function () {
	    var static_options_div = document.getElementById('static_options');
	    if (select.value == "dhcp") {
		static_options_div.style.display = "none";
	    } else {
		static_options_div.style.display = "initial";
	    }
	});
    }
    function update_input_fields () {
	ajax.get('net_status.json').then(function (net) {
	    var wired = net["wired"];
	    if (wired["Type"] === "DHCP") {
		var static_options_div = document.getElementById('static_options');
		document.getElementById("ip_type").value = "dhcp";
		static_options_div.style.display = 'none';
	    }
	    document.getElementById("ip_addr").value = wired["IP Address"];
	    document.getElementById("gateway").value = wired["Gateway"];
	    document.getElementById("dns").value = wired["DNS"];
	    var wireless = net["wireless"];
	    document.getElementById("ssid").value = wireless["SSID"];
	});
	ajax.get('server.json').then(function (server) {
	    document.getElementById("host").value = server["host"];
	    document.getElementById("port").value = server["port"];
	    document.getElementById("interval").value = server["interval"];
	});
    };
    
    track_wired_type();
    update_input_fields();
});
