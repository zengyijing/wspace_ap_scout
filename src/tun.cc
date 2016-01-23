#include "tun.h"

int Tun::AllocTun(char *dev, int flags) {
  struct ifreq ifr;
  int fd, err;
  char *clonedev = "/dev/net/tun";

  if( (fd = open(clonedev , O_RDWR)) < 0 ) {
    perror("Opening /dev/net/tun");
    return fd;
  }

  memset(&ifr, 0, sizeof(ifr));

  ifr.ifr_flags = flags;

  if (*dev) {
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);
  }

  if((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
    perror("ioctl(TUNSETIFF)");
    close(fd);
    return err;
  }

  strcpy(dev, ifr.ifr_name);
  return fd;
}

void Tun::CreateAddr(const char *ip, int port, sockaddr_in *addr) {
  memset(addr, 0, sizeof(sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = inet_addr(ip);
  addr->sin_port = htons(port);
}

void Tun::CreateConn() {
  InitSock();
  ObtainClientAddr();
}

void Tun::BindSocket(int fd, sockaddr_in *addr) {
  int optval = 1;
  socklen_t addr_len = sizeof(struct sockaddr_in);

  if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0)  
    perror("setsocketopt()");

  if(bind(fd, (struct sockaddr*)addr, addr_len) < 0)  
    perror("ath bind()");
}

void Tun::InitSock() {
  /* initialize tun/tap interface */
  if ((tun_fd_ = AllocTun(if_name_, tun_type_ | IFF_NO_PI)) < 0 ) {
    perror("Error connecting to tun/tap interface!");
  }

  // Create sockets
  sock_fd_eth_ = CreateSock();
  sock_fd_ath_ = CreateSock();  // should be broadcast at the server side

  // Create server side address
  CreateAddr(server_ip_eth_, port_eth_, &server_addr_eth_);   
  CreateAddr(server_ip_ath_, port_ath_, &server_addr_ath_);   


  CreateAddr(controller_ip_eth_, port_eth_, &controller_addr_eth_); 


  BindSocket(sock_fd_eth_, &server_addr_eth_);
  BindSocket(sock_fd_ath_, &server_addr_ath_);

  int is_broadcast=1;
  if (setsockopt(sock_fd_ath_, SOL_SOCKET, SO_BROADCAST, &is_broadcast, sizeof(is_broadcast)) < 0) 
    perror("Error: InitSock set broadcast option fails!");
}

void Tun::ObtainClientAddr() {
  /** Obtain client's ath address. */
  CreateAddr(broadcast_ip_ath_, port_ath_, &client_addr_ath_);
  printf("Client ath address: %s\n", inet_ntoa(client_addr_ath_.sin_addr));

  /** Obtain client's eth address. */
  //Accept(sock_fd_eth_, &client_addr_eth_);

/*
  strncpy(client_ip_eth_, inet_ntoa(client_addr_eth_.sin_addr), 16);
  printf("Client eth address: %s\n", client_ip_eth_);
*/

}
/*
int Tun::Accept(int listen_fd, sockaddr_in *client_addr) {
  socklen_t addr_len = sizeof(struct sockaddr_in);
  bzero((char*)client_addr, (int)addr_len);
  char buffer[PKT_SIZE];
  if (recvfrom(listen_fd, buffer, PKT_SIZE, 0, (struct sockaddr*)client_addr, (socklen_t*)&addr_len) == -1) 
    perror("Server recvfrom fail!");

  if (strncmp(buffer, "connect", strlen("connect"))) 
    perror("Invalid connection message from client!");
  else
    memcpy(&client_id_, buffer + strlen("connect") + 1, sizeof(int));

  snprintf(buffer, PKT_SIZE, "accept\0");
  // Server reply
  if (sendto(listen_fd, buffer, strlen(buffer)+1, 0, (struct sockaddr*)client_addr, (socklen_t)addr_len) == -1) 
    perror("Server recvfrom fail!");
}
*/
int Tun::CreateSock() {
  int sock_fd;
  if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("UDP socket()");
  }
  return sock_fd;
}

uint16_t Tun::Read(const IOType &type, char *buf, uint16_t len) {
  uint16_t nread=-1;
  if (type == kTun)
    nread = cread(tun_fd_, buf, len);
  else if (type == kCellular)
    nread = recvfrom(sock_fd_eth_, buf, len, 0, NULL, NULL);
  else if (type == kWspace)  /** All the uplink traffic should send over the cellular.*/
    assert(0);
  assert(nread > 0);
  return nread;
}

uint16_t Tun::Write(const IOType &type, char *buf, uint16_t len) {
  uint16_t nwrite=-1;
  assert(len > 0);
  socklen_t addr_len = sizeof(struct sockaddr_in);
  if (type == kTun)
    nwrite = cwrite(tun_fd_, buf, len);
  else if (type == kCellular)
    nwrite = sendto(sock_fd_eth_, buf, len, 0, (struct sockaddr*)&client_addr_eth_, addr_len);

  else if (type == kControl)
    nwrite = sendto(sock_fd_eth_, buf, len, 0, (struct sockaddr*)&controller_addr_eth_, addr_len);

  else if (type == kWspace)
    nwrite = sendto(sock_fd_ath_, buf, len, 0, (struct sockaddr*)&client_addr_ath_, addr_len);
  assert(nwrite == len);
  return nwrite;
}

inline int cread(int fd, char *buf, int n) {
  int nread;

  if((nread=read(fd, buf, n)) < 0) {
    perror("Reading data");
  }
  return nread;
}

inline int cwrite(int fd, char *buf, int n) {
  int nwrite;
  if((nwrite=write(fd, buf, n)) < 0) {
    perror("Writing data");
  }
  return nwrite;
}
