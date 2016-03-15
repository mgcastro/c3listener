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
	    if (wired["proto"] === "dhcp") {
		var static_options_div = document.getElementById('static_options');
		document.getElementById("ip_type").value = "dhcp";
		static_options_div.style.display = 'none';
	    } else {
		document.getElementById("ip_type").value = "static";
	    }
	    document.getElementById("ip_addr").value = wired["ipaddr"];
	    document.getElementById("gateway").value = wired["gateway"];
	    document.getElementById("dns").value = wired["dns"];
	    var wireless = net["wireless"];
	    document.getElementById("ssid").value = wireless["ssid"];
	});
	ajax.get('server.json').then(function (server) {
	    document.getElementById("host").value = server["host"];
	    document.getElementById("port").value = server["port"];
	    document.getElementById("interval").value = server["interval"];
	});
    }
    function valid_ip (ip) {
	var octets = ip.split('.');
	if (octets.length != 4) {
	    return false;
	}
	for (var i = 0; i < octets.length; i++) {
	    if (!(/^[0-9]{1,3}$/.test(octets[i]))) {
		return false;
	    }
	    var tmp = parseInt(octets[i])
	    if (tmp > 255) {
		return false
	    }
	}
	return true;
    }
    function valid_mask (mask) {
	var mask_l = mask.split(".").length;
	if (mask_l == 4) {
	    return valid_ip(mask);
	}
	if (mask_l == 1) {
	    return (parseInt(mask) <= 32 && /^[0-9]{1,2}$/.test(mask));
	}
	return false;
    }
    function valid_ip_mask (ip_mask) {
	ip_mask = ip_mask.split('/');
	if (ip_mask.length != 2) {
	    return false;
	}
	return (valid_ip(ip_mask[0]) && valid_mask(ip_mask[1]));
    }
    function valid_dns(dns) {
	var ips = dns.split(','),
	    r_list = [];
	for (var i = 0; i < ips.length; i++) {
	    r_list.push(valid_ip(ips[i]));
	}
	return r_list.reduce(function (prev, cur) {
	    return (prev && cur);
	}, true);
    }
    function register_form_validator (form_el_id, f) {
	var form = document.getElementById(form_el_id);
	form.addEventListener("submit", function (evt) {
	    return f(form, evt);
	});
	form.addEventListener("change", function (evt) {
	    f(form);
	});
    }
    function validate_wired_form (form, evt) {
	var r_list = [];
	var els = form.getElementsByTagName('input')
	for (var i = 0, l = els.length; i < l; i++) {
	    var el = els[i];
	    var r = true;
	    if (el.name == "static_ip") {
		r = valid_ip_mask(el.value);
	    }
	    if (el.name == "gateway") {
		r = valid_ip(el.value);
	    }
	    if (el.name == "dns") {
		r = valid_dns(el.value);
	    }
	    if (!r) {
		el.parentElement.classList.add('has-error');
	    } else {
		el.parentElement.classList.remove('has-error');
	    }
	    r_list.push(valid_ip_mask(el.value));
	}
	var select = document.getElementById('ip_type');
	var static_needed = (select.value == 'dhcp') ? false : true;
	var static_valid = r_list.reduce(function (prev, cur) {
		return prev && cur;
	}, true);
	var valid = (!static_needed || static_valid);
	if (!valid && evt) {
	    evt.preventDefault();
	}
	return valid;
    }
    function valid_wpa_key (key) {
	var key_l = key.length;
	var valid_hex = /^[0-9a-fA-F]$/
	if (key_l < 8 || key_l > 64) {
	    return false;
	}
	if (key_l == 64) {
	    for (var i = 0; i < key_l; i++) {
		if (!valid_hex.test(key[key_l])) {
		    return false;
		}
	    }
	}
	return true;
    }
    function validate_wireless_form (form, evt) {
	alert("bob");
	var r_list = [];
	var els = form.getElementsByTagName('input')
	for (var i = 0, l = els.length; i < l; i++) {
	    var el = els[i];
	    var r = true;
	    if (el.name == "ssid") {
		r = el.validity.valid;
		console.log("SSID: ", r);
	    }
	    if (el.name == "wpa_key") {
		r = valid_wpa_key(el.value);
		console.log("Key: ", r);
	    }
	    if (!r) {
		el.parentElement.classList.add('has-error');
	    } else {
		el.parentElement.classList.remove('has-error');
	    }
	    r_list.push(valid_ip_mask(el.value));
	}
	var valid = r_list.reduce(function (prev, cur) {
		return prev && cur;
	}, true);
	if (!valid && evt) {
	    evt.preventDefault();
	}
	return valid;
    }
    function populate_svrstatus () {
	ajax.get('server.json').then(function (server) {
	    document.getElementById("unit-id").textContent = server["listener_id"];
	});
    }
    populate_svrstatus();
    
    track_wired_type();
    update_input_fields();
    register_form_validator("wired-form", validate_wired_form);
    register_form_validator("wireless-form", validate_wireless_form);

    return {"validate": validate_wireless_form};
});
