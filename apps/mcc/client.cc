#include "application.h"
#include "connection.h"
#include "reactor.h"
#include "log.h"

#include "smp.h"
#include "distributor.h"

#include <vector>
#include <random>

#include <time.h>

#define MAXRAND 100
#define NUM_RTT 5000

using namespace infgen;
using namespace std;

namespace bpo = boost::program_options;
logger client_logger("client_log", true);


class client {
private:
  unsigned nr_conns_;
  unsigned epoch_;
  unsigned burst_;
  unsigned setup_time_;
  double request_ratio_;
  unsigned wait_time_;
  unsigned duration_;
	int think_time_; /// ns

  std::vector<connptr> conns_;
  std::vector<int> ref_;

  //@ wuwenqing for fixed length of payload
	unsigned req_length_;
  unsigned prio_grain_; //priority grain (packet vs. flow)
  std::string heartbeat_;
  std::string request_;
	const char* req_ptr_;
	const char* hrt_ptr_;

  distributor<client>* container_;
public :
  struct metrics {
    unsigned connected;
    unsigned retry;
    unsigned send;
    unsigned request;
    unsigned received;
    uint64_t rtts[NUM_RTT+1]{};
  };

  metrics stats_sec, stats_log;
private:
  void clear_stats(metrics& stats) {
    stats.send = 0;
    stats.request = 0;
    stats.received = 0;
  }

  void send_request(unsigned j) {
    conns_[j]->send_packet(/*request_data*/ request_);
    stats_sec.request++;
    stats_sec.send++;
    stats_log.request++;
    stats_log.send++;
		
	conns_[j]->req_cnt_++;
	memcpy((void *)req_ptr_, &conns_[j]->req_cnt_, 4);
	memcpy((void *)hrt_ptr_, &conns_[j]->req_cnt_, 4);
  }

  void send_heartbeat(unsigned j) {
    conns_[j]->send_packet(/*heartbeat_data*/ heartbeat_ );
    stats_sec.send++;
    stats_log.send++;
		
		conns_[j]->req_cnt_++;
		memcpy((void *)req_ptr_, &conns_[j]->req_cnt_, 4);
		memcpy((void *)hrt_ptr_, &conns_[j]->req_cnt_, 4);
  }

  unsigned Fibonacci_service(int delay) { /// ns
  	unsigned pre = 0;
		unsigned cur = 1;
		int loops = delay * 0.75;

		while (loops-- > 0) {
			cur += pre;
			pre = cur - pre;
		}

		return pre;
  }
public:
  client(unsigned conns, unsigned epoch, unsigned burst, unsigned setup_time,
         unsigned wait_time, unsigned duration, int think_time, double ratio, unsigned req_length, unsigned prio_grain)
      : nr_conns_(conns), epoch_(epoch), burst_(burst), setup_time_(setup_time),
        request_ratio_(ratio), wait_time_(wait_time), duration_(duration), think_time_(think_time),
				req_length_(req_length), prio_grain_(prio_grain),
        heartbeat_(req_length, 0), request_(req_length, 0),
        stats_sec(metrics{}), stats_log(metrics{}){
    		request_[5] = 0x01;
    		request_[6] = 0x02;

    		heartbeat_[5] = 0x00;
  	  		heartbeat_[6] = 0x02;
  	  		heartbeat_[8] = 0x08;

			req_ptr_ = request_.data();
			hrt_ptr_ = heartbeat_.data();
		}

  void set_container(distributor<client>* container) {
  	container_ = container;
  }

  uint64_t connected_sec() { return stats_sec.connected; }
  uint64_t send_sec() { return stats_sec.send; }
  uint64_t request_sec() { return stats_sec.request; }
  uint64_t received_sec() { return stats_sec.received; }
  uint64_t retry_sec() { return stats_sec.retry; }

  uint64_t connected_log() { return stats_log.connected; }
  uint64_t send_log() { return stats_log.send; }
  uint64_t request_log() { return stats_log.request; }
  uint64_t received_log() { return stats_log.received; }

  void flush_log_stats() {
    stats_log.send = 0;
    stats_log.request = 0;
    stats_log.received = 0;
    for(int i = 0; i < NUM_RTT+1; i++)
      stats_log.rtts[i] = 0;

  }

  void start(ipv4_addr server_addr) {

	if (prio_grain_ == 1) {
		// Grain of priority: flow
		ref_.resize(burst_);
		std::iota(ref_.begin(), ref_.end(), 0);
		std::random_device rd;
		std::mt19937 g(rd());
		std::shuffle(ref_.begin(), ref_.end(), g);
	}
    auto block = nr_conns_ / setup_time_;
    for (unsigned i = 0; i < setup_time_; i++) {
      engine().add_oneshot_task_after(i * 1s, [=] {
        for (unsigned j = 0; j < block; j++) {
	  	  	//usleep(10);
          	auto conn = engine().connect(make_ipv4_address(server_addr));
					conn->req_cnt_ = 1;
          	conn->when_ready([this] (const connptr& conn) {
            stats_log.connected++;
            stats_sec.connected++;
            conns_.push_back(conn);
            if (conns_.size() >= nr_conns_) {
              app_logger.trace("all connections ready!");
            }
          });

          	conn->when_recved([this] (const connptr& conn) {
            stats_sec.received++;
            stats_log.received++;
            std::string s = conn->get_input().string();
            conn->get_input().consume(s.size());
            unsigned int idx = (conn->rtt) / 100000;
            if (idx < NUM_RTT)
              stats_log.rtts[idx]++;
            else
              stats_log.rtts[NUM_RTT]++;

            //@ wuwenqing, simulate 'Think time' forxx ns
						unsigned val = Fibonacci_service(think_time_); 
						request_[9] = static_cast<int>(val % 127);
          });

          conn->when_closed([this] {
            stats_sec.connected--;
            stats_log.connected--;
          });

          conn->when_failed([this] (const connptr& conn) {
            engine().add_oneshot_task_after(3s, [conn] {conn->reconnect(); });
          });

          conn->when_disconnect([this] (const connptr& conn) {
            stats_sec.connected--;
            stats_sec.retry++;

            stats_log.connected--;
            stats_log.retry++;
            engine().add_oneshot_task_after(3s, [conn] {conn->reconnect(); });
           });
        } //for(...)
      });
    }
    engine().add_oneshot_task_after(seconds(wait_time_ + setup_time_),
                                              [this] { do_req(); });

    engine().add_oneshot_task_after(seconds(duration_), [this] {
      for (auto c: conns_) {
        c->close();
      }
      engine().stop();
    });
  }

  void print_stats() {
    /*fmt::printf("[engine {}]\tconnected: {} \tretry: {}\tsend: {}\t"
                 "request: {}\treceived: {}\n", engine().cpu_id(),
                 stats_sec.connected, stats_sec.retry, stats_sec.send, stats_sec.request,
                 stats_sec.received);*/
    fprintf(stderr, "[engine %d]\tconnected: %u \tretry: %u\tsend: %u\t"
                 "request: %u\treceived: %u\n", engine().cpu_id(),
                 stats_sec.connected, stats_sec.retry, stats_sec.send, stats_sec.request,
                 stats_sec.received);
    clear_stats(stats_sec);
  }

  void do_req() {
    auto blocks = conns_.size() / burst_;
	auto remainder = conns_.size() % burst_;
    //blocks = blocks <= 0 ? 1 : blocks;
    blocks = remainder > 0 ? (blocks+1) : blocks;
    auto interval = epoch_ / blocks;
	int thre = (double)MAXRAND * request_ratio_ * 1.5;

	app_logger.info("interval: {}", interval);
	app_logger.info("blocks: {}", blocks);
    for (unsigned i = 0; i < blocks; i++) {
      engine().add_periodic_task_at<infinite>(
          system_clock::now() + i * milliseconds(interval), milliseconds(epoch_), [=] {
		  			int type_cnt = 0;
					int pri = -1;
            for (unsigned j = i * burst_;
                 j < (i + 1) * burst_ && j < conns_.size(); j++) {
              if (conns_[j]->get_state() == tcp_connection::state::connected) {
                clock_gettime(CLOCK_MONOTONIC, &conns_[j]->time_send);
					if (prio_grain_ == 1) { // flow-level priority
						//if (ref_[j % burst_] < static_cast<int>(burst_ * request_ratio_)) 
						pri = (double)rand() / (RAND_MAX+1.0) * MAXRAND;
    					//fmt::print("pri:{}\n", pri);
						if ((pri < thre) && 
							(type_cnt < static_cast<int>(burst_ * request_ratio_))){
							type_cnt += 1;
							send_request(j);
						} else {
							send_heartbeat(j);
						}
					} else { //packet-level priority
						pri = rand() % MAXRAND;
    					fmt::print("pri:{}\n", pri);
						if ((pri < thre) && (type_cnt < static_cast<int>(burst_ * request_ratio_))) {
							type_cnt += 1;
							send_request(j);
						} else {
							send_heartbeat(j);
						}
					}
              }
            }
          });
    }
  }
};

int main(int argc, char **argv) {
  application app;
  //add by songhui
  srand((unsigned)time(NULL));  
  app.add_options()
	("length,l", bpo::value<unsigned>()->default_value(16), "length of message (> 8)")
	("priority-level,p", bpo::value<unsigned>()->default_value(1), "Grain of priority (default flow-level)")
    /*("epoch,e", bpo::value<unsigned>()->default_value(1), "send epoch(s)")*/
    ("epoch,e", bpo::value<float>()->default_value(1.0), "send epoch(s)")
    ("burst,b", bpo::value<unsigned>()->default_value(1), "burst packets")
    ("conn,c", bpo::value<unsigned>()->default_value(1), "number of flows")
    ("setup-time,s", bpo::value<unsigned>()->default_value(1), "connection setup time(s)")
    ("wait-time,w", bpo::value<unsigned>()->default_value(1), "wait time before sending requests(s)")
    ("duration,d", bpo::value<unsigned>()->default_value(1000000), "duration of test")
    ("think-time,t", bpo::value<int>()->default_value(0), "think time between requests (ns)")
    ("request-ratio,r", bpo::value<double>()->default_value(1.0), "ratio of request packet")
    ("log-duration", bpo::value<unsigned>()->default_value(10), "log duration between logs");

  app.run(argc, argv, [&app] {
    auto &config = app.configuration();
    auto epoch = static_cast<unsigned>(1000 * config["epoch"].as<float>()); //Milliseconds
    auto conn = config["conn"].as<unsigned>();
    auto burst = config["burst"].as<unsigned>();
    auto setup = config["setup-time"].as<unsigned>();
    auto wait = config["wait-time"].as<unsigned>();
    auto duration = config["duration"].as<unsigned>();
    auto think_time = config["think-time"].as<int>();
    auto ratio = config["request-ratio"].as<double>();
    auto log_duration = config["log-duration"].as<unsigned>();
    auto dest = config["dest"].as<std::string>();
	auto length = config["length"].as<unsigned>();
	auto prio_grain = config["priority-level"].as<unsigned>();

    fmt::print(
        "configuration: \nconnections: {}\n  epoch: {}\n  burst: {}\n"
        "request_ratio: {}\n  threads: {}\n",
        conn, epoch, burst, ratio, smp::count-1);

    auto loaders = new distributor<client>;
	//auto conn_remain = conn % (smp::count-1);
    loaders->start(conn / (smp::count-1), epoch, burst / (smp::count-1),
        setup, wait, duration, think_time, ratio, length, prio_grain);
    loaders->invoke_on_all(&client::start, ipv4_addr(dest, 80));

    adder connected, send, request, received, retry;
    engine().add_periodic_task_at<infinite>(
        system_clock::now(), 1s, [&, loaders]() mutable {
          loaders->map_reduce(connected, &client::connected_sec);
          loaders->map_reduce(request, &client::request_sec);
          loaders->map_reduce(send, &client::send_sec);
          loaders->map_reduce(received, &client::received_sec);
          loaders->map_reduce(retry, &client::retry_sec);
          loaders->invoke_on_all(&client::print_stats);

          engine().add_oneshot_task_after(100ms, [&] () mutable {
            fprintf(stderr, "[ALL]\t\tconnected: %u\tretry: %u\tsend: %u\trequest: %u\treceived: %u\n",
                       connected.result(), retry.result(), send.result(), request.result(),
                       received.result());
            connected.reset();
            request.reset();
            send.reset();
            received.reset();
            retry.reset();
          });
    });

    adder send_log, request_log, received_log, connected_log;
    uint64_t rtt_log[NUM_RTT+1];
    for(int i = 0; i< NUM_RTT+1; i++)
      rtt_log[i] = 0;

    engine().add_periodic_task_at<infinite>(
        system_clock::now(), seconds(log_duration), [&, loaders]() mutable {
          loaders->map_reduce(connected_log, &client::connected_log);
          loaders->map_reduce(request_log, &client::request_log);
          loaders->map_reduce(send_log, &client::send_log);
          loaders->map_reduce(received_log, &client::received_log);
          for(unsigned int i = 1; i < smp::count; i++)
		    for(int j = 0; j < NUM_RTT+1; j++)
		      rtt_log[j] += loaders->instances_[i].service->stats_log.rtts[j];

          loaders->invoke_on_all(&client::flush_log_stats);

            int total_rtt = 0, ptr = 0, ptr_50 = 0, acc = 0;
            for(int i = 0; i < NUM_RTT+1; i++)
              total_rtt += rtt_log[i];
            for (ptr = 0; ptr < NUM_RTT; ptr++)
            {
              acc += rtt_log[ptr];
              if(0 == ptr_50 && acc >= total_rtt/2)
                ptr_50 = ptr;
              if (total_rtt - acc <= total_rtt * 0.01)
                break;
            }
            fprintf(stderr, "Total RTTs : %d\n", total_rtt);
            fprintf(stderr, "connected: %d\tsend: %d\t request: %d\t received: %d \t50th-RTT:%d\t 99th-RTT: %d\n",
                      connected_log.result(), send_log.result(), request_log.result(),
                      received_log.result(), ptr_50, ptr);

            for(int i = 0; i<NUM_RTT+1;i++)
              rtt_log[i]=0;
            connected_log.reset();
            request_log.reset();
            send_log.reset();
            received_log.reset();
    });
    engine().run();
  });
}


