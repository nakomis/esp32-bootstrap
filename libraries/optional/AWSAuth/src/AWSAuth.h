#ifndef CATCAM_AWSAUTH_H
#define CATCAM_AWSAUTH_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "SDLogger.h"

struct AWSCredentials {
    String accessKeyId;
    String secretAccessKey;
    String sessionToken;
    time_t expiration;
    bool isValid;
};

struct SigV4Headers {
    String authorization;
    String date;
    String securityToken;
    String contentType;
    String host;
    String payloadHash;  // SHA256 hash of the payload (for X-Amz-Content-Sha256 header)
    bool isValid;
};

class AWSAuth {
public:
    AWSAuth(const char* region = "eu-west-2");

    // Initialize with certificates from secrets.h
    bool initialize(const char* awsCertCA, const char* awsCertCRT, const char* awsCertPrivate,
                   const char* credentialsEndpoint);

    // Get temporary AWS credentials using IoT Credentials Provider
    bool getCredentialsWithRoleAlias(const char* roleAlias);

    // Create SigV4 signed headers for API Gateway request (for string payloads)
    SigV4Headers createSigV4Headers(const String& method, const String& uri,
                                   const String& host, const String& payload = "",
                                   const String& contentType = "application/json");

    // Create SigV4 signed headers for binary payloads (e.g., images)
    SigV4Headers createSigV4HeadersForBinary(const String& method, const String& uri,
                                             const String& host, const uint8_t* payload,
                                             size_t payloadSize,
                                             const String& contentType = "application/octet-stream");

    // Calculate SHA256 hash of binary data (returns lowercase hex string)
    String sha256HashBinary(const uint8_t* data, size_t len);

    // Get current credentials
    AWSCredentials getCurrentCredentials() const { return credentials; }

    // Check if credentials are still valid (not expired)
    bool areCredentialsValid() const;

    // Refresh credentials if needed
    bool refreshCredentialsIfNeeded(const char* roleAlias);

    // Utility methods
    String urlEncode(const String& str);

private:
    String region;
    String credentialsEndpointHost;
    const char* caCert;
    const char* clientCert;
    const char* clientKey;
    AWSCredentials credentials;

    // Helper methods for SigV4 signing
    String createCanonicalRequest(const String& method, const String& uri,
                                 const String& queryString, const String& headers,
                                 const String& signedHeaders, const String& payloadHash);

    String createStringToSign(const String& algorithm, const String& requestDateTime,
                             const String& credentialScope, const String& canonicalRequestHash);

    String createSignature(const String& stringToSign, const uint8_t* signingKey);

    void getSigningKey(uint8_t* output, const String& key, const String& dateStamp,
                       const String& regionName, const String& serviceName);

    void hmacSha256Raw(uint8_t* output, const uint8_t* key, size_t keyLen,
                       const uint8_t* data, size_t dataLen);
    String hmacSha256Hex(const uint8_t* key, size_t keyLen, const String& data);
    String sha256Hash(const String& data);
    String getISOTimestamp();
    String getDateStamp();
    String bytesToHex(const uint8_t* bytes, size_t len);

    // Internal method that takes pre-computed payload hash
    SigV4Headers createSigV4HeadersInternal(const String& method, const String& uri,
                                            const String& host, const String& payloadHash,
                                            const String& contentType);
};

#endif
