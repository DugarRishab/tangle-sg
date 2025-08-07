#include "../headers/transaction.h"
#include "../headers/tangle.h"
#include "../headers/utils.h"
#include <sodium.h>
#include <vector>
#include <stdexcept>
#include <cstdlib>

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


