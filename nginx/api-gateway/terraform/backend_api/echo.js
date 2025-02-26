function echo(r) {
    var headers = {};
    for (var h in r.headersIn) {
        headers[h] = r.headersIn[h];
    }

    var req = {
        "client": r.variables.remote_addr,
        "port": Number(r.variables.server_port),
        "host": r.variables.host,
        "method": r.variables.request_method,
        "uri": r.uri,
        "httpVersion": r.httpVersion,
        "headers": headers,
        "body": r.variables.request_body
    }
    var res = {
        "status": Number(r.variables.return_code),
        "timestamp": r.variables.time_iso8601
    }

    r.return(Number(r.variables.return_code), JSON.stringify({ "request": req, "response": res }) + '\n');
}

function baseballLeagues(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var leagues = [
      {
          "id": 0,
          "leagueName": "Major League Baseball"
      },
      {
          "id": 1,
          "leagueName": "Nippon Professional Baseball"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "leagues": leagues }) + '\n');
}

function baseballPlayers(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var players = [
      {
          "id": 0,
          "firstName": "Babe",
          "lastName": "Ruth"
      },
      {
          "id": 1,
          "firstName": "Roger",
          "lastName": "Clemens"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "players": players }) + '\n');
}

function baseballStadiums(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var stadiums = [
      {
          "id": 0,
          "stadiumName": "AT&T Park"
      },
      {
          "id": 1,
          "stadiumName": "Wrigley Field"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "stadiums": stadiums }) + '\n');
}

function golfCourses(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var courses = [
      {
          "id": 0,
          "courseName": "Royal County Down"
      },
      {
          "id": 1,
          "courseName": "Augusta National G.C."
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "courses": courses }) + '\n');
}

function golfPlayers(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var players = [
      {
          "id": 0,
          "firstName": "Rory",
          "lastName": "McIlroy"
      },
      {
          "id": 1,
          "firstName": "Jon",
          "lastName": "Rahm"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "players": players }) + '\n');
}

function golfTournaments(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var tournaments = [
      {
          "id": 0,
          "tournamentName": "Masters Tournament"
      },
      {
          "id": 1,
          "tournamentName": "PGA Championship"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "tournaments": tournaments }) + '\n');
}

function tennisCourts(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var courts = [
      {
          "id": 0,
          "courtName": "Centre Court"
      },
      {
          "id": 1,
          "courtName": "Court Philippe Chatrier"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "courts": courts }) + '\n');
}

function tennisPlayers(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var players = [
      {
          "id": 0,
          "firstName": "Rafael",
          "lastName": "Nadal"
      },
      {
          "id": 1,
          "firstName": "Roger",
          "lastName": "Federer"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "players": players }) + '\n');
}

function tennisTournaments(r) {
  var headers = {};
  for (var h in r.headersIn) {
      headers[h] = r.headersIn[h];
  }
  var tournaments = [
      {
          "id": 0,
          "tournamentName": "Wimbledon"
      },
      {
          "id": 1,
          "tournamentName": "Roland Garros"
      }
  ]

  r.return(Number(r.variables.return_code), JSON.stringify({ "tournaments": tournaments }) + '\n');
}

export default { echo, baseballLeagues, baseballPlayers, baseballStadiums, golfCourses, golfPlayers, golfTournaments, tennisCourts, tennisPlayers, tennisTournaments };
