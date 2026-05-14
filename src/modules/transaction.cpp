#include "../headers/transaction.h"
#include "../headers/tangle.h"
#include "../headers/utils.h"
#include <sodium.h>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <json/json.h> // jsoncpp
#include <string>
#include <unordered_set>
#include <cstdint>
#include <sstream>
#include <memory>

using namespace std;

// signature is done aganst the serialized transaction data and not the entire transaction object
// because the transaction object contains metadata that is not part of the signature
// HENCE msg = serializeTransactionData(tx)
// and not serializeTransaction(tx)
string signTransaction(const std::string &msg)
{	
	std::vector<unsigned char> sig(crypto_sign_BYTES);

	auto sk = from_base64(getenv("SK_b64"));
	if (sk.size() != crypto_sign_SECRETKEYBYTES)
	{
		throw std::runtime_error("Invalid secret key size");
	}

	if (sodium_init() < 0)
	{
		throw std::runtime_error("Failed to initialize libsodium");
	}

	auto pk = from_base64(getenv("PK_b64"));
	auto uid = from_base64(getenv("UID"));

	// std::cout << "[SIGNTX] signing tx with sk, pk, uid: "
	// 	<< to_base64(sk.data(), sk.size()) << ", "
	// 	<< to_base64(pk.data(), pk.size()) << ", "
	// 	<< to_base64(uid.data(), uid.size()) << "\n";

	crypto_sign_detached(
		sig.data(), nullptr,
		reinterpret_cast<const unsigned char *>(msg.data()), msg.size(),
		sk.data());

	// std::cout << "[SIGNTX] Signature generated: "
	// 	<< to_base64(sig.data(), sig.size()) << "\n";

	return to_base64(sig.data(), sig.size());


};

bool verifyTransaction(const std::string &msg, const std::string &sig_b64, const std::string &uid)
{
	// auto pk = from_base64(getenv("PK_b64"));
	auto pk = from_base64(uid);

	// std::cout << "[VERIFYTX] verifying tx with pk: "
	// 	<< to_base64(pk.data(), pk.size()) << "\n";

	// std::cout << "[VERIFYTX] Signature to verify: "
	// 	<< sig_b64 << "\n";

	if (pk.size() != crypto_sign_PUBLICKEYBYTES)
	{
		throw std::runtime_error("Invalid public key size");
	}

	if (sodium_init() < 0)
	{
		throw std::runtime_error("Failed to initialize libsodium");
	}

	auto sig = from_base64(sig_b64);
	return crypto_sign_verify_detached(
			   sig.data(),
			   reinterpret_cast<const unsigned char *>(msg.data()), msg.size(),
			   pk.data()) == 0;
};

string serializeTransaction(const Transaction &tx, bool pretty){
	Json::Value root;
	Json::Value jdata;

	jdata["transaction_id"] = tx.data.transaction_id;
	jdata["sender"] = tx.data.sender;
	jdata["receiver"] = tx.data.receiver;
	jdata["amount"] = tx.data.amount;
	jdata["unit"] = tx.data.unit;
	jdata["price_per_unit"] = tx.data.price_per_unit;
	jdata["currency"] = tx.data.currency;
	jdata["timestamp"] = Json::Int64(tx.data.timestamp);

	for (const auto &p : tx.data.parents)
		jdata["parents"].append(p);

	root["data"] = jdata;

	// metadata
	Json::Value jmeta;
	jmeta["lastUpdated"] = Json::Int64(tx.metadata.lastUpdated);

	// weightMap: unordered_set -> array
	std::vector<std::string> wm(tx.metadata.weightMap.begin(), tx.metadata.weightMap.end());
	
	for (const auto &id : wm)
		jmeta["weightMap"].append(id);

	jmeta["cumulative_weight"] = tx.metadata.cumulative_weight;
	jmeta["reference_count"] = tx.metadata.reference_count;
	jmeta["status"] = static_cast<int>(tx.metadata.status);
	jmeta["votes"] = tx.metadata.votes;

	// voted_by: unordered_set -> array
	std::vector<std::string> vb(tx.metadata.voted_by.begin(), tx.metadata.voted_by.end());
	for (const auto &id : vb)
		jmeta["voted_by"].append(id);

	jmeta["signature1"] = tx.metadata.signature1;
	jmeta["signature2"] = tx.metadata.signature2;
	jmeta["checksum"] = tx.metadata.checksum;

	jmeta["consensusTimestamp"] = Json::Int64(tx.metadata.consensusTimestamp);
	jmeta["consensusDuration"] = Json::Int64(tx.metadata.consensusDuration);

	jmeta["verificationTimestamp"] = Json::Int64(tx.metadata.verificationTimestamp);
	jmeta["verificationDuration"] = Json::Int64(tx.metadata.verificationDuration);

	jmeta["tsaDuration"] = Json::Int64(tx.metadata.tsaDuration);
	jmeta["completionDuration"] = Json::Int64(tx.metadata.completionDuration);

	jmeta["propagationDelay"] = Json::Int64(tx.metadata.propagationDelay);
	jmeta["avgPropagationDelay"] = Json::Int64(tx.metadata.avgPropagationDelay);

	// hops
	for (const auto &h : tx.metadata.hops)
	{
		Json::Value jh;
		jh["timestamp"] = Json::Int64(h.first);
		jh["uid"] = h.second;
		jmeta["hops"].append(jh);
	}

	root["metadata"] = jmeta;

	Json::StreamWriterBuilder builder;
	if (pretty)
		builder["indentation"] = "  ";
	else
		builder["indentation"] = "";
	std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
	std::ostringstream os;
	writer->write(root, &os);
	return os.str();
}

Transaction deserializeTransaction(const std::string &jsonStr){
	Transaction tx;
	Json::CharReaderBuilder rbuilder;
	rbuilder["collectComments"] = false;
	std::string errs;
	Json::Value root;
	std::istringstream is(jsonStr);
	if (!Json::parseFromStream(rbuilder, is, &root, &errs))
	{
		throw std::invalid_argument(std::string("JSON parse error: ") + errs);
	}

	// data
	if (root.isMember("data") && root["data"].isObject())
	{
		const Json::Value &jdata = root["data"];
		if (jdata.isMember("transaction_id"))
			tx.data.transaction_id = jdata["transaction_id"].asString();
		if (jdata.isMember("sender"))
			tx.data.sender = jdata["sender"].asString();
		if (jdata.isMember("receiver"))
			tx.data.receiver = jdata["receiver"].asString();
		if (jdata.isMember("amount"))
			tx.data.amount = jdata["amount"].asDouble();
		if (jdata.isMember("unit"))
			tx.data.unit = jdata["unit"].asString();
		if (jdata.isMember("price_per_unit"))
			tx.data.price_per_unit = jdata["price_per_unit"].asDouble();
		if (jdata.isMember("currency"))
			tx.data.currency = jdata["currency"].asString();
		if (jdata.isMember("timestamp"))
			tx.data.timestamp = jdata["timestamp"].asInt64();

		if (jdata.isMember("parents") && jdata["parents"].isArray())
		{
			tx.data.parents.clear();
			for (const auto &jp : jdata["parents"])
				tx.data.parents.push_back(jp.asString());
		}
	}

	// metadata
	if (root.isMember("metadata") && root["metadata"].isObject())
	{
		const Json::Value &jmeta = root["metadata"];
		if (jmeta.isMember("lastUpdated"))
			tx.metadata.lastUpdated = jmeta["lastUpdated"].asInt64();

		if (jmeta.isMember("weightMap") && jmeta["weightMap"].isArray())
		{
			tx.metadata.weightMap.clear();
			for (const auto &jwm : jmeta["weightMap"])
				tx.metadata.weightMap.insert(jwm.asString());
		}

		if (jmeta.isMember("cumulative_weight"))
			tx.metadata.cumulative_weight = jmeta["cumulative_weight"].asInt();
		if (jmeta.isMember("reference_count"))
			tx.metadata.reference_count = jmeta["reference_count"].asInt();
		if (jmeta.isMember("status"))
			tx.metadata.status = static_cast<TransactionStatus>(jmeta["status"].asInt());
		if (jmeta.isMember("votes"))
			tx.metadata.votes = jmeta["votes"].asInt();
		if (jmeta.isMember("voted_by") && jmeta["voted_by"].isArray())
		{
			tx.metadata.voted_by.clear();
			for (const auto &jv : jmeta["voted_by"])
				tx.metadata.voted_by.insert(jv.asString());
		}
		if (jmeta.isMember("signature1"))
			tx.metadata.signature1 = jmeta["signature1"].asString();
		if (jmeta.isMember("signature2"))
			tx.metadata.signature2 = jmeta["signature2"].asString();
		if (jmeta.isMember("checksum"))
			tx.metadata.checksum = jmeta["checksum"].asString();

		if (jmeta.isMember("consensusTimestamp"))
			tx.metadata.consensusTimestamp = jmeta["consensusTimestamp"].asInt64();
		if (jmeta.isMember("consensusDuration"))
			tx.metadata.consensusDuration = jmeta["consensusDuration"].asInt64();

		if (jmeta.isMember("verificationTimestamp"))
			tx.metadata.verificationTimestamp = jmeta["verificationTimestamp"].asInt64();
		if (jmeta.isMember("verificationDuration"))
			tx.metadata.verificationDuration = jmeta["verificationDuration"].asInt64();

		if (jmeta.isMember("tsaDuration"))
			tx.metadata.tsaDuration = jmeta["tsaDuration"].asInt64();
		if (jmeta.isMember("completionDuration"))
			tx.metadata.completionDuration = jmeta["completionDuration"].asInt64();

		if (jmeta.isMember("propagationDelay"))
			tx.metadata.propagationDelay = jmeta["propagationDelay"].asInt64();
		if (jmeta.isMember("avgPropagationDelay"))
			tx.metadata.avgPropagationDelay = jmeta["avgPropagationDelay"].asInt64();

		if (jmeta.isMember("hops") && jmeta["hops"].isArray())
		{
			tx.metadata.hops.clear();
			for (const auto &jh : jmeta["hops"])
			{
				if (jh.isObject() && jh.isMember("timestamp") && jh.isMember("uid"))
				{
					int64_t ts = jh["timestamp"].asInt64();
					std::string uid = jh["uid"].asString();
					tx.metadata.hops.emplace_back(ts, uid);
				}
				else
				{
					// skip malformed hop entries (or throw)
				}
			}
		}
	}

	return tx;
}

string serializeTransactionData(const Transaction &tx)
{
	// Serialize only the tx_data part for signing/verifying
	Json::Value root;
	Json::Value jdata;

	jdata["transaction_id"] = tx.data.transaction_id;
	jdata["sender"] = tx.data.sender;
	jdata["receiver"] = tx.data.receiver;
	jdata["amount"] = tx.data.amount;
	jdata["unit"] = tx.data.unit;
	jdata["price_per_unit"] = tx.data.price_per_unit;
	jdata["currency"] = tx.data.currency;
	jdata["timestamp"] = Json::Int64(tx.data.timestamp);

	for (const auto &p : tx.data.parents)
		jdata["parents"].append(p);

	root["data"] = jdata;

	Json::StreamWriterBuilder builder;
	builder["indentation"] = ""; // No pretty print for signing
	std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
	std::ostringstream os;
	writer->write(root, &os);
	return os.str();
}