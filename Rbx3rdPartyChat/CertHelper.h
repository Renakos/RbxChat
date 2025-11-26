#pragma warning(disable : 4996) // unsafe funxtions
#pragma once

#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
class CertHelper
{
public:
    static void load_self_signed_cert(boost::asio::ssl::context &ctx);
};