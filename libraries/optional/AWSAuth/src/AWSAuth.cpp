#include "AWSAuth.h"
#include <mbedtls/md.h>
#include <HTTPClient.h>

AWSAuth::AWSAuth(const char* region) : region(region) {
    credentials.isValid = false;
    credentials.expiration = 0;
    caCert = nullptr;
    clientCert = nullptr;
    clientKey = nullptr;
}

bool AWSAuth::initialize(const char* awsCertCA, const char* awsCertCRT, const char* awsCertPrivate,
                        const char* credentialsEndpoint) {
    caCert = awsCertCA;
    clientCert = awsCertCRT;
    clientKey = awsCertPrivate;
    credentialsEndpointHost = String(credentialsEndpoint);

    // Configure time for certificate validation and SigV4 signing
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    // Wait for time to be set
    SDLogger::getInstance().infof("AWSAuth: Waiting for NTP time sync...");
    time_t now = 0;
    int attempts = 0;
    while (now < 1700000000 && attempts < 20) { // Wait until time is reasonable (after Nov 2023)
        delay(500);
        time(&now);
        attempts++;
    }

    if (now < 1700000000) {
        SDLogger::getInstance().errorf("AWSAuth: Failed to get NTP time");
        return false;
    }

    SDLogger::getInstance().infof("AWSAuth: Initialized with credentials endpoint: %s", credentialsEndpointHost.c_str());
    return true;
}

bool AWSAuth::getCredentialsWithRoleAlias(const char* roleAlias) {
    // Use IoT Credentials Provider to get temporary AWS credentials
    // Endpoint format: https://<credentials-endpoint>/role-aliases/<role-alias>/credentials

    WiFiClientSecure sslClient;
    sslClient.setCACert(caCert);
    sslClient.setCertificate(clientCert);
    sslClient.setPrivateKey(clientKey);

    String url = "https://" + credentialsEndpointHost + "/role-aliases/" + String(roleAlias) + "/credentials";
    SDLogger::getInstance().infof("AWSAuth: Requesting credentials from: %s", url.c_str());

    HTTPClient http;
    http.begin(sslClient, url);
    http.addHeader("x-amzn-iot-thingname", "BootBootsThing");

    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String response = http.getString();
        SDLogger::getInstance().debugf("AWSAuth: Credentials response received");

        // Parse JSON response
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
            SDLogger::getInstance().errorf("AWSAuth: Failed to parse credentials JSON: %s", error.c_str());
            http.end();
            return false;
        }

        // Extract credentials from response
        // Response format: {"credentials":{"accessKeyId":"...","secretAccessKey":"...","sessionToken":"...","expiration":"..."}}
        JsonObject creds = doc["credentials"];

        if (creds.isNull()) {
            SDLogger::getInstance().errorf("AWSAuth: No credentials object in response");
            SDLogger::getInstance().errorf("AWSAuth: Response: %s", response.c_str());
            http.end();
            return false;
        }

        credentials.accessKeyId = creds["accessKeyId"].as<String>();
        credentials.secretAccessKey = creds["secretAccessKey"].as<String>();
        credentials.sessionToken = creds["sessionToken"].as<String>();

        // Parse expiration time (ISO 8601 format)
        String expirationStr = creds["expiration"].as<String>();
        // For now, set expiration to 1 hour from now (credentials typically valid for 1 hour)
        time_t now;
        time(&now);
        credentials.expiration = now + 3600;

        credentials.isValid = true;

        SDLogger::getInstance().infof("AWSAuth: Credentials obtained successfully");
        SDLogger::getInstance().infof("AWSAuth: Access Key: %s...", credentials.accessKeyId.substring(0, 8).c_str());

        http.end();
        return true;
    }

    SDLogger::getInstance().errorf("AWSAuth: Failed to get credentials. HTTP Code: %d", httpResponseCode);
    String response = http.getString();
    SDLogger::getInstance().errorf("AWSAuth: Response: %s", response.c_str());

    http.end();
    return false;
}

bool AWSAuth::areCredentialsValid() const {
    if (!credentials.isValid) {
        return false;
    }
    time_t now;
    time(&now);
    // Consider credentials invalid if they expire within 5 minutes
    return now < (credentials.expiration - 300);
}

bool AWSAuth::refreshCredentialsIfNeeded(const char* roleAlias) {
    if (!areCredentialsValid()) {
        SDLogger::getInstance().infof("AWSAuth: Refreshing expired credentials");
        return getCredentialsWithRoleAlias(roleAlias);
    }
    return true;
}

SigV4Headers AWSAuth::createSigV4Headers(const String& method, const String& uri,
                                         const String& host, const String& payload,
                                         const String& contentType) {
    // For string payloads, calculate the SHA256 hash
    String payloadHash = sha256Hash(payload);
    return createSigV4HeadersInternal(method, uri, host, payloadHash, contentType);
}

SigV4Headers AWSAuth::createSigV4HeadersForBinary(const String& method, const String& uri,
                                                   const String& host, const uint8_t* payload,
                                                   size_t payloadSize,
                                                   const String& contentType) {
    // For binary payloads, calculate the SHA256 hash of the binary data
    SDLogger::getInstance().infof("AWSAuth: Hashing binary payload of %d bytes", payloadSize);
    String payloadHash = sha256HashBinary(payload, payloadSize);
    SDLogger::getInstance().infof("AWSAuth: Payload hash: %s", payloadHash.c_str());
    return createSigV4HeadersInternal(method, uri, host, payloadHash, contentType);
}

SigV4Headers AWSAuth::createSigV4HeadersInternal(const String& method, const String& uri,
                                                  const String& host, const String& payloadHash,
                                                  const String& contentType) {
    SigV4Headers headers;
    headers.isValid = false;

    if (!areCredentialsValid()) {
        SDLogger::getInstance().errorf("AWSAuth: Invalid AWS credentials for SigV4 signing");
        return headers;
    }

    // Get current time
    String amzDate = getISOTimestamp();
    String dateStamp = getDateStamp();

    SDLogger::getInstance().debugf("AWSAuth: Creating SigV4 signature for %s %s", method.c_str(), uri.c_str());

    // Split URI into path and query string for canonical request
    String canonicalURI = uri;
    String queryString = "";
    int qPos = uri.indexOf('?');
    if (qPos >= 0) {
        canonicalURI = uri.substring(0, qPos);
        queryString = uri.substring(qPos + 1);
    }

    // Create canonical headers (must be sorted alphabetically by header name)
    String canonicalHeaders = "content-type:" + contentType + "\n" +
                              "host:" + host + "\n" +
                              "x-amz-date:" + amzDate + "\n" +
                              "x-amz-security-token:" + credentials.sessionToken + "\n";
    String signedHeaders = "content-type;host;x-amz-date;x-amz-security-token";

    // Create canonical request
    SDLogger::getInstance().tracef("AWSAuth: canonicalHeaders length: %d", canonicalHeaders.length());

    String canonicalRequest = createCanonicalRequest(method, canonicalURI, queryString, canonicalHeaders,
                                                    signedHeaders, payloadHash);

    SDLogger::getInstance().tracef("AWSAuth: Canonical Request Hash: %s", sha256Hash(canonicalRequest).c_str());

    // Create string to sign
    String algorithm = "AWS4-HMAC-SHA256";
    String credentialScope = dateStamp + "/" + region + "/execute-api/aws4_request";
    String canonicalRequestHash = sha256Hash(canonicalRequest);
    String stringToSign = createStringToSign(algorithm, amzDate, credentialScope,
                                           canonicalRequestHash);

    SDLogger::getInstance().tracef("AWSAuth: String to Sign:\n%s", stringToSign.c_str());

    // Create signing key
    uint8_t signingKey[32];
    getSigningKey(signingKey, credentials.secretAccessKey, dateStamp, region, "execute-api");

    // Create signature
    String signature = createSignature(stringToSign, signingKey);

    // Create authorization header
    String authorizationHeader = algorithm + " " +
                                "Credential=" + credentials.accessKeyId + "/" + credentialScope + ", " +
                                "SignedHeaders=" + signedHeaders + ", " +
                                "Signature=" + signature;

    headers.authorization = authorizationHeader;
    headers.date = amzDate;
    headers.securityToken = credentials.sessionToken;
    headers.contentType = contentType;
    headers.host = host;
    headers.payloadHash = payloadHash;  // Store the payload hash for the HTTP header
    headers.isValid = true;

    SDLogger::getInstance().debugf("AWSAuth: SigV4 headers created successfully");

    return headers;
}

String AWSAuth::createCanonicalRequest(const String& method, const String& uri,
                                      const String& queryString, const String& headers,
                                      const String& signedHeaders, const String& payloadHash) {
    return method + "\n" +
           uri + "\n" +
           queryString + "\n" +
           headers + "\n" +
           signedHeaders + "\n" +
           payloadHash;
}

String AWSAuth::createStringToSign(const String& algorithm, const String& requestDateTime,
                                  const String& credentialScope, const String& canonicalRequestHash) {
    return algorithm + "\n" +
           requestDateTime + "\n" +
           credentialScope + "\n" +
           canonicalRequestHash;
}

String AWSAuth::createSignature(const String& stringToSign, const uint8_t* signingKey) {
    return hmacSha256Hex(signingKey, 32, stringToSign);
}

void AWSAuth::getSigningKey(uint8_t* output, const String& key, const String& dateStamp,
                           const String& regionName, const String& serviceName) {
    // kDate = HMAC("AWS4" + kSecret, Date)
    String kSecret = "AWS4" + key;
    uint8_t kDate[32];
    hmacSha256Raw(kDate, (const uint8_t*)kSecret.c_str(), kSecret.length(),
                  (const uint8_t*)dateStamp.c_str(), dateStamp.length());

    // kRegion = HMAC(kDate, Region)
    uint8_t kRegion[32];
    hmacSha256Raw(kRegion, kDate, 32,
                  (const uint8_t*)regionName.c_str(), regionName.length());

    // kService = HMAC(kRegion, Service)
    uint8_t kService[32];
    hmacSha256Raw(kService, kRegion, 32,
                  (const uint8_t*)serviceName.c_str(), serviceName.length());

    // kSigning = HMAC(kService, "aws4_request")
    const char* aws4Request = "aws4_request";
    hmacSha256Raw(output, kService, 32,
                  (const uint8_t*)aws4Request, strlen(aws4Request));
}

void AWSAuth::hmacSha256Raw(uint8_t* output, const uint8_t* key, size_t keyLen,
                           const uint8_t* data, size_t dataLen) {
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(md_info, key, keyLen, data, dataLen, output);
}

String AWSAuth::hmacSha256Hex(const uint8_t* key, size_t keyLen, const String& data) {
    uint8_t output[32];
    hmacSha256Raw(output, key, keyLen, (const uint8_t*)data.c_str(), data.length());
    return bytesToHex(output, 32);
}

String AWSAuth::sha256Hash(const String& data) {
    uint8_t output[32];
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md(md_info, (const uint8_t*)data.c_str(), data.length(), output);
    return bytesToHex(output, 32);
}

String AWSAuth::sha256HashBinary(const uint8_t* data, size_t len) {
    uint8_t output[32];
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md(md_info, data, len, output);
    return bytesToHex(output, 32);
}

String AWSAuth::bytesToHex(const uint8_t* bytes, size_t len) {
    String result = "";
    result.reserve(len * 2);
    const char hexChars[] = "0123456789abcdef";  // AWS SigV4 requires lowercase hex
    for (size_t i = 0; i < len; i++) {
        result += hexChars[(bytes[i] >> 4) & 0x0F];
        result += hexChars[bytes[i] & 0x0F];
    }
    return result;
}

String AWSAuth::urlEncode(const String& str) {
    String encoded = "";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
            encoded += hex;
        }
    }
    return encoded;
}

String AWSAuth::getISOTimestamp() {
    time_t now;
    time(&now);
    struct tm* timeinfo = gmtime(&now);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", timeinfo);
    return String(buffer);
}

String AWSAuth::getDateStamp() {
    time_t now;
    time(&now);
    struct tm* timeinfo = gmtime(&now);

    char buffer[9];
    strftime(buffer, sizeof(buffer), "%Y%m%d", timeinfo);
    return String(buffer);
}
