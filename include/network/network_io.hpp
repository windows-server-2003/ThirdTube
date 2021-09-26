#pragma once
#include <vector>
#include <map>
#include <string>
#include <3ds.h>
#include <curl/curl.h>

#define NETWORK_FRAMEWORK_HTTPC 0
#define NETWORK_FRAMEWORK_SSLC 1
#define NETWORK_FRAMEWORK_LIBCURL 2

struct NetworkResult {
	httpcContext context;
	std::string redirected_url;
	bool fail = false; // receiving http error code like 404 is still counted as a 'success'
	std::string error;
	int status_code = -1;
	std::string status_message;
	std::vector<u8> data;
	std::map<std::string, std::string> response_headers;
	
	bool status_code_is_success() { return status_code / 100 == 2; }
	std::string get_header(std::string key);
	void finalize();
};

struct NetworkSession {
	bool inited = false;
	bool fail = false;
	int sockfd = -1;
	sslcContext sslc_context;
	std::string host_name;
	
	void open(std::string host_name);
	void close();
};
struct NetworkSessionList { // one instance per thread
private :
	void deinit(); // will be called for each instance when the app exits
public :
	CURL* curl = NULL;
	// should not be used from outside network_io.cpp
	std::map<std::string, NetworkSession> sessions; 
	std::vector<u8> *buffer;
	
	bool inited = false;
	
	// this function does NOT perform any network/socket related operations
	void init();
	void close_sessions();
	
	static void at_exit();
};

NetworkResult Access_http_get(NetworkSessionList &session_list, std::string url, const std::map<std::string, std::string> &request_headers);
NetworkResult Access_http_post(NetworkSessionList &session_list, const std::string &url, const std::map<std::string, std::string> &request_headers,
	const std::string &body);

std::string url_get_host_name(const std::string &url);

#define HTTP_STATUS_CODE_OK 200
#define HTTP_STATUS_CODE_NO_CONTENT 204
#define HTTP_STATUS_CODE_PARTIAL_CONTENT 206
#define HTTP_STATUS_CODE_FORBIDDEN 403
#define HTTP_STATUS_CODE_NOT_FOUND 404
