#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#pragma comment (lib, "Ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>

#endif

#define SRA_PORT      6666
#define DMA_PORT      6667

#define SRA_BUF_SIZE  4096
#define DMA_MAX_SIZE  (64 * 1024)
#define DMA_BUF_SIZE  (10 * 1024 * 1024)
#define MEM_BUF_SIZE  (100 * 1024 * 1024)

#define WR_FLAG (PROT_READ | PROT_WRITE)
#define RD_FLAG (PROT_READ)

int listen_sra, sock_sra;
int listen_dma, sock_dma;
struct sockaddr_in servaddr_sra;
struct sockaddr_in servaddr_dma;
uint8_t payload_sra[SRA_BUF_SIZE];
std::vector<uint8_t> payload_dma;
std::vector<uint8_t> payload_mem;

int main() {

#ifndef _WIN32
  int io_fd;
  uint dma_size;
  uint dma_addr;

  void *ptr;
  uint address;
  uint page_addr, page_offset;
  uint page_size = sysconf(_SC_PAGESIZE);

  /* Open the IO proxy device for the transmit and receive channels */
  io_fd = open("/dev/mem", O_RDWR);
  if (io_fd < 1) {
    printf("Unable to open IO proxy device file\n");
    exit(EXIT_FAILURE);
  }
#endif

  int n;

  std::vector<uint32_t*> buffers;

#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
    WSACleanup();
    return 0;
  }
#endif

  if ((listen_sra = (int)socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    printf(" create socket error: %s (errno :%d)\n", strerror(errno), errno);
    return 0;
  }

  memset(&servaddr_sra, 0, sizeof(servaddr_sra));
  servaddr_sra.sin_family = AF_INET;
  servaddr_sra.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr_sra.sin_port = htons(SRA_PORT);

  if ((listen_dma = (int)socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    printf(" create socket error: %s (errno :%d)\n", strerror(errno), errno);
    return 0;
  }

  memset(&servaddr_dma, 0, sizeof(servaddr_dma));
  servaddr_dma.sin_family = AF_INET;
  servaddr_dma.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr_dma.sin_port = htons(DMA_PORT);

  if (bind(listen_sra, (struct sockaddr*) &servaddr_sra, sizeof(servaddr_sra)) == -1) {
    printf(" bind socket error: %s (errno :%d)\n", strerror(errno), errno);
    return 0;
  }

  if (listen(listen_sra, 10) == -1) {
    printf(" listen socket error: %s (errno :%d)\n", strerror(errno), errno);
    return 0;
  }

  if (bind(listen_dma, (struct sockaddr*) &servaddr_dma, sizeof(servaddr_dma)) == -1) {
    printf(" bind socket error: %s (errno :%d)\n", strerror(errno), errno);
    return 0;
  }

  if (listen(listen_dma, 10) == -1) {
    printf(" listen socket error: %s (errno :%d)\n", strerror(errno), errno);
    return 0;
  }

  printf("waiting for client's connection\r\n");

  while (1) {
    if ((sock_sra = (int)accept(listen_sra, (struct sockaddr *) NULL, NULL)) == -1) {
      printf(" accpt socket error: %s (errno :%d)\n", strerror(errno), errno);
      return 0;
    }

    printf("SRA client connected\r\n");

    if ((sock_dma = (int)accept(listen_dma, (struct sockaddr *) NULL, NULL)) == -1) {
      printf(" accpt socket error: %s (errno :%d)\n", strerror(errno), errno);
      return 0;
    }

    printf("DMA client connected\r\n");

    while (1) {
      n = recv(sock_sra, (char*)payload_sra, SRA_BUF_SIZE, 0);

      if (n == 0)
        break;

      if (n != 12)
        continue;

#ifndef _WIN32

      if (payload_sra[1] == 0) { // SRA
        address = *(uint *)(payload_sra + 4);
        page_addr = (address & ~(page_size - 1));
        page_offset = address - page_addr;

        if (payload_sra[0] == 0) { // IO write
          ptr = mmap(NULL, page_size, WR_FLAG, MAP_SHARED, io_fd, page_addr);

          *((uint *)((uint8_t*)ptr + page_offset)) = *(uint *)(payload_sra + 8);

          munmap(ptr, page_size);
        }
        else if (payload_sra[0] == 1) { // IO read
          ptr = mmap(NULL, page_size, RD_FLAG, MAP_SHARED, io_fd, page_addr);

          *(uint *)(payload_sra + 8) = *((uint *)((uint8_t*)ptr + page_offset));

          munmap(ptr, page_size);
        }
      }
      else if (payload_sra[1] == 1) { // DMA
        dma_addr = *(uint*)(payload_sra + 4);
        dma_size = *(uint*)(payload_sra + 8);

        int PkgLength = dma_size * sizeof(uint);

        if (payload_sra[0] == 0) { // DMA write

          /* Map the transmit and receive channels memory into user space so it's accessible */
          uint32_t * mm_base = (uint32_t*)mmap(NULL, PkgLength, PROT_READ | PROT_WRITE, MAP_SHARED, io_fd, dma_addr);
          if (mm_base == (uint32_t *)-1) {
            perror("Virtual address mapping for absolute memory access failed.\n");
            exit(EXIT_FAILURE);
          }

          memcpy(mm_base, &payload_dma[0], PkgLength);

          if (munmap(mm_base, PkgLength) == -1) {
            printf("Can't unmap memory from user space.\n");
            exit(EXIT_FAILURE);
          }
        }
        else if (payload_sra[0] == 1) { // DMA read
          payload_dma.resize(PkgLength);
          /* Map the transmit and receive channels memory into user space so it's accessible */
          uint32_t * mm_base = (uint32_t*)mmap(NULL, PkgLength, PROT_READ | PROT_WRITE, MAP_SHARED, io_fd, dma_addr);
          if (mm_base == (uint32_t *)-1) {
            perror("Virtual address mapping for absolute memory access failed.\n");
            exit(EXIT_FAILURE);
          }

          memcpy(&payload_dma[0], mm_base, PkgLength);

          if (munmap(mm_base, PkgLength) == -1) {
            printf("Can't unmap memory from user space.\n");
            exit(EXIT_FAILURE);
          }
        }
        else if (payload_sra[0] == 2) { // DMA prepare
          payload_dma.resize(PkgLength);
          n = recv(sock_dma, (void*)&payload_dma[0], PkgLength, 0);
          if (n == 0)
            break;
        }
      }
      else if (payload_sra[1] == 2) { // Memory

        dma_addr = *(uint*)(payload_sra + 4);
        dma_size = *(uint*)(payload_sra + 8);

        int PkgLength = dma_size * sizeof(uint);
        char* buffer_ptr;

        if (payload_sra[0] == 0) { // Allocate
          auto data = new uint32_t[dma_size];
          printf("allocate memory 0x%08x\r\n", (uint32_t)data);
          memset(data, dma_addr, sizeof(uint32_t) * dma_size);
          buffers.push_back(data);

          ((uint32_t*)payload_sra)[1] = (uint32_t)data;
        }
        else if (payload_sra[0] == 1) { // Deallocate
          for (auto iter = buffers.begin(); iter != buffers.end(); ) {
            if (dma_addr == (uint32_t)*iter) {
              printf("deallocate memory 0x%08x\r\n", (uint32_t)*iter);
              delete *iter;
              iter = buffers.erase(iter);
            }
            else
              iter++;
          }
        }
        else if (payload_sra[0] == 2) { // Read
          printf("read memory 0x%08x, size 0x%08x\r\n", dma_addr, PkgLength);
          for (auto iter = buffers.begin(); iter != buffers.end(); iter++) {
            if (dma_addr == (uint32_t)*iter) {
              payload_mem.resize(PkgLength);
              memcpy((void*)&payload_mem[0], (void*)dma_addr, PkgLength);
            }
          }
        }
        else if (payload_sra[0] == 3) { // Write
          printf("write memory 0x%08x, size 0x%08x\r\n", dma_addr, PkgLength);
          for (auto iter = buffers.begin(); iter != buffers.end(); iter++) {
            if (dma_addr == (uint32_t)*iter) {
              payload_mem.resize(PkgLength);
              buffer_ptr = (char*)&payload_mem[0];
              for (;;) {
                if (PkgLength <= 0)
                  break;

                n = recv(sock_dma, buffer_ptr, PkgLength, 0);
                if (n == 0)
                  break;

                buffer_ptr += n;
                PkgLength -= n;
              }

              memcpy(*iter, &payload_mem[0], dma_size * sizeof(uint));
            }
          }
        }
        else if (payload_sra[0] == 4) { // Reset
          for (auto iter = buffers.begin(); iter != buffers.end(); ) {
            printf("reset memory 0x%08x\r\n", (uint32_t)*iter);
            delete *iter;
            iter = buffers.erase(iter);
          }
        }
      }
#endif

      if (send(sock_sra, (char*)payload_sra, 12, 0) < 0) {
        break;
      }

#ifndef _WIN32
      if (((payload_sra[0] == 1) && (payload_sra[1] == 1)) ||
        ((payload_sra[0] == 2) && (payload_sra[1] == 2))) {
        if (send(sock_dma, (char*)&payload_mem[0], dma_size * sizeof(uint), 0) < 0) {
          printf("send dma data error\r\n");
          break;
        }
      }
#endif
    }

    printf("client disconnected\r\n");

#ifdef _WIN32
    closesocket(sock_sra);
    closesocket(sock_dma);
#else
    close(sock_sra);
    close(sock_dma);
#endif
  }

#ifdef _WIN32
  closesocket(listen_sra);
  closesocket(listen_dma);
#else
  close(sock_sra);
  close(sock_dma);
#endif

  return 0;
}