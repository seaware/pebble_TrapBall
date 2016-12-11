// TrapBall - (c)Copyright 2014 Frank Canova @ Seaware
// Portions of C code were provided by Pebble in the feature_accel_discs.c example.


var player_name = localStorage.getItem("player_name");
if (typeof player_name === 'undefined' || !player_name) {
	player_name = "";
}

var player_id = localStorage.getItem("player_id");
if (typeof player_id === 'undefined' || !player_id) {
	player_id = 0;
}

//var userstat;
var summary = "";

function uuid()
{
	return "3aa643c5-acd4-4897-9213-5e6812da3857";
}

function baseURL()
{
	return "http://canova3.com/mobilestat/index.php/";
}

Pebble.addEventListener("appmessage", function(e) {
	console.log("Received message from pebble");

	if (e.payload.post_score) {
		console.log("Score="+e.payload.post_score+" Level="+e.payload.post_level);
		postPlayerScore(e.payload.post_score, e.payload.post_level);
	}
});

function postPlayerScore(score,level){
	
  var response;
  var response_parts = new Array();
  var req = new XMLHttpRequest();
	
	console.log("Doing setscore HTTP request...");
	
  // build the GET request
  req.open('GET', baseURL() + "api/setscore/" + uuid() + "/" + Pebble.getAccountToken() + "/" + score + "/" + level + "/" + player_id, true);
  req.onload = function(e) {
    if (req.readyState == 4) {
      // 200 - HTTP OK
      if(req.status == 200) {
		response_parts = req.responseText.split("<",1);
        response = JSON.parse(response_parts[0]);
		console.log("Web reply:" + response_parts[0]);
		
		summary = "";
		if (response.u_name == "null") response.u_name = '';
		if (response.player_rank)
		{
			summary = "This ranks " + response.player_rank + " among the " + response.n + " games everyone has played so far.\n\n"
		}
		if (response.top)
		{
			var i = 1;
			summary += "The top games are:\n";
			response.top.forEach( function(top_player)
			{
				summary += i + ". " + top_player.u_name + " scored " + top_player.score + " on level " + top_player.level + "\n";
				i++;
			});
		}
		if (localStorage.getItem("player_name") == "")
			summary += "\nNote that you can update your player name in the settings for this Watchapp.\n\n"
		 response.summary = summary;
		 sendToPebble(response);
        }
      } else {
        console.log("Web request returned error code " + req.status.toString());
      }
    }
  req.send(null);
};

Pebble.addEventListener("ready", function() {
	console.log("TrapBall JS ready!");
	console.log("Pebble Account Token: " + Pebble.getAccountToken());
	getUserStats();
});


function getUserStats(){
	
  var response;
  var response_parts = new Array();
  var req = new XMLHttpRequest();
	
	console.log("Doing NEW getUserStats HTTP request...");
	
  // build the GET request
  req.open('GET', baseURL() + "api/userstat/" + uuid() + "/" + Pebble.getAccountToken() + "/" + encodeURI(localStorage.getItem("player_name")), true);
  req.onload = function(e) {
    if (req.readyState == 4) {
      // 200 - HTTP OK
      if(req.status == 200) {
		response_parts = req.responseText.split("<",1);
        response = JSON.parse(response_parts[0]);
		console.log("Web reply:" + response_parts[0]);
		  
		  if (response.u_id)
			  localStorage.setItem("player_id", response.u_id);
		  if (response.u_name)
		  {
			  response.u_name = decodeURI(response.u_name);
			  if (response.u_name == "null") response.u_name = '';
			  localStorage.setItem("player_name", response.u_name);
	  	  }
		  sendToPebble(response);

        }
      } else {
        console.log("Web request returned error code " + req.status.toString());
      }
    }
  req.send(null);
};


function sendToPebble( response )
{
		// Send entire response to pebble
		var transactionId = Pebble.sendAppMessage( response 
			
		
 	    ,function(e) {
			console.log("Successfully delivered message with transactionId="
			  + e.data.transactionId);
		  } 
			// Pebble has an SDK bug so the following doesn't work:
			/* ,
		  function(e) {
			console.log("Unable to deliver message with transactionId="
			  + e.data.transactionId
			  + " Error is: " + e.error.message);
		  } */
		);
};

// Show config menu
Pebble.addEventListener("showConfiguration", function(e) {
	
	player_name = localStorage.getItem("player_name");
	if (typeof player_name === 'undefined') {
		player_name = "";
	}

	console.log("Showing configuration");
	console.log("  Current player_name=" + player_name);
	Pebble.openURL(baseURL() + "show/config/" + uuid() + "/" + Pebble.getAccountToken() + "/" + encodeURI(player_name)); //encodeURI vs encodeURIComponent
});

// Save player_name on phone. Pebble & web use phone as master source.
Pebble.addEventListener("webviewclosed", function(e) {
	console.log("Configuration closed");
	console.log("  Web response=" + e.response);
	if (typeof e.response != 'undefined' && e.response != "")
	{
		var configuration = JSON.parse(e.response);
		player_name = decodeURI(configuration["u_name"]);
		if (player_name == "null") player_name = '';
		localStorage.setItem("player_name", player_name);		
	}
});