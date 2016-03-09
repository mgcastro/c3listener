define(["ajax"], function (ajax) {
    function update_input_fields () {
	ajax.get('server.json').then(function (server) {
	    document.getElementById('path_loss').value = server["path_loss"];
	    document.getElementById('haab').value = server["haab"];
	});
    }
    update_input_fields();
});
