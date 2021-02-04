#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <experimental/filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <vector>
#include <tuple>
#include <queue>
#include <fstream>
#include <string>


#include "http_messages.hh"
#include "misc.hh"

// You may find implementing this function and using it in server.cc helpful
char user[] = "oberoi";
char encrypted[] = "Basic b2Jlcm9pOmVuY3J5cHRlZA==";  // oberoi:encrypted
namespace fs = std::experimental::filesystem;
int isBrowse;

std::string traverse(std::string path, std::string uri) {
  std::string browsable = "<html>\n<head>\n<title>CS252: " + uri + "</title>\n</head>\n";
  browsable += "<body>\n<h1>cs252: " + uri + "</h1>\n <ul>\n";

  DIR * d = opendir(path.c_str());
  if (NULL == d) {
    perror("opendir: ");
    exit(1);
  }
  for (dirent * ent = readdir(d); NULL != ent; ent = readdir(d)) {
    browsable = browsable + "<li><A HREF=\"" + uri + ent->d_name + "\"> " + ent->d_name + "</A>\n";
  }
  browsable += "</ul>\n</body>\n<html>";
  closedir(d);
  return browsable;
}

int getStatusCode(std::string uri, int * file, std::string ** name) {
  std::string path = std::string("http-root-dir/htdocs" + uri);
  struct stat s;
  isBrowse = 0;
  if (stat(path.c_str(), &s) == 0) {
    if (s.st_mode & S_IFDIR) {
      if (uri.compare("/") == 0) {
        path += "index.html";
        (*name) = new std::string("/index.html");
        *file = open(path.c_str(), O_RDONLY, 0664);
        if (*file < 0) {
          perror("open file");
          return 404;
        }
      } else if (uri[uri.length() - 1] == '/') {
        (*name) = new std::string(traverse(path, uri));
        isBrowse = 1;
        return 200;
      } else {
        path = path + "/index.html";
        (*name) = new std::string(uri + "/index.html");
        if (!stat(path.c_str(), &s)) {
          return 404;
        }
        *file = open(path.c_str(), O_RDONLY, 0664);
        if (*file < 0) {
          perror("open file");
          return 404;
        }
      }
    } else {
      (*name) = new std::string(uri);
      *file = open(path.c_str(), O_RDONLY, 0644);
      if (*file < 0) {
        perror("open file");
        return 404;
      }
    }
    close(*file);
    return 200;
  }
  return 404;
}

HttpResponse handle_htdocs(const HttpRequest& request) {
  HttpResponse resp;
  resp.http_version = request.http_version;
  // Get the request URI, verify the file exists and serve it
  if (request.headers.count("Authorization") > 0 &&
    request.headers.find("Authorization")->second.length() == 30 &&
    request.headers.find("Authorization")->second.compare(encrypted) == 0) {
    // check authorization
    int file;
    std::string * name;
    resp.status_code = getStatusCode(request.request_uri, &file, &name);  // get status code
    if (resp.status_code == 200) {
      if (isBrowse) {
        resp.headers["Content-Type"] = "text/html;charset=us-ascii";
        resp.message_body = *name;
      } else {
        // input the correct message body
        std::string * fullPath = new std::string("http-root-dir/htdocs" + (*name));
        std::string contentType = get_content_type(*fullPath);
        resp.headers["Content-Type"] = contentType;
        if (contentType.compare(0, 9, "text/html") == 0) {  // if html or text file
          FILE * input = fopen(fullPath->c_str(), "r");
          resp.message_body = "";
          char c;
          while ((c = getc(input)) != EOF) {
            resp.message_body += c;
          }
          fclose(input);
        } else {  // for all other formats open as binary file
          resp.message_body = "";
          std::ifstream input(fullPath->c_str(), std::ios::binary);
          std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(input), {});
          resp.message_body = std::string(buffer.begin(), buffer.end());
        }
        delete(fullPath);
      }
      delete(name);
    } else {
      resp.message_body = "404 Error! File not found";
    }
    resp.headers["Connection"] = "close";
    resp.headers["Content-Length"] = std::to_string(resp.message_body.length());
  } else {
    // header to request autohoization
    resp.status_code = 401;
    resp.headers["Authorization"] =  "Basic realm=\"" + std::string(user) + "\"";
  }
  std::cout << resp.to_string() << std::endl;
  return resp;
}
