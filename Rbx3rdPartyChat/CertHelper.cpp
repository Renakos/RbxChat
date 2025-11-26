#include "pch.h"
#include "CertHelper.h"

void CertHelper::load_self_signed_cert(boost::asio::ssl::context& ctx) {

    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) throw std::runtime_error("EVP_PKEY_new failed");

    RSA* rsa = RSA_generate_key(2048, RSA_F4, NULL, NULL);
    if (!rsa) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("RSA_generate_key failed");
    }

    if (EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        RSA_free(rsa);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("EVP_PKEY_assign_RSA failed");
    }

    X509* x509 = X509_new();
    if (!x509) {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("X509_new failed");
    }

    X509_set_version(x509, 2);

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    X509_gmtime_adj(X509_get_notBefore(x509), 0);

    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L);

    X509_set_pubkey(x509, pkey);

    X509_NAME* name = X509_get_subject_name(x509);
    if (!name) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("X509_get_subject_name failed");
    }

    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"ZeroTierP2P", -1, -1, 0);

    X509_set_issuer_name(x509, name);

    if (X509_sign(x509, pkey, EVP_sha256()) == 0) {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("X509_sign failed");
    }

    BIO* cert_bio = BIO_new(BIO_s_mem());
    BIO* key_bio = BIO_new(BIO_s_mem());
    if (!cert_bio || !key_bio) {
        BIO_free(cert_bio);
        BIO_free(key_bio);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("BIO_new failed");
    }

    if (!PEM_write_bio_X509(cert_bio, x509) || !PEM_write_bio_PrivateKey(key_bio, pkey, NULL, NULL, 0, NULL, NULL)) {
        BIO_free(cert_bio);
        BIO_free(key_bio);
        X509_free(x509);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("PEM_write_bio failed");
    }

    char* cert_data = nullptr;
    long cert_len = BIO_get_mem_data(cert_bio, &cert_data);

    ctx.use_certificate_chain(boost::asio::buffer(cert_data, cert_len));

    char* key_data = nullptr;
    long key_len = BIO_get_mem_data(key_bio, &key_data);

    ctx.use_private_key(boost::asio::buffer(key_data, key_len), boost::asio::ssl::context::pem);

    BIO_free(cert_bio);
    BIO_free(key_bio);
    X509_free(x509);
    EVP_PKEY_free(pkey);
}