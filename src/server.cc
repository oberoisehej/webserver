/**
 * This file contains the primary logic for your server. It is responsible for
 * handling socket communication - parsing HTTP requests and sending HTTP responses
 * to the client. 
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <functional>
#include <iostream>
#include <sstream>
#include <vector>
#include <tuple>
#include <queue>
#include <fstream>
#include <thread>
#include <ctime>
#include <chrono>

#include "server.hh"
#include "http_messages.hh"
#include "errors.hh"
#include "misc.hh"
#include "routes.hh"

Server::Server(SocketAcceptor const& acceptor) : _acceptor(acceptor) { }
clock_t startTime;
float upTime;
int numRequests;
float maxTime;
float minTime;
std::string * curUri;
std::string * curUrl;
std::string * maxUrl;
std::string * minUrl;
std::string * ip;
std::string * curCode;
char isLinear;

class Timer {
 public:
    void start() {
      m_StartTime = std::chrono::system_clock::now();
      m_bRunning = true;
    }
    void stop() {
      m_EndTime = std::chrono::system_clock::now();
      m_bRunning = false;
    }
    double elapsedMilliseconds() {
      std::chrono::time_point<std::chrono::system_clock> endTime;
      if (m_bRunning) {
        endTime = std::chrono::system_clock::now();
      } else {
        endTime = m_EndTime;
      }
      return std::chrono::duration_cast<std::chrono::milliseconds>(endTime - m_StartTime).count();
    }
    double elapsedSeconds() {
      return elapsedMilliseconds() / 1000.0;
    }

 private:
    std::chrono::time_point<std::chrono::system_clock> m_StartTime;
    std::chrono::time_point<std::chrono::system_clock> m_EndTime;
    bool m_bRunning = false;
};


// STL THREAD VERSION
struct ThreadParams {
  const Server * server;
  Socket_t sock;
};

void dispatchThread(ThreadParams * params) {
  // Thread dispatching this request
  params->server->handle(params->sock);
  // Delete params struct
  delete params;
}

void *loopThread(ThreadParams * params) {
  while (1) {
    Socket_t sock = params->server->_acceptor.accept_connection();
    params->server->handle(sock);
  }
  delete params;
}

void Server::run_linear() const {
  Timer timer;
  timer.start();
  upTime = 0;
  numRequests = 0;
  maxTime = 0;
  minTime = 99999;
  isLinear = 1;
  curCode = new std::string();

  while (1) {
    Socket_t sock = _acceptor.accept_connection();
    numRequests++;
    float time = timer.elapsedSeconds();
    handle(sock);
    time = timer.elapsedSeconds() - time;
    if (time < minTime && time > 0) {
      minTime = time;
      minUrl = new std::string(*curUrl);
    }
    if (time > maxTime) {
      maxTime = time;
      maxUrl = new std::string(*curUrl);
    }
    upTime = timer.elapsedSeconds();
    std::ofstream stats;
    stats.open("http-root-dir/htdocs/stats");
    stats << "Total Server Uptime: " << upTime << "s\n";
    stats << "Number of Requests: " << numRequests << "\n";
    stats << "Longest Request: " << maxTime << "s " << *maxUrl << "\n";
    stats << "Shortest Request: " << minTime << "s " << *minUrl << "\n";

    std::ofstream log;
    log.open("myhttpd.log", std::ios::app);
    log << curUri << " " << *curCode << "\n";
  }
}

void Server::run_fork() const {
  // TODO: Task 1.4
  while (1) {
    Socket_t sock = _acceptor.accept_connection();
    int ret = fork();
    if (ret == 0) {
      handle(sock);
      exit(0);
    } else {
      int id = 0;
      while (id >= 0) {
        // while there are terminated tasks, keep checking for more
        id = waitpid(-1, NULL, WNOHANG);
      }
    }
  }
}

void Server::run_thread() const {
  // TODO: Task 1.4
  while (1) {
    Socket_t sock = _acceptor.accept_connection();
    // Put socket in new ThreadParams struct
    ThreadParams * threadParams = new ThreadParams;
    threadParams->server = this;
    threadParams->sock = std::move(sock);
    // Create thread
    std::thread t(dispatchThread, threadParams);
    t.detach();
  }
}

void Server::run_thread_pool(const int num_threads) const {
  // TODO: Task 1.4
  // Socket_t sock = _acceptor.accept_connection();

  ThreadParams * threadParams = new ThreadParams;
  threadParams->server = this;

for (int i = 0; i < num_threads; i++) {
    std::thread t(loopThread, threadParams);
    t.detach();
  }
  loopThread(threadParams);
}

// example route map. you could loop through these routes and find the first route which
// matches the prefix and call the corresponding handler. You are free to implement
// the different routes however you please
/*
std::vector<Route_t> route_map = {
  std::make_pair("/cgi-bin", handle_cgi_bin),
  std::make_pair("/", handle_htdocs),
  std::make_pair("", handle_default)
};
*/


void Server::handle(const Socket_t& sock) const {
  HttpRequest request;
  int count = 0;
  while (true) {
    std::string str = std::string(sock->readline());
    if (str.length() < 2) {
      return;
    }
    if (str.compare("\r\n") == 0) {
      break;
    }
    if (count == 0) {
      request.method = str.substr(0, str.find(" "));
      str = str.substr(str.find(" ") + 1);
      request.request_uri = str.substr(0, str.find(" "));
      if (str.find("cgi-bin") < str.length() && str.find("?") < str.length()) {
        str = str.substr(str.find("?") + 1);
        request.query = str.substr(0, str.find(" "));
      }
      str = str.substr(str.find(" ") + 1);
      request.http_version = str.substr(0, str.find("\r\n"));
    } else {
      std::string header = str.substr(0, str.find(":"));
      str = str.substr(str.find(" ") + 1);
      if (str.length() >= 2) {
        str.erase(str.size() -2);
      }
      request.headers[header] = str;
    }
    count++;
  }
  request.print();
  if (isLinear) {
    if (request.headers.count("Host") > 0) {
      curUrl = new std::string(request.headers.find("Host")->second);
      *curUrl = *curUrl + request.request_uri;
    } else {
      curUrl = new std::string(request.request_uri);
    }
    curUri = new std::string(request.request_uri);

    if (use_https) {
      *curUrl = "https://" + *curUrl;
    } else {
      *curUrl = "http://" + *curUrl;
    }
  }

  HttpResponse resp;
  if (request.request_uri.find("cgi-bin") < request.request_uri.length()) {
    resp = handle_cgi_bin(request, sock);
  } else {
    resp = handle_htdocs(request);
  }
  if (isLinear) {
    *curCode = std::to_string(resp.status_code);
  }
  sock->write(resp.to_string());
}


