#include "../headers/transaction.h"
#include "../headers/tangle.h"
#include "../headers/utils.h"
#include <sodium.h>
#include <fstream>
#include <filesystem>

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

	crypto_sign_detached(
		sig.data(), nullptr,
		reinterpret_cast<const unsigned char *>(msg.data()), msg.size(),
		sk.data());
	return to_base64(sig.data(), sig.size());
};

bool verifyTransaction(const std::string &msg, const std::string &sig_b64, const std::string &uid)
{
	// auto pk = from_base64(getenv("PK_b64"));
	auto pk = from_base64(uid);

	if (pk.size() != crypto_sign_PUBLICKEYBYTES)
	{
		throw std::runtime_error("Invalid public key size");
	}
	
	auto sig = from_base64(sig_b64);
	return crypto_sign_verify_detached(
			   sig.data(),
			   reinterpret_cast<const unsigned char *>(msg.data()), msg.size(),
			   pk.data()) == 0;
};


