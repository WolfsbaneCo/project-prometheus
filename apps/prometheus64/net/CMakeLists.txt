# Online play: libcurl (curl licence) for HTTPS and secure WebSockets. Built here as a static
# library, so the app needs no extra DLLs. TLS comes from the system: Windows' own (Schannel) on
# Windows, OpenSSL elsewhere. This directory has its own scope, like ../archives.
include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Only HTTP(S) and WebSockets. (HTTP_ONLY would switch WebSockets off too.)
set(HTTP_ONLY OFF CACHE BOOL "" FORCE)
foreach(proto DICT FILE FTP GOPHER IMAP IPFS LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP)
  set(CURL_DISABLE_${proto} ON CACHE BOOL "" FORCE)
endforeach()
set(CURL_DISABLE_WEBSOCKETS OFF CACHE BOOL "" FORCE)
foreach(opt CURL_USE_LIBPSL CURL_USE_LIBSSH2 CURL_USE_LIBSSH CURL_USE_GSSAPI USE_NGHTTP2 USE_LIBIDN2 CURL_BROTLI
            CURL_ZSTD CURL_ZLIB USE_LIBRTMP CURL_USE_GNUTLS CURL_USE_MBEDTLS CURL_USE_WOLFSSL ENABLE_ARES)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
if(WIN32)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
else()
  set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(p64_curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG curl-8_19_0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(p64_curl)

add_library(p64_curl INTERFACE)
target_link_libraries(p64_curl INTERFACE CURL::libcurl_static)
