#include "linux_http_transport.h"

#include <cstring>

// ---- Share handle lock callbacks (for thread-safe connection/DNS sharing) ----

void LinuxHttpTransport::share_lock_cb(CURL* /*handle*/, curl_lock_data data,
                                        curl_lock_access /*access*/, void* userptr) {
    auto* self = static_cast<LinuxHttpTransport*>(userptr);
    switch (data) {
        case CURL_LOCK_DATA_DNS:
            self->share_dns_mutex_.lock();
            break;
        case CURL_LOCK_DATA_CONNECT:
            self->share_conn_mutex_.lock();
            break;
        case CURL_LOCK_DATA_SSL_SESSION:
            self->share_ssl_mutex_.lock();
            break;
        default:
            break;
    }
}

void LinuxHttpTransport::share_unlock_cb(CURL* /*handle*/, curl_lock_data data,
                                          void* userptr) {
    auto* self = static_cast<LinuxHttpTransport*>(userptr);
    switch (data) {
        case CURL_LOCK_DATA_DNS:
            self->share_dns_mutex_.unlock();
            break;
        case CURL_LOCK_DATA_CONNECT:
            self->share_conn_mutex_.unlock();
            break;
        case CURL_LOCK_DATA_SSL_SESSION:
            self->share_ssl_mutex_.unlock();
            break;
        default:
            break;
    }
}

// ---- Response write callbacks ----

size_t LinuxHttpTransport::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, total);
    return total;
}

size_t LinuxHttpTransport::header_callback(char* ptr, size_t size, size_t nmemb, void* /*userdata*/) {
    // We don't need to parse headers for now, just consume them
    return size * nmemb;
}

size_t LinuxHttpTransport::stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t total = size * nmemb;
    auto* ctx = static_cast<StreamContext*>(userdata);

    if (ctx->aborted || !ctx->on_chunk) {
        return 0;  // Returning 0 tells curl to abort the transfer
    }

    const std::string chunk(ptr, total);
    if (!(*ctx->on_chunk)(chunk)) {
        ctx->aborted = true;
        return 0;  // Signal abort
    }

    return total;
}

// ---- Constructor / Destructor ----

LinuxHttpTransport::LinuxHttpTransport() {
    // Global curl init (safe to call multiple times)
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Create shared handle for connection pooling across threads
    share_handle_ = curl_share_init();
    if (share_handle_) {
        curl_share_setopt(share_handle_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(share_handle_, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
        curl_share_setopt(share_handle_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(share_handle_, CURLSHOPT_LOCKFUNC, share_lock_cb);
        curl_share_setopt(share_handle_, CURLSHOPT_UNLOCKFUNC, share_unlock_cb);
        curl_share_setopt(share_handle_, CURLSHOPT_USERDATA, this);
    }
}

LinuxHttpTransport::~LinuxHttpTransport() {
    if (share_handle_) {
        curl_share_cleanup(share_handle_);
    }
    curl_global_cleanup();
}

// ---- Common CURL configuration ----

void LinuxHttpTransport::configure_handle(CURL* curl, const HttpRequest& request,
                                           struct curl_slist** headers_list) const {
    // URL
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());

    // HTTP method
    if (request.method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (request.method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (request.method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (!request.method.empty()) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }

    // Request body (zero-copy, curl reads from this pointer)
    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
    }

    // Headers
    *headers_list = nullptr;
    for (const auto& [key, value] : request.headers) {
        const std::string header = key + ": " + value;
        *headers_list = curl_slist_append(*headers_list, header.c_str());
    }
    if (*headers_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *headers_list);
    }

    // Timeouts
    const long timeout_s = request.timeout_ms > 0 ? (request.timeout_ms / 1000) : 300;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    // === Performance optimizations ===

    // HTTP/2 over TLS (falls back to HTTP/1.1 if server doesn't support it)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);

    // Connection reuse (keep-alive)
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);

    // TCP_NODELAY — send immediately, don't wait for Nagle buffering
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

    // TCP keepalive — detect dead connections early
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);

    // Share DNS cache + connection pool + SSL sessions across handles
    if (share_handle_) {
        curl_easy_setopt(curl, CURLOPT_SHARE, share_handle_);
    }

    // DNS cache TTL (60 seconds)
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 60L);

    // Max connections to cache per host (for potential parallel requests)
    curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 8L);

    // Follow redirects (some API endpoints redirect)
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);

    // Accept compressed responses (gzip, deflate, br)
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    // Disable signal-based timeout handling (not safe in multi-threaded apps)
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    // Suppress progress meter
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
}

// ---- Synchronous send ----

HttpResponse LinuxHttpTransport::send(const HttpRequest& request) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {0, "", "Failed to initialize curl handle"};
    }

    struct curl_slist* headers_list = nullptr;
    configure_handle(curl, request, &headers_list);

    // Collect response body
    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);

    // Perform request
    const CURLcode res = curl_easy_perform(curl);

    HttpResponse response;
    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);
        response.body = std::move(response_body);
    }

    curl_slist_free_all(headers_list);
    curl_easy_cleanup(curl);
    return response;
}

// ---- Streaming send ----

HttpResponse LinuxHttpTransport::send_streaming(
    const HttpRequest& request,
    std::function<bool(const std::string& chunk)> on_chunk) {

    CURL* curl = curl_easy_init();
    if (!curl) {
        return {0, "", "Failed to initialize curl handle"};
    }

    struct curl_slist* headers_list = nullptr;
    configure_handle(curl, request, &headers_list);

    // Set up streaming callback
    StreamContext ctx;
    ctx.on_chunk = &on_chunk;
    ctx.aborted = false;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);

    // Low-latency streaming: disable output buffering
    // (curl streams data as it arrives, callback fires immediately)

    // Perform request
    const CURLcode res = curl_easy_perform(curl);

    HttpResponse response;
    if (ctx.aborted) {
        // User-initiated abort via on_chunk returning false
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);
    } else if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        response.status_code = static_cast<int>(http_code);
    }

    curl_slist_free_all(headers_list);
    curl_easy_cleanup(curl);
    return response;
}
