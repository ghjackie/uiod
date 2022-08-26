#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#endif

#include <chrono>
#include <iostream>

#define IO_PORT      6666
#define IO_BUF_SIZE  4096

#define DMA_PORT     6667
#define DMA_MAX_SIZE (64 * 1024)
#define DMA_BUF_SIZE (10 * 1024 * 1024)

#define WR_FLAG (PROT_READ | PROT_WRITE)
#define RD_FLAG (PROT_READ)

using namespace std;
using namespace std::chrono;

int main(int argc, char**argv) {

  time_point<high_resolution_clock> timer;
  int32_t count;

  int n, i;
  uint8_t payload_io[IO_BUF_SIZE];

  struct sockaddr_in addr_in_io, addr_in_dma;
  int sock_io, sock_dma;

  const char *address = "192.168.26.1";

  if (argc >= 2) {
    address = argv[1];
  }

  memset(payload_io, 0, IO_BUF_SIZE);

#if defined(WIN32) || defined(WIN64)
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
    WSACleanup();
    return -1;
  }
#endif

  sock_io = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_io == -1) {
    printf("create socket failed with error: %s (errno :%d)\n", strerror(errno), errno);
    return -1;
  }

  memset(payload_io, 0, sizeof(payload_io));
  memset(&addr_in_io, 0, sizeof(addr_in_io));
  inet_pton(AF_INET, address, &addr_in_io.sin_addr.s_addr);
  addr_in_io.sin_family = AF_INET;
  addr_in_io.sin_port = htons(IO_PORT);

  int result = connect(sock_io, (struct sockaddr*)&addr_in_io, sizeof(addr_in_io));
  if (result == -1) {
    printf("connect socket failed with error: %s (errno :%d)\n", strerror(errno), errno);
    return -1;
  }

  sock_dma = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_dma == -1) {
    printf("create stream socket failed with error: %s (errno :%d)\n", strerror(errno), errno);
    return -1;
  }

  memset(&addr_in_dma, 0, sizeof(addr_in_dma));
  inet_pton(AF_INET, address, &addr_in_dma.sin_addr.s_addr);
  addr_in_dma.sin_family = AF_INET;
  addr_in_dma.sin_port = htons(DMA_PORT);

  result = ::connect(sock_dma, (struct sockaddr*)&addr_in_dma, sizeof(addr_in_dma));
  if (result == -1) {
    printf("connect stream socket failed with error: %s (errno :%d)", strerror(errno), errno);
    return -1;
  }

  printf("Starting performance profiling...\r\n");
  timer = high_resolution_clock::now();

  for (i = 0; i < 10000; i++) {
    payload_io[0] = 1;
    *(uint32_t*)(&payload_io[4]) = 0x43C01000;

    if (send(sock_io, (char*)payload_io, 12, 0) < 0) {
      printf("send msg error: %s(errno :%d)\n", strerror(errno), errno);
      printf("Iteration count: %d\n", i);
      return 0;
    }

    if ((n = recv(sock_io, (char*)payload_io, IO_BUF_SIZE, 0)) != 12) {
      printf("recv msg error: %s(errno :%d)\n", strerror(errno), errno);
      printf("Iteration count: %d\n", i);
      return -1;
    }

    payload_io[n] = '\0';
  }

  count = (int32_t)duration_cast<microseconds>(high_resolution_clock::now() - timer).count();

#ifdef _WIN32
  closesocket(sock_io);
  closesocket(sock_dma);
#else
  close(sock_io);
  close(sock_dma);
#endif

  printf("Done performance profiling...\r\n");

  printf("Iteration count: %d\n", i);
  printf("Duration : %ds %dms %dus\n",
    (count / 1000000),
    ((count / 1000) % 1000),
    (count % 1000));

  printf("0x%08x 0x%08x\n", *(uint32_t*)(&payload_io[4]), *(uint32_t*)(&payload_io[8]));

  printf("\npress any key to exit...\r\n");
  getchar();

  return 0;
}

