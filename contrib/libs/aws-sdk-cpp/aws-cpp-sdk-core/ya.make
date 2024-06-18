# Generated by devtools/yamaker.

LIBRARY()

LICENSE(
    Apache-2.0 AND
    MIT AND
    Zlib
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/curl
    contrib/libs/openssl
    contrib/libs/zlib
    contrib/restricted/aws/aws-c-auth
    contrib/restricted/aws/aws-c-cal
    contrib/restricted/aws/aws-c-common
    contrib/restricted/aws/aws-c-event-stream
    contrib/restricted/aws/aws-c-http
    contrib/restricted/aws/aws-c-io
    contrib/restricted/aws/aws-c-mqtt
    contrib/restricted/aws/aws-c-sdkutils
    contrib/restricted/aws/aws-checksums
    contrib/restricted/aws/aws-crt-cpp
)

ADDINCL(
    GLOBAL contrib/libs/aws-sdk-cpp/aws-cpp-sdk-core/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

CFLAGS(
    -DAWS_AUTH_USE_IMPORT_EXPORT
    -DAWS_CAL_USE_IMPORT_EXPORT
    -DAWS_CHECKSUMS_USE_IMPORT_EXPORT
    -DAWS_COMMON_USE_IMPORT_EXPORT
    -DAWS_COMPRESSION_USE_IMPORT_EXPORT
    -DAWS_CRT_CPP_USE_IMPORT_EXPORT
    -DAWS_EVENT_STREAM_USE_IMPORT_EXPORT
    -DAWS_HTTP_USE_IMPORT_EXPORT
    -DAWS_IO_USE_IMPORT_EXPORT
    -DAWS_MQTT_USE_IMPORT_EXPORT
    -DAWS_S3_USE_IMPORT_EXPORT
    -DAWS_SDKUTILS_USE_IMPORT_EXPORT
    -DAWS_SDK_VERSION_MAJOR=1
    -DAWS_SDK_VERSION_MINOR=11
    -DAWS_SDK_VERSION_PATCH=37
    -DAWS_TEST_REGION=US_EAST_1
    -DAWS_USE_EPOLL
    -DCURL_HAS_H2
    -DCURL_HAS_TLS_PROXY
    -DENABLED_REQUEST_COMPRESSION
    -DENABLED_ZLIB_REQUEST_COMPRESSION
    -DENABLE_CURL_CLIENT
    -DENABLE_CURL_LOGGING
    -DENABLE_OPENSSL_ENCRYPTION
    -DHAS_PATHCONF
    -DHAS_UMASK
)

SRCS(
    source/AmazonSerializableWebServiceRequest.cpp
    source/AmazonStreamingWebServiceRequest.cpp
    source/AmazonWebServiceRequest.cpp
    source/Aws.cpp
    source/Globals.cpp
    source/Region.cpp
    source/Version.cpp
    source/auth/AWSCredentialsProvider.cpp
    source/auth/AWSCredentialsProviderChain.cpp
    source/auth/SSOCredentialsProvider.cpp
    source/auth/STSCredentialsProvider.cpp
    source/auth/bearer-token-provider/DefaultBearerTokenProviderChain.cpp
    source/auth/bearer-token-provider/SSOBearerTokenProvider.cpp
    source/auth/signer-provider/BearerTokenAuthSignerProvider.cpp
    source/auth/signer-provider/DefaultAuthSignerProvider.cpp
    source/auth/signer/AWSAuthBearerSigner.cpp
    source/auth/signer/AWSAuthEventStreamV4Signer.cpp
    source/auth/signer/AWSAuthSignerCommon.cpp
    source/auth/signer/AWSAuthSignerHelper.cpp
    source/auth/signer/AWSAuthV4Signer.cpp
    source/auth/signer/AWSNullSigner.cpp
    source/client/AWSClient.cpp
    source/client/AWSErrorMarshaller.cpp
    source/client/AWSJsonClient.cpp
    source/client/AWSUrlPresigner.cpp
    source/client/AWSXmlClient.cpp
    source/client/AdaptiveRetryStrategy.cpp
    source/client/AsyncCallerContext.cpp
    source/client/ClientConfiguration.cpp
    source/client/CoreErrors.cpp
    source/client/DefaultRetryStrategy.cpp
    source/client/GenericClientConfiguration.cpp
    source/client/RequestCompression.cpp
    source/client/RetryStrategy.cpp
    source/client/SpecifiedRetryableErrorsRetryStrategy.cpp
    source/config/AWSConfigFileProfileConfigLoader.cpp
    source/config/AWSProfileConfigLoaderBase.cpp
    source/config/ConfigAndCredentialsCacheManager.cpp
    source/config/EC2InstanceProfileConfigLoader.cpp
    source/config/defaults/ClientConfigurationDefaults.cpp
    source/endpoint/AWSEndpoint.cpp
    source/endpoint/AWSPartitions.cpp
    source/endpoint/BuiltInParameters.cpp
    source/endpoint/ClientContextParameters.cpp
    source/endpoint/DefaultEndpointProvider.cpp
    source/endpoint/EndpointProviderBase.cpp
    source/endpoint/internal/AWSEndpointAttribute.cpp
    source/external/cjson/cJSON.cpp
    source/external/tinyxml2/tinyxml2.cpp
    source/http/HttpClient.cpp
    source/http/HttpClientFactory.cpp
    source/http/HttpRequest.cpp
    source/http/HttpResponse.cpp
    source/http/HttpTypes.cpp
    source/http/Scheme.cpp
    source/http/URI.cpp
    source/http/curl/CurlHandleContainer.cpp
    source/http/curl/CurlHttpClient.cpp
    source/http/standard/StandardHttpRequest.cpp
    source/http/standard/StandardHttpResponse.cpp
    source/internal/AWSHttpResourceClient.cpp
    source/monitoring/DefaultMonitoring.cpp
    source/monitoring/HttpClientMetrics.cpp
    source/monitoring/MonitoringManager.cpp
    source/utils/ARN.cpp
    source/utils/Array.cpp
    source/utils/DNS.cpp
    source/utils/DateTimeCommon.cpp
    source/utils/Directory.cpp
    source/utils/Document.cpp
    source/utils/EnumParseOverflowContainer.cpp
    source/utils/FileSystemUtils.cpp
    source/utils/GetTheLights.cpp
    source/utils/HashingUtils.cpp
    source/utils/StringUtils.cpp
    source/utils/TempFile.cpp
    source/utils/UUID.cpp
    source/utils/base64/Base64.cpp
    source/utils/crypto/CRC32.cpp
    source/utils/crypto/Cipher.cpp
    source/utils/crypto/ContentCryptoMaterial.cpp
    source/utils/crypto/ContentCryptoScheme.cpp
    source/utils/crypto/CryptoBuf.cpp
    source/utils/crypto/CryptoStream.cpp
    source/utils/crypto/EncryptionMaterials.cpp
    source/utils/crypto/KeyWrapAlgorithm.cpp
    source/utils/crypto/MD5.cpp
    source/utils/crypto/Sha1.cpp
    source/utils/crypto/Sha256.cpp
    source/utils/crypto/Sha256HMAC.cpp
    source/utils/crypto/factory/Factories.cpp
    source/utils/crypto/openssl/CryptoImpl.cpp
    source/utils/event/EventDecoderStream.cpp
    source/utils/event/EventEncoderStream.cpp
    source/utils/event/EventHeader.cpp
    source/utils/event/EventMessage.cpp
    source/utils/event/EventStreamBuf.cpp
    source/utils/event/EventStreamDecoder.cpp
    source/utils/event/EventStreamEncoder.cpp
    source/utils/event/EventStreamErrors.cpp
    source/utils/json/JsonSerializer.cpp
    source/utils/logging/AWSLogging.cpp
    source/utils/logging/CRTLogSystem.cpp
    source/utils/logging/CRTLogging.cpp
    source/utils/logging/ConsoleLogSystem.cpp
    source/utils/logging/DefaultLogSystem.cpp
    source/utils/logging/FormattedLogSystem.cpp
    source/utils/logging/LogLevel.cpp
    source/utils/memory/AWSMemory.cpp
    source/utils/memory/stl/SimpleStringStream.cpp
    source/utils/stream/ConcurrentStreamBuf.cpp
    source/utils/stream/PreallocatedStreamBuf.cpp
    source/utils/stream/ResponseStream.cpp
    source/utils/stream/SimpleStreamBuf.cpp
    source/utils/threading/Executor.cpp
    source/utils/threading/ReaderWriterLock.cpp
    source/utils/threading/Semaphore.cpp
    source/utils/threading/ThreadTask.cpp
    source/utils/xml/XmlSerializer.cpp
)

IF (OS_WINDOWS)
    SRCS(
        source/net/windows/Net.cpp
        source/net/windows/SimpleUDP.cpp
        source/platform/windows/Environment.cpp
        source/platform/windows/FileSystem.cpp
        source/platform/windows/OSVersionInfo.cpp
        source/platform/windows/Security.cpp
        source/platform/windows/Time.cpp
    )
ELSE()
    SRCS(
        source/net/linux-shared/Net.cpp
        source/net/linux-shared/SimpleUDP.cpp
        source/platform/linux-shared/Environment.cpp
        source/platform/linux-shared/FileSystem.cpp
        source/platform/linux-shared/OSVersionInfo.cpp
        source/platform/linux-shared/Security.cpp
        source/platform/linux-shared/Time.cpp
    )
ENDIF()

END()
