
#include "../headers/telemetry.h"
#include "../headers/tangle.h"
#include "../headers/peers2.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <iomanip>

#include <curl/curl.h>
#include <jsoncpp/json/json.h>

static std::string now_iso8601()
{
	using namespace std::chrono;
	auto t = system_clock::now();
	auto tt = system_clock::to_time_t(t);
	auto ms = duration_cast<milliseconds>(t.time_since_epoch()) % 1000;

	std::ostringstream ss;
	ss << std::put_time(gmtime(&tt), "%Y-%m-%dT%H:%M:%S");
	ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
	return ss.str();
}



static std::string slurp(const std::string &path)
{
	std::ifstream f(path);
	if (!f)
		return "";
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

class MetricsSampler
{
public:
	MetricsSampler()
	{
		read_cpu_prev();
		read_net_prev();
	}

	MetricSample sample()
	{
		MetricSample m;
		m.ts = now_iso8601();

		uint64_t total = 0, idle = 0;
		if (read_cpu(total, idle))
		{
			uint64_t td = total - prev_total;
			uint64_t id = idle - prev_idle;
			double cpu = 0.0;
			if (td > 0)
				cpu = (double)(td - id) * 100.0 / (double)td;
			m.cpu_percent = cpu;
			prev_total = total;
			prev_idle = idle;
		}

		uint64_t mem_total_kb = 0, mem_avail_kb = 0;
		read_meminfo(mem_total_kb, mem_avail_kb);
		m.ram_total_mb = mem_total_kb / 1024;
		m.ram_used_mb = (mem_total_kb > mem_avail_kb) ? ((mem_total_kb - mem_avail_kb) / 1024) : 0;

		uint64_t sent = 0, recv = 0;
		if (read_net(sent, recv))
		{
			m.net_bytes_sent = (sent >= prev_net_sent) ? (sent - prev_net_sent) : 0;
			m.net_bytes_recv = (recv >= prev_net_recv) ? (recv - prev_net_recv) : 0;
			prev_net_sent = sent;
			prev_net_recv = recv;
		}

		return m;
	}

private:
	uint64_t prev_total = 0, prev_idle = 0;
	uint64_t prev_net_sent = 0, prev_net_recv = 0;

	bool read_cpu(uint64_t &total_out, uint64_t &idle_out)
	{
		auto s = slurp("/proc/stat");
		if (s.empty())
			return false;
		std::istringstream ss(s);
		std::string line;
		if (!std::getline(ss, line))
			return false;
		std::istringstream ls(line);
		std::string label;
		ls >> label;
		uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
		ls >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
		total_out = user + nice + system + idle + iowait + irq + softirq + steal;
		idle_out = idle + iowait;
		return true;
	}
	void read_cpu_prev()
	{
		uint64_t t, i;
		if (read_cpu(t, i))
		{
			prev_total = t;
			prev_idle = i;
		}
	}

	bool read_meminfo(uint64_t &total_kb, uint64_t &avail_kb)
	{
		auto s = slurp("/proc/meminfo");
		if (s.empty())
			return false;
		total_kb = avail_kb = 0;
		std::istringstream ss(s);
		std::string line;
		while (std::getline(ss, line))
		{
			if (line.rfind("MemTotal:", 0) == 0)
			{
				std::istringstream ls(line);
				std::string key;
				uint64_t val;
				std::string unit;
				ls >> key >> val >> unit;
				total_kb = val;
			}
			else if (line.rfind("MemAvailable:", 0) == 0)
			{
				std::istringstream ls(line);
				std::string key;
				uint64_t val;
				std::string unit;
				ls >> key >> val >> unit;
				avail_kb = val;
			}
			if (total_kb && avail_kb)
				break;
		}
		return total_kb > 0;
	}

	bool read_net(uint64_t &sent_out, uint64_t &recv_out)
	{
		auto s = slurp("/proc/net/dev");
		if (s.empty())
			return false;
		std::istringstream ss(s);
		std::string line;
		std::getline(ss, line);
		std::getline(ss, line); // skip headers
		uint64_t total_sent = 0, total_recv = 0;
		while (std::getline(ss, line))
		{
			std::istringstream ls(line);
			std::string iface;
			if (!(ls >> iface))
				continue;
			if (iface.back() == ':')
				iface.pop_back();
			uint64_t recv_bytes = 0, dummy = 0, sent_bytes = 0;
			ls >> recv_bytes;
			for (int i = 0; i < 7; i++)
				ls >> dummy; // skip to sent field
			ls >> sent_bytes;
			if (iface == "lo")
				continue;
			total_recv += recv_bytes;
			total_sent += sent_bytes;
		}
		sent_out = total_sent;
		recv_out = total_recv;
		return true;
	}
	void read_net_prev()
	{
		uint64_t s = 0, r = 0;
		if (read_net(s, r))
		{
			prev_net_sent = s;
			prev_net_recv = r;
		}
	}
};

// ---------- global telemetry storage (file-scope globals) ----------
static std::vector<MetricSample> g_metrics;
static std::mutex g_metrics_mutex;
static std::atomic<bool> g_collect_running(false);
static std::thread g_collector_thread;

// Start collector: spawns a background thread to sample metrics every interval_ms
inline void startTelemetryCollector(int interval_ms)
{
	if (g_collect_running.load())
		return;
	g_collect_running.store(true);
	g_collector_thread = std::thread([interval_ms]()
									 {
        MetricsSampler sampler;
        while (g_collect_running.load()) {
            MetricSample s = sampler.sample();
            {
                std::lock_guard<std::mutex> lg(g_metrics_mutex);
                g_metrics.push_back(std::move(s));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        } });
}

// Stop and join collector
inline void stopTelemetryCollector()
{
	if (!g_collect_running.load())
		return;
	g_collect_running.store(false);
	if (g_collector_thread.joinable())
		g_collector_thread.join();
}

// Snapshot and clear collected metrics (thread-safe)
inline std::vector<MetricSample> snapshotAndClearMetrics()
{
	std::vector<MetricSample> v;
	{
		std::lock_guard<std::mutex> lg(g_metrics_mutex);
		v.swap(g_metrics);
	}
	return v;
}

// ---------- libcurl HTTP POST for JSON ----------
struct HttpResult
{
	bool ok = false;
	long http_code = 0;
	std::string body;
	std::string err;
};

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	((std::string *)userp)->append((char *)contents, size * nmemb);
	return size * nmemb;
}

inline HttpResult http_post_json(const std::string &url, const std::string &payload, const std::string &api_key = "")
{
	HttpResult res;
	CURL *curl = curl_easy_init();
	if (!curl)
	{
		res.err = "curl_init_failed";
		return res;
	}

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	if (!api_key.empty())
	{
		std::string h = "x-api-key: " + api_key;
		headers = curl_slist_append(headers, h.c_str());
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payload.size());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode rc = curl_easy_perform(curl);
	if (rc != CURLE_OK)
	{
		res.err = curl_easy_strerror(rc);
	}
	else
	{
		long code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		res.http_code = code;
		res.ok = (code >= 200 && code < 300);
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return res;
}

// ---------- Build JSON payload using jsoncpp ----------
// The code expects Tangle::getAllTransactions() to return std::vector<Transaction>
// where Transaction has `.data` and `.metadata` members as in your example.

// template <typename Tangle, typename PeersT>
inline std::string buildTelemetryPayloadJson(const std::string &nodeId,

											 Tangle &tangle,
											 Peers &peers,
											 std::vector<MetricSample> &metrics)
{

	auto txs = tangle.getAllTransactions();
	auto peersList = peers.getPeerList();

	Json::Value root(Json::objectValue);
	root["nodeId"] = nodeId;

	root["ts_end"] = now_iso8601();

	// tangle array
	Json::Value tangle_arr(Json::arrayValue);
	for (auto &[tx_id, tx] : txs)
	{
		Json::Value jtx(Json::objectValue);

		jtx["tx_id"] = tx.data.transaction_id;
		// parents array
		Json::Value pars(Json::arrayValue);
		for (const auto &p : tx.data.parents)
			pars.append(p);
		jtx["parents"] = pars;

		// add other fields (best-effort)
		jtx["timestamp"] = std::to_string(tx.data.timestamp); // if timestamp is time_t
		jtx["sender"] = tx.data.sender;
		jtx["receiver"] = tx.data.receiver;
		jtx["amount"] = tx.data.amount;
		jtx["unit"] = tx.data.unit;
		jtx["price_per_unit"] = tx.data.price_per_unit;
		jtx["currency"] = tx.data.currency;

		// metadata
		jtx["cumulative_weight"] = Json::Value((Json::UInt64)tx.metadata.cumulative_weight);
		jtx["lastUpdated"] = Json::Value((Json::UInt64)tx.metadata.lastUpdated);
		jtx["signature1"] = tx.metadata.signature1;
		jtx["signature2"] = tx.metadata.signature2;
		jtx["checksum"] = tx.metadata.checksum;

		jtx["consensusTimestamp"] = Json::Value((Json::UInt64)tx.metadata.consensusTimestamp);
		jtx["consensusDuration"] = Json::Value((Json::UInt64)tx.metadata.consensusDuration);
		jtx["verificationTimestamp"] = Json::Value((Json::UInt64)tx.metadata.verificationTimestamp);
		jtx["verificationDuration"] = Json::Value((Json::UInt64)tx.metadata.verificationDuration);
		jtx["powDuration"] = Json::Value((Json::UInt64)tx.metadata.powDuration);
		jtx["tsaDuration"] = Json::Value((Json::UInt64)tx.metadata.tsaDuration);
		jtx["completionDuration"] = Json::Value((Json::UInt64)tx.metadata.completionDuration);

		// hops: assuming tx.metadata.hops is a vector of pairs (timestamp, uid)
		Json::Value hops_arr(Json::arrayValue);
		for (const auto &hop : tx.metadata.hops)
		{
			Json::Value hop_obj(Json::objectValue);
			hop_obj["timestamp"] = Json::Value((Json::UInt64)hop.first);
			hop_obj["uid"] = hop.second;
			hops_arr.append(hop_obj);
		}
		jtx["hops"] = hops_arr;

		tangle_arr.append(jtx);
	}
	root["tangle"] = tangle_arr;

	// peersList
	Json::Value peersList_arr(Json::arrayValue);
	for (const auto &[id, peer] : peersList)
	{
		Json::Value p(Json::objectValue);
		p["id"] = peer.id;
		p["address"] = peer.address;
		p["port"] = peer.port;
		p["uri"] = peer.uri;

		p["state"] = static_cast<int>(peer.state); // assuming status is a string

		peersList_arr.append(p);
	}

	root["peers"] = peersList_arr;

	// metrics: split into cpu, ram, net arrays
	Json::Value metrics_arr(Json::arrayValue);
	for (const auto &m : metrics)
	{
		Json::Value m_obj(Json::objectValue);
		m_obj["ts"] = m.ts;	
		m_obj["cpu_percent"] = m.cpu_percent;
		m_obj["ram_total_mb"] = Json::UInt64(m.ram_total_mb);
		m_obj["ram_used_mb"] = Json::UInt64(m.ram_used_mb);
		m_obj["net_bytes_sent"] = Json::UInt64(m.net_bytes_sent);
		m_obj["net_bytes_recv"] = Json::UInt64(m.net_bytes_recv);

		metrics_arr.append(m_obj);
	}
	
	root["metrics"] = metrics_arr;

	// writer
	Json::StreamWriterBuilder w;
	w["indentation"] = ""; // compact
	return Json::writeString(w, root);
}

// ---------- Top-level send function ----------
// This builds payload (pulls transactions + peers internally from provided objects),
// and posts JSON to endpoint with provided api_key.

inline bool sendTelemetry(const std::string &endpoint,

						  const std::string &nodeId,
						  Tangle &tangle,
						  Peers &peers,
						  std::vector<MetricSample> &metrics)
{
	std::string payload = buildTelemetryPayloadJson(nodeId, tangle, peers, metrics);

	HttpResult r = http_post_json(endpoint, payload);
	if (!r.ok)
	{
		std::cerr << "[telemetry] POST failed: code=" << r.http_code << " err=" << r.err << " body=" << r.body << "\n";
		return false;
	}
	std::cout << "[telemetry] POST success: " << r.body << "\n";
	return true;
}