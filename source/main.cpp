#include <iostream>
#include <getopt.h>
#include <stdio.h>
#include <cstdarg>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <mutex>
#include <vector>
#include <thread>
#include "oscpkt/oscpkt.hh"

#define VERSION_STRING "0.0.1"


struct options {
  std::string socket_path;
  std::vector<std::string> eval_commands;
  bool interactive;
};

std::vector<int> pending_requests;
std::mutex pending_lock;

int parse_opts(options& options, int argc, char* argv[]);
int interactive_loop(int sock, int start_id, std::istream& in);

int add_pending(int id) {
  std::lock_guard<std::mutex> guard(pending_lock);
  pending_requests.push_back(id);
  return 0;
}
int remove_pending(int id) {
  std::lock_guard<std::mutex> guard(pending_lock);
  for (auto it = pending_requests.begin(); it != pending_requests.end(); it++) {
    if (*it == id) {
      pending_requests.erase(it);
      return 0;
    }
  }
  return -1;
}

int handle_message(oscpkt::Message* msg, oscpkt::Message::ArgReader* arg, int id);

int osc_msg(int fd, const std::string& path, int id, const std::string& argtypes, ...) {
  oscpkt::PacketWriter pw;
  oscpkt::Message msg;
  msg.init(path);
  msg.pushInt32(id);
  if (!argtypes.empty()) {
    va_list arglist;
    va_start(arglist, argtypes);
    int n = argtypes.size();
    for (int i = 0; i < n; i++) {
      if (argtypes[i] == 'i') {
        int arg = va_arg(arglist, int);
        msg.pushInt32(arg);
      }
      if (argtypes[i] == 'b') {
        int arg = va_arg(arglist, int);
        msg.pushBool(arg != 0);
      }
      if (argtypes[i] == 's') {
        const char* arg = va_arg(arglist, const char*);
        if (!arg) {
          msg.pushStr("");
        } else {
          std::string str(arg);
          msg.pushStr(str);
        }
      }
    }
  }
  add_pending(id);
  pw.init().addMessage(msg);
  uint32_t size = pw.packetSize();
  write(fd, &size, sizeof(size));
  write(fd, pw.packetData(), size);
}

int read_osc(int fd, oscpkt::PacketReader* pr) {
  uint32_t size;
  char buff[1024*128];
  int bytes = read(fd, &size, sizeof(size));
  if (bytes == -EAGAIN) return 0;
  if (bytes < sizeof(size) || size >=1024*128)
    return -1;
  bytes = read(fd, &buff, size);
  pr->init(&buff, bytes);
  return bytes;
}

int socket_read_loop(int fd) {
  oscpkt::PacketReader pr;
  while (true) {
    int ret = read_osc(fd, &pr);
    if (ret < 0 || !pr.isOk()) {
      return -1;
    }
    oscpkt::Message* msg;
    for (msg = pr.popMessage(); msg; msg = pr.popMessage()) {
      //std::cout << *msg << std::endl;
      oscpkt::Message::ArgReader arg = msg->arg();
      int resp_id = -1;
      if (arg.isInt32()) {
        arg.popInt32(resp_id);
        if (msg->match("/done"))
          remove_pending(resp_id);
        handle_message(msg, &arg, resp_id);
      }
    }
  }
}

int main(int argc, char* argv[]) {
  
  int retcode = 0;
  options options;
  options.interactive = false;

  int ret = parse_opts(options, argc, argv);

  if (ret > 0) return 0; //This was something like "--help" where we don't do anything.
  if (ret < 0) return ret;

  const char* run_dir = getenv("XDG_RUNTIME_DIR");
  if (options.socket_path.empty() && run_dir) {
    options.socket_path = std::string(run_dir) + "/mg.sock";
  }

  if (options.socket_path.empty()) {
    std::cout << "Could not determine socket path." << std::endl;
    exit(-1);
  }

  sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    perror("socket");
    exit(-2);
  }
  std::cout << "connecting to " << options.socket_path << "  ... " << std::endl;
  strcpy(addr.sun_path, options.socket_path.c_str());//, options.socket_path.size()+1);
  addr.sun_family = AF_UNIX;
  auto len = strlen(addr.sun_path) + sizeof(addr.sun_family);
  ret = connect(sock, (struct sockaddr *)&addr, len);
  if (ret < 0) {
    perror("connect");
    exit(-3);
  }

  std::thread* reader = new std::thread(socket_read_loop, sock);
  reader->detach();
  delete reader;

  int request_id = 1;
  osc_msg(sock, "/listen", request_id++, "sb", "plug", true);
  osc_msg(sock, "/listen", request_id++, "sb", "slot", true);
  for (auto command : options.eval_commands) {
    std::cout << "Running: " << command << std::endl;
    osc_msg(sock, "/eval", request_id++, "s", command.c_str());
  }

  if (options.interactive)
    interactive_loop(sock, request_id, std::cin);

  while (true) {
    std::lock_guard<std::mutex> guard(pending_lock);
    if (pending_requests.size() == 0)
      break;
  }
  close(sock);
  exit(retcode);
}

int handle_message(oscpkt::Message* msg, oscpkt::Message::ArgReader* arg, int id) {
  if (msg->match("/text")) {
    std::string text;
    arg->popStr(text);
    std::cout << text << std::endl;
  } else if (msg->match("/error")) {
    std::string text, path;
    int line;
    arg->popStr(text);
    arg->popStr(path);
    arg->popInt32(line);
    std::cerr << text;
    if (!path.empty()) {
      std::cerr << "(" << path;
      if (line >= 0) {
        std::cerr << ":" << line;
      }
      std::cerr << ")";
    }
    std::cerr << std::endl;
  } else {
    //a generic catch-all to print something to the user.
    std::cout << msg->addressPattern() << ": ";
    while (arg->nbArgRemaining() > 0) {
      if (arg->isInt32()) {
        int i;
        arg->popInt32(i);
        std::cout << i << " ";
      } else if (arg->isStr()) {
        std::string s;
        arg->popStr(s);
        std::cout << "\"" << s << "\" ";
      }
    }
    std::cout << std::endl;
  }
}

int print_version() {
  std::cout <<  "moltengamepadctl version " << VERSION_STRING << "\n";
  return 0;
}

int print_usage(char* execname) {
  print_version();
  std::cout << "USAGE:\n";
  std::cout << "\t" << execname << " [OPTIONS]\n";
  std::cout << "\n";
  std::string help_text = ""\
                          "--help -h\n"\
                          "\tShow this message\n"\
                          "\n"\
                          "--version -v\n"\
                          "\tDisplay the version string\n"\
                          "\n"\
                          "--socket-path -S\n"\
                          "\tPath to the UNIX socket of MoltenGamepad\n"\
                          "\n"\
                          "--exec -e <command>\n"\
                          "\tSend command to MoltenGamepad\n"\
                          "\n"\
                          "--interactive -i <command>\n"\
                          "\tRead and send commands from standard input.\n"\
                          "\n"\
                          ;

  std::cout << help_text;
  return 0;
}



int parse_opts(options& options, int argc, char* argv[]) {

  int c = 0;

  static struct option long_options[] = {
    {"help",          0,    0,  'h'},
    {"version",       0,    0,  'v'},
    {"socket-path",   1,    0,  'S'},
    {"exec",          1,    0,  'e'},
    {"interactive",   0,    0,  'i'},
    {0,               0,    0,    0},
  };
  int long_index;
  while (c != -1) {
    c = getopt_long(argc, argv, "hve:S:i", long_options, &long_index);
    switch (c) {
    case 0:
      break;
    case 'h':
      print_usage(argv[0]);
      return 10;
      break;
    case 'v':
      print_version();
      return 10;
      break;
    case 'S':
      options.socket_path = std::string(optarg);
      break;
    case 'e':
      options.eval_commands.push_back(std::string(optarg));
      break;
    case 'i':
      options.interactive = true;
      break;
    }

  }

  return 0;
}


int interactive_loop(int sock, int start_id, std::istream& in) {
  bool keep_looping = true;
  char* buff = new char [1024];

  while (keep_looping) {
    in.getline(buff, 1024);

    char* trimmed = buff;
    while (trimmed < (buff+1024) && *trimmed && *trimmed == ' ')
      trimmed++;
    if (strncmp(trimmed, "quit", 4) == 0)
      break;

    osc_msg(sock, "/eval", start_id++, "s", trimmed);


    if (in.eof()) break;


  }

  delete[] buff;
}
