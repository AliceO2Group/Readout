// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include <arpa/inet.h>
#include <atomic>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <malloc.h>
#include <netdb.h>
#include <poll.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "Consumer.h"
#include "MemoryBankManager.h"
#include "ReadoutUtils.h"

struct pdata {
  uint64_t buf_va;            // base address of server memory buffer
  uint32_t buf_rkey;          // key for RDMA access to memory buffer
  uint64_t buf_pageSize;      // size (bytes) of each page
  uint64_t buf_numberOfPages; // number of pages in buffer
  uint64_t maxPages;          // max number of pages to send
};

const uint64_t CHUNK_SIZE = 0.5 * 1024 * 1024;
const uint64_t N_CHUNK = 1000;
const unsigned int nloop = 10;

#define RESOLVE_TIMEOUT_MS 1000
#define MAX_WR 256

class ConsumerRDMA : public Consumer {
public:
  struct rdma_event_channel *cm_channel = nullptr;
  struct rdma_cm_id *cm_id = nullptr;
  struct ibv_pd *pd = nullptr;
  struct ibv_comp_channel *comp_chan = nullptr;
  struct ibv_cq *cq = nullptr;
  struct ibv_mr *mr = nullptr;
  struct pdata server_pdata;
  bool CQshutdownRequest = false;
  std::atomic<int> nAvailable; // maximum number of WR that can be issued concurrently

  int nPagesSent = 0; // number of pages sent
  
  //  int cfgMaxPages=0; // max number of pages to send
  //  int cfgRemotePageSize=1024*1024; // size of remote data page
  //  int cfgRemotePageNumber=1; // number of remote pages // TODO: get this on connect init

  ConsumerRDMA(ConfigFile &cfg, std::string cfgEntryPoint) : Consumer(cfg, cfgEntryPoint) {

    // configuration parameter: | consumer-rdma-* | port | int | 10001 | Remote server TCP port number to connect to. |
    std::string cfgPort = "10000"; // remote server port
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".port", cfgPort);

    // configuration parameter: | consumer-rdma-* | host | string | localhost | Remote server IP name to connect to. |
    std::string cfgHost = "localhost"; // remote server address
    cfg.getOptionalValue<std::string>(cfgEntryPoint + ".host", cfgHost);

    // cfg.getOptionalValue<int>(cfgEntryPoint + ".maxPages", cfgMaxPages);
    // cfg.getOptionalValue<int>(cfgEntryPoint + ".remotePageSize", cfgRemotePageSize);

    theLog.log(LogInfoDevel, "Looking for RDMA device...");

    // list devices
    struct ibv_device **device_list = nullptr;
    int num_devices = 0;
    device_list = ibv_get_device_list(&num_devices);
    if (!device_list) {
      throw __LINE__;
    }
    for (int i = 0; i < num_devices; ++i) {
      theLog.log(LogInfoDevel, "RDMA device[%d]: name=%s", i, ibv_get_device_name(device_list[i]));
    }
    ibv_free_device_list(device_list);
    if (num_devices == 0) {
      theLog.log(LogErrorDevel, "no device found");
      throw __LINE__;
    }

    // setup connection manager (CM)
    cm_channel = rdma_create_event_channel();
    if (cm_channel == nullptr) {
      throw __LINE__;
    }
    if (rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP)) {
      throw __LINE__;
    }

    theLog.log(LogInfoDevel_(3002), "Connecting to %s : %s", cfgHost.c_str(), cfgPort.c_str());

    /*
        if(cfgMaxPages) {
          theLog.log(LogInfoDevel_(3002), "Will send %d pages",cfgMaxPages);
        }
        if(cfgRemotePageSize) {
          theLog.log(LogInfoDevel_(3002), "Remote page size:",cfgRemotePageSize);
        }
    */

    // resolve server address
    struct addrinfo *addr_res;
    struct addrinfo addr_hints;
    memset(&addr_hints, 0, sizeof(addr_hints));
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(cfgHost.c_str(), cfgPort.c_str(), &addr_hints, &addr_res) < 0) {
      throw __LINE__;
    }
    bool addr_found = false;
    for (struct addrinfo *t = addr_res; t != nullptr; t = t->ai_next) {
      if (rdma_resolve_addr(cm_id, NULL, t->ai_addr, RESOLVE_TIMEOUT_MS) == 0) {
        addr_found = true;
        break;
      }
    }
    if (addr_res != nullptr) {
      freeaddrinfo(addr_res);
    }
    if (!addr_found) {
      throw __LINE__;
    }

    struct rdma_cm_event *event = nullptr;
    if (rdma_get_cm_event(cm_channel, &event)) {
      throw __LINE__;
    }
    // theLog.log(LogDebugTrace, "event:%s",rdma_event_str(event->event));
    if (event->event != RDMA_CM_EVENT_ADDR_RESOLVED) {
      throw __LINE__;
    }
    rdma_ack_cm_event(event);

    if (rdma_resolve_route(cm_id, RESOLVE_TIMEOUT_MS)) {
      throw __LINE__;
    }
    if (rdma_get_cm_event(cm_channel, &event)) {
      throw __LINE__;
    }
    if (event->event != RDMA_CM_EVENT_ROUTE_RESOLVED) {
      throw __LINE__;
    }
    rdma_ack_cm_event(event);

    // the struct ibv_context is available in cm_id->verbs
    struct ibv_port_attr port_attr;
    int port_num = 1;
    if (ibv_query_port(cm_id->verbs, port_num, &port_attr)) {
      throw __LINE__;
    }
    if (port_attr.state != IBV_PORT_ACTIVE) {
      theLog.log(LogInfoDevel, "port state NOT ACTIVE = %d ", port_attr.state);
    }

    int c_active_width[] = {1, 1, 2, 4, 4, 8, 12};
    float c_active_speed[] = {1, 2.5, 2, 5.0, 4, 10.0, 8, 10.0, 16, 14.0, 32, 25.0};
    int c_mtu[] = {IBV_MTU_256, 256, IBV_MTU_512, 512, IBV_MTU_1024, 1024, IBV_MTU_2048, 2048, IBV_MTU_4096, 4096};

    for (unsigned int i = 0; i < sizeof(c_mtu) / sizeof(int); i += 2) {
      if (port_attr.active_mtu == c_mtu[i]) {
        theLog.log(LogInfoDevel, "active_mtu = %d", c_mtu[i + 1]);
        break;
      }
    }

    theLog.log(LogInfoDevel, "RDMA max msg =%d", port_attr.max_msg_sz);

    for (unsigned int i = 0; i < sizeof(c_active_width) / sizeof(int); i += 2) {
      if (port_attr.active_width == c_active_width[i]) {
        theLog.log(LogInfoDevel, "active_width = %dx", c_active_width[i + 1]);
        break;
      }
    }
    for (unsigned int i = 0; i < sizeof(c_active_speed) / sizeof(float); i += 2) {
      if (port_attr.active_speed == c_active_speed[i]) {
        theLog.log(LogInfoDevel, "active_speed = %.1f Gbps", c_active_speed[i + 1]);
        break;
      }
    }

    // create protection domain (PD)
    pd = ibv_alloc_pd(cm_id->verbs);
    if (pd == nullptr) {
      throw __LINE__;
    }

    // create completion event channel
    comp_chan = ibv_create_comp_channel(cm_id->verbs);
    if (comp_chan == nullptr) {
      throw __LINE__;
    }

    // set completion channel non-blocking
    int flags = fcntl(comp_chan->fd, F_GETFL);
    if (fcntl(comp_chan->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      throw __LINE__;
    }
    flags = fcntl(comp_chan->fd, F_GETFL);
    printf("flags: O_NONBLOCK = %d\n", (flags & O_NONBLOCK) ? 1 : 0);

    printf("created comp_chan @ %d\n", comp_chan->fd);

    // create completion queue (CQ)
    cq = ibv_create_cq(cm_id->verbs, MAX_WR, NULL, comp_chan, 0);
    if (cq == nullptr) {
      throw __LINE__;
    }

    // request completion notification on CQ
    if (ibv_req_notify_cq(cq, 0)) {
      throw __LINE__;
    }

    // register memory region

    // registering all memory banks
    std::vector<MemoryBankManager::memoryRange> memoryRegions;
    theMemoryBankManager.getMemoryRegions(memoryRegions);

    // check if memory regions contiguous
    char *p0 = nullptr;
    char *p1 = nullptr;
    bool isContiguous = true;
    for (auto const &r : memoryRegions) {
      char *pr = (char *)r.offset;
      if (p0 == nullptr) {
        p0 = pr;
      } else {
        if (pr != p1) {
          isContiguous = false;
          break;
        }
      }
      p1 = &pr[r.size];
    }
    if (isContiguous) {
      size_t sz = p1 - p0;
      theLog.log(LogInfoDevel, "Banks contiguous, registering them in one go: %p - %p (size %lu)", p0, p1 - 1, sz);
      mr = ibv_reg_mr(pd, p0, sz, IBV_ACCESS_LOCAL_WRITE);
      if (mr == nullptr) {
        throw __LINE__;
      }
    } else {
      theLog.log(LogInfoDevel, "Banks not contiguous, configuration not supported");
      throw __LINE__;
    }

    // create queue pair (QP)
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.cap.max_send_wr = MAX_WR;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_wr = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = cq;
    qp_attr.qp_type = IBV_QPT_RC;
    if (rdma_create_qp(cm_id, pd, &qp_attr)) {
      throw __LINE__;
    }

    // connect to server
    struct rdma_conn_param conn_param;
    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.initiator_depth = 1;
    conn_param.retry_count = 7;

    if (rdma_connect(cm_id, &conn_param)) {
      throw __LINE__;
    }
    if (rdma_get_cm_event(cm_channel, &event)) {
      throw __LINE__;
    }
    printf("event:%s\n", rdma_event_str(event->event));
    if (event->event != RDMA_CM_EVENT_ESTABLISHED) {
      throw __LINE__;
    }
    // store data given by server
    // it contains the access parameters to the remote memory

    memcpy(&server_pdata, event->param.conn.private_data, sizeof(server_pdata));
    rdma_ack_cm_event(event);
    printf("remote buf @ %p\n", (void *)server_pdata.buf_va);

    theLog.log(LogInfoDevel, "Remote buffer : %lu bytes total, %lu pages x %lu bytes", server_pdata.buf_pageSize * server_pdata.buf_numberOfPages, server_pdata.buf_numberOfPages, server_pdata.buf_pageSize);

    nAvailable = MAX_WR;
    /*
    // start a thread for completion queue
    std::thread tCQ([&]() {
      while(!CQshutdownRequest) {

        printf("waiting CQ @ %d\n",comp_chan->fd);

        // poll the channel
        struct pollfd cq_pollfd;
        cq_pollfd.fd = comp_chan->fd;
        cq_pollfd.events  = POLLIN;
        cq_pollfd.revents = 0;
        int err=0;
        int ms_timeout = 1000;
        for(;err==0;) {
          printf("."); fflush(stdout);
          err = poll(&cq_pollfd, 1, ms_timeout);
          if (CQshutdownRequest) break;
        }
        if (err==0) {continue;} // timeout
        if (err<0) {break;}

        printf("reading CQ\n");
        continue;
        // wait for next event in CQ
        struct ibv_cq *evt_cq=nullptr;
        void *cq_context=nullptr;
        if (ibv_get_cq_event(comp_chan,&evt_cq, &cq_context)) {throw __LINE__;}

        // acknowledge the event (ONE event)
        ibv_ack_cq_events(evt_cq, 1);

        // request next event
        if (ibv_req_notify_cq(cq, 0)) {throw __LINE__;}

        //printf("emptying CQ\n");

        // empty CQ
        struct ibv_wc wc;
        for (;;) {
          int err=ibv_poll_cq(cq, 1, &wc);
          if (err<0) {throw __LINE__;}
          if (err==0) {break;}
          if (wc.status != IBV_WC_SUCCESS) {
            //printf("Completion with status 0x%x was found\n",wc.status);
          } else {
            //printf("WR success\n");
            nAvailable++;
          }
        }

      }

    });
    */
  }

  ~ConsumerRDMA() {}

  int pushData(DataBlockContainerReference &b) {
    //	nBytesSent+=b->getData()->header.dataSize;

  startCQHandle:

    // first, handle pending completion events

    // wait for next event in CQ
    // printf("check for CQ event in queue\n");
    struct ibv_cq *evt_cq = nullptr;
    void *cq_context = nullptr;
    if (ibv_get_cq_event(comp_chan, &evt_cq, &cq_context)) {
      goto endCQHandle;
    }
    // printf("got CQ event\n");

    // acknowledge the event (ONE event)
    ibv_ack_cq_events(evt_cq, 1);

    // request next event
    if (ibv_req_notify_cq(cq, 0)) {
      throw __LINE__;
    }

    // empty CQ
    struct ibv_wc wc;
    for (;;) {
      int err = ibv_poll_cq(cq, 1, &wc);
      if (err < 0) {
        throw __LINE__;
      }
      if (err == 0) {
        break;
      }
      if (wc.status != IBV_WC_SUCCESS) {
        // printf("Completion with status 0x%x was found\n",wc.status);
      } else {
        // printf("WR success\n");
        nAvailable++;
      }
    }

  endCQHandle:
    // printf("end of CQ event handler\n");

    if (nPagesSent >= server_pdata.maxPages) {
      if (nPagesSent == server_pdata.maxPages) {
        theLog.log(LogInfoDevel, "Max number of pages sent");
        nPagesSent++;
      }
      // we have reached quota
      return 0;
    }

    int32_t *ptr = (int32_t *)b->getData()->data;
    size_t size = b->getData()->header.dataSize;

    if (!nAvailable.load()) {
      // printf("."); fflush(stdout);
      usleep(1000);
      goto startCQHandle;
    }
    //      printf("%d available\n",nAvailable.load());

    nAvailable--;

    // scatter-gather element (SGE)
    struct ibv_sge sg_list;
    memset(&sg_list, 0, sizeof(sg_list));
    sg_list.addr = (uintptr_t)ptr;
    sg_list.length = size;
    sg_list.lkey = mr->lkey;

    // work request (WR)
    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 0; // b->getData()->header.id;
    wr.next = nullptr;
    wr.sg_list = &sg_list;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.wr.rdma.remote_addr = server_pdata.buf_va + (nPagesSent % server_pdata.buf_numberOfPages) * server_pdata.buf_pageSize;

    ptr[0] = size; // first word is the size of message transmitted
    // printf("Write remote @ page[%d]=%p\n",nPagesSent %
    // server_pdata.buf_numberOfPages,(void *)(wr.wr.rdma.remote_addr));
    /*      printf("Write remote @ %p\n",(void *)(wr.wr.rdma.remote_addr));
          printf("Local data @ %p : %08X %08X\n",(void *)ptr,ptr[0],ptr[1]);
      */
    if (size > server_pdata.buf_pageSize) {
      printf("error data bigger than remote page size\n");
    }
    wr.wr.rdma.rkey = server_pdata.buf_rkey;
    wr.send_flags = IBV_SEND_SIGNALED; // if not there, nothing happens in completion queue
    struct ibv_send_wr *bad_wr;
    // printf("sending %d\n",buf[i*CHUNK_SIZE]);

    if (ibv_post_send(cm_id->qp, &wr, &bad_wr)) {
      throw __LINE__;
    }

    nPagesSent++;

    return 0;
  }

private:
};

std::unique_ptr<Consumer> getUniqueConsumerRDMA(ConfigFile &cfg, std::string cfgEntryPoint) { return std::make_unique<ConsumerRDMA>(cfg, cfgEntryPoint); }
