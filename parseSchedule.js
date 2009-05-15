(function () {
  // fetch the list of stations and setup our final datastructure
  var schedules = new Object;
  var stations = new Array;
  var stationIt = document.evaluate("//div[@class='scheduleStations']", document, 
	      null, XPathResult.ORDERED_NODE_ITERATOR_TYPE, null);
  var station = stationIt.iterateNext();

  while (station) {
    schedules[station.textContent] = new Array;
    stations.push(station.textContent);
    station = stationIt.iterateNext();
  }

  // see if we have subroutes (e.g. B/BX/BF)
  var hasSubroutes = (document.evaluate("count(//tr[@class='headrow']//div[@class='scheduleTimesGrey'])",
	  document, null, XPathResult.ANY_TYPE, null).numberValue != 0);

  // and now loop over the rows of the schedule to figure out the actual schedule
  var rowIt = document.evaluate("//tr[@class='row']", document,
	      null, XPathResult.ORDERED_NODE_ITERATOR_TYPE, null);
  var row = rowIt.iterateNext();

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
	col = colIt.iterateNext();
	first = false;
	continue column;
      }

      var scheduleEntry = new Object;
      scheduleEntry.time = col.textContent;

      if (subRoute) {
	scheduleEntry.route = subRoute;
      }

      schedules[stations[i]].push(scheduleEntry);
      i++;
      col = colIt.iterateNext();
    }

    row = rowIt.iterateNext();
  }

  return schedules;
})()
