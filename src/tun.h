#ifndef TUN_H_
#define TUN_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> 
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <map>
#include <string>
using namespace std;
/* buffer for reading from tun/tap interface, must be >= 1500 */
#define PKT_SIZE 2000   
#define PORT_ETH 55554
#define PORT_ATH 55555
#define MAX_RADIO 3
class Tun {
 public:
  enum IOType {
    kTun=1,
    kWspace, 
    kCellular, 
    kControl,
  };

  Tun(): tun_type_(IFF_TUN), port_eth_(PORT_ETH), port_ath_(PORT_ATH) {
    if_name_[0] = '\0';
    server_ip_eth_[0] = '\0';
    server_ip_ath_[0] = '\0';
    broadcast_ip_ath_[0] = '\0';
    controller_ip_eth_[0] = '\0';

  }

  ~Tun() {
    close(tun_fd_);
    close(sock_fd_eth_);
    close(sock_fd_ath_);
  }
  
  void Init();
  void InitSock();
  void ObtainClientAddr();
  //int Accept(int listen_fd, sockaddr_in *client_addr);
  int AllocTun(char *dev, int flags);
  int CreateSock();
  void BindSocket(int fd, sockaddr_in *addr);
  void CreateAddr(const char *ip, int port, sockaddr_in *addr);
  uint16_t Read(const IOType &type, char *buf, uint16_t len);
  uint16_t Write(const IOType &type, char *buf, uint16_t len, int client_id = 0);

// Data members:
  int tun_fd_;
  int tun_type_;        // TUN or TAP
  char if_name_[IFNAMSIZ];
  char server_ip_eth_[16];
  char server_ip_ath_[16];
  char broadcast_ip_ath_[16];
  struct sockaddr_in server_addr_eth_, server_addr_ath_, client_addr_ath_; 
  uint16_t port_eth_, port_ath_;
  int sock_fd_eth_, sock_fd_ath_;       // Sockets to handle request at the server side

  map<int, string> client_ip_tbl_; // <client_id, client_ip_eth_>.
  map<int, struct sockaddr_in> client_addr_eth_tbl_;
  char controller_ip_eth_[16];
  struct sockaddr_in controller_addr_eth_;
};

int cread(int fd, char *buf, int n);
int cwrite(int fd, char *buf, int n);
int read_n(int fd, char *buf, int n);

#endif
