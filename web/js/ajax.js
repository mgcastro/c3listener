define(function() {
    var prefix='/json/';
    'use strict';
    function request(method, url, data, json_p) {
	var promise = new Promise(function(resolve, reject) {
	    var req = new XMLHttpRequest();
	    req.open(method, prefix+url);
	    if (json_p == true) {
		data = JSON.stringify(data);
		req.setRequestHeader('Content-Type', 'application/json');
	    }
	    req.onload = function() {
		if (req.status >= 200 && req.status <= 300) {
		    if (req.status != 204 && req.status != 201) {
			resolve(JSON.parse(req.response));
		    } else {
			resolve(null);
		    }
		}
		else {
		    reject(req.response);
		}
	    };
	    req.onerror = function() {
		reject(Error("Network Error"));
	    };
	    req.send(data);
	});
	return promise;
    };
    return {
	'post': function(url, data) {
	    return request('POST', url, data);
	},
	'get': function(url) {
	    return request('GET', url);
	},
	'put': function(url, data) {
	    return request('PUT', url, data);
	},
	'delete': function(url) {
	    return request('DELETE', url);
	},
	'post_json': function(url, data) {
	    return request('POST', url, data, true);
	},
	'put_json': function(url, data) {
	    return request('PUT', url, data, true);
	},
    }
});
