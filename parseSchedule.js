(function () {
  // figure out when this schedule is valid as of
  var validAsOf;
  var validIt = document.evaluate("//p[@class='bodyBlueHeadline']", document,
		  null, XPathResult.UNORDERED_NODE_ITERATOR_TYPE, null);
  for (var possibleValid = validIt.iterateNext(); possibleValid; possibleValid = validIt.iterateNext()) {
    var captures;
    if ((captures = /Schedule\s+effective\s+as\s+of\s+(\w+ \d+, \d+)/.exec(possibleValid.textContent)) != null) {
      validAsOf = captures[1];
      break;
    }
  }

  // figure out what directions this route goes
  var direction;
  var availableDirections;
  var directionIt = document.evaluate("//td[@class='scheduleHeaderBlueHilite']", document,
	      null, XPathResult.UNORDERED_NODE_ITERATOR_TYPE, null);

  for (var possibleDirection = directionIt.iterateNext(); possibleDirection; possibleDirection = directionIt.iterateNext()) {
    var captures;
    if ((captures = /(North|South|East|West)\s+Bound/.exec(possibleDirection.textContent)) != null ||
	(captures = /(Loop|Clockwise|Counterclockwise)/.exec(possibleDirection.textContent)) != null) {
      // we've found a direction: record it
      var currentDir;
      switch (captures[1]) {
      case "North": currentDir = "N"; break;
      case "South": currentDir = "S"; break;
      case "East": currentDir = "E"; break;
      case "West": currentDir = "W"; break;
      case "Loop": currentDir = "Loop"; break;
      case "Clockwise": currentDir = "CW"; break;
      case "Counterclockwise": currentDir = "CCW"; break;
      default: availableDirections = ("? (" + captures[1] + ")"); break;
      }

      if (availableDirections != null)
	availableDirections += "-" + currentDir;
      else 
	availableDirections = currentDir;

      if (document.evaluate("count(.//a)", possibleDirection, null, XPathResult.NUMBER_TYPE, null).numberValue == 0) {
	// _not_ a link: the currently listed schedule's direction
	direction = currentDir;
      }
    }
  }

  // fetch the list of stations
  var stations = new Array;
  var stationIt = document.evaluate("//div[@class='scheduleStations']", document, 
	      null, XPathResult.ORDERED_NODE_ITERATOR_TYPE, null);
  var station = stationIt.iterateNext();

  var schedules = new Object;
  while (station) {
    var stationName = station.textContent;
    if (schedules[stationName] != null)
	stationName += " (return)";

    schedules[stationName] = new Array;
    stations.push(stationName);
    station = stationIt.iterateNext();
  }

  // see if we have subroutes (e.g. B/BX/BF)
  var hasSubroutes = (document.evaluate("string-length(string(//tr[@class='headrow']//div[@class='scheduleTimesGrey'][1]/text()))",
	  document, null, XPathResult.NUMBER_TYPE, null).numberValue != 0);

  // and now loop over the rows of the schedule to figure out the actual schedule
  var rowIt = document.evaluate("//tr[@class='row']", document,
	      null, XPathResult.ORDERED_NODE_ITERATOR_TYPE, null);
  var row = rowIt.iterateNext();

  var subroutes = new Object;
  while (row) {
    var colIt = document.evaluate(".//div[@class='scheduleTimesGrey']", row,
	      null, XPathResult.ORDERED_NODE_ITERATOR_TYPE, null);

    var col = colIt.iterateNext();
    if (!col)
      return "no columns!";

    var first = true;
    var i = 0;
    var subRoute;

    // for each row, look at the table cells within that row
    // if we have a subroute, the first cell is the subroute; otherwise
    // the cells are times, each going with its respective station from the header row
column:
    while (col) {
      if (first && hasSubroutes) {
	subRoute = col.textContent;
	subroutes[subRoute] = 1;
	col = colIt.iterateNext();
	first = false;
	continue column;
      }

      var scheduleEntry = new Object;
      scheduleEntry.time = col.textContent;

      if (scheduleEntry.time == "--") {
	// no stop at this station for this bus
	i++;
	col = colIt.iterateNext();
	continue column;
      }

      if (subRoute) {
	scheduleEntry.route = subRoute;
      }

      schedules[stations[i]].push(scheduleEntry);
      i++;
      col = colIt.iterateNext();
    }

    row = rowIt.iterateNext();
  }

  // convert our set of subroutes to a list
  var subrouteList = new Array;
  for (var r in subroutes)
      subrouteList.push(r);

  if (validAsOf == null || schedules.length == 0 || direction == null || availableDirections == null)
    return null;

  var ret = new Object;
  ret.validAsOf = validAsOf;
  ret.schedules = schedules;
  ret.subroutes = subrouteList;
  ret.direction = direction;
  ret.availableDirections = availableDirections;

  return ret;
})()
