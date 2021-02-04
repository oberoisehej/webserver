#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <link.h>
#include "http_messages.hh"
#include "socket.hh"

// You could implement your logic for handling /cgi-bin requests here
typedef void (*httprunfunc)(int ssock, const char * querystring);
char user2[] = "oberoi";
char encrypted2[] = "Basic b2Jlcm9pOmVuY3J5cHRlZA==";  // oberoi:encrypted

int getStatusCode2(std::string uri, int * file, std::string ** name) {
  std::string path = std::string("http-root-dir/" + uri);

  struct stat s;
  if (stat(path.c_str(), &s) == 0) {
      (*name) = new std::string(uri);
      *file = open(path.c_str(), O_RDONLY, 0644);
      if (*file < 0) {
        perror("open file");
        return 404;
      }
    close(*file);
    return 200;
  }
  return 404;
}



HttpResponse handle_cgi_bin(const HttpRequest& request, const Socket_t& sock) {
  HttpResponse response;
  response.http_version = request.http_version;
  if (request.headers.count("Authorization") > 0 &&
    request.headers.find("Authorization")->second.length() == 30 &&
    request.headers.find("Authorization")->second.compare(std::string(encrypted2)) == 0) {
    // check authorization
    int file;

    std::string * name = new std::string();
    int qIndex = request.request_uri.find("?");
    response.status_code = getStatusCode2(request.request_uri.substr(0, qIndex), &file, &name);
    name = new std::string("http-root-dir" + (*name));
    if (response.status_code == 200) {
      int defaultOut = dup(1);
      int fdpipe[2];
      if (pipe(fdpipe) == -1) {
        perror("pipe");
        return response;
      }
      dup2(fdpipe[1], 1);
      close(fdpipe[1]);
      int ret;
      if (name->find(".so") < name->length()) {
        // Loadable Module
        ret = fork();
        if (ret == 0) {
          std::string library("./" + *name);
          void * lib = dlopen(library.c_str(), RTLD_LAZY);
          if (lib == NULL) {
            fprintf(stderr, "./%s not found", name->c_str());
            perror("dlopen");
            exit(1);
          }
          httprunfunc cur_httprun;
          cur_httprun = (httprunfunc) dlsym(lib, "httprun");
          if (cur_httprun == NULL) {
            perror("dlysm: httprun not found:");
            exit(1);
          }
          cur_httprun(fdpipe[0], request.query.c_str());
          exit(0);
        } else if (ret < 0) {
          perror("fork");
        }
      } else {
        ret = fork();
        if (ret == 0) {
          std::string req = std::string("REQUEST_METHOD=GET");
          putenv(const_cast<char *>(req.c_str()));
          std::string query = std::string("QUERY_STRING=" + request.query);
          putenv(const_cast<char *>(query.c_str()));
          system(name->c_str());
          exit(0);
        } else if (ret < 0) {
          perror("fork");
        }
      }
      waitpid(ret, NULL, 0);
      dup2(defaultOut, 1);
      close(defaultOut);
      char c = 0;
      close(fdpipe[1]);
      response.message_body = "";
      while (read(fdpipe[0], &c, 1) != 0) {
        response.message_body.push_back(c);
      }
    } else {
      response.message_body = "404 Error! File not found";
    }
    response.headers["Connection"] = "close";
    response.headers["Content-Length"] = std::to_string(response.message_body.length());
  } else {
    response.status_code = 401;
    response.headers["Authorization"] = "Basic realm=\"" + std::string(user2) + "\"";
  }
  std::cout << response.to_string() << "\r\n";
  return response;
}
