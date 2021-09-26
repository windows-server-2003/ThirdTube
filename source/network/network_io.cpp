#include "headers.hpp"
#include "network/network_io.hpp"
#include <cassert>
#include <deque>

#include <fcntl.h>

#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static volatile bool exiting = false;

void NetworkSession::open(std::string host_name) {
	static const int SOCKET_BUFFER_MAX_SIZE = 0x8000;
	
	this->host_name = host_name;
	
	Result ret = 0;
	
	struct addrinfo hints;
	struct addrinfo *resaddr = NULL;
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		Util_log_save("sslc", "Failed to create the socket.");
		goto fail;
	}
	// expand socket buffer size
	setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &SOCKET_BUFFER_MAX_SIZE, sizeof(int));
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	
	// Util_log_save("sslc", "Resolving hostname...");
	
	if (getaddrinfo(host_name.c_str(), "443", &hints, &resaddr) != 0) {
		Util_log_save("sslc", "getaddrinfo() failed.");
		goto fail;
	}
	
	// Util_log_save("sslc", "Connecting to the server...");
	
	struct addrinfo *resaddr_cur;
	for (resaddr_cur = resaddr; resaddr_cur; resaddr_cur = resaddr_cur->ai_next) {
		if (connect(sockfd, resaddr_cur->ai_addr, resaddr_cur->ai_addrlen) == 0) break;
	}
	freeaddrinfo(resaddr);
	
	if (!resaddr_cur) {
		Util_log_save("sslc", "Failed to connect.");
		goto fail;
	}
	
	// Util_log_save("sslc", "Running sslc setup...");
	
	ret = sslcCreateContext(&sslc_context, sockfd, SSLCOPT_DisableVerify, host_name.c_str());
	if (R_FAILED(ret)) {
		Util_log_save("sslc", "sslcCreateContext() failed: ", (unsigned int) ret);
		goto fail;
	}
	
	// set the socket to be nonblocking
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
	
	this->fail = false;
	inited = true;
	return;
	
	fail :
	this->fail = true;
	if (sockfd != -1) closesocket(sockfd);
}
void NetworkSession::close() {
	if (inited) {
		sslcDestroyContext(&sslc_context);
		closesocket(sockfd);
		inited = false;
	}
}

static std::vector<NetworkSessionList *> deinit_list;

void NetworkSessionList::init() {
	buffer = new std::vector<u8> (0x40000); // 256 KiB
	if (!buffer) {
		Util_log_save("sslc", "failed to allocate memory");
		usleep(5000000);
		return;
	}
	inited = true;
	
	deinit_list.push_back(this);
}
void NetworkSessionList::close_sessions() {
	for (auto &session : sessions) session.second.close();
}
void NetworkSessionList::deinit() {
	close_sessions();
	sessions.clear();
	
	delete buffer;
	buffer = NULL;
	
	inited = false;
}
void NetworkSessionList::at_exit() {
	for (auto session_list : deinit_list) session_list->deinit();
	deinit_list.clear();
	exiting = true;
}


/*
	HTTP/1.1 client
*/

std::string url_get_host_name(const std::string &url) {
	auto pos0 = url.find("://");
	if (pos0 == std::string::npos) return "";
	pos0 += 3;
	return std::string(url.begin() + pos0, std::find(url.begin() + pos0, url.end(), '/'));
}
static std::string get_page_url(const std::string &url) {
	auto pos0 = url.find("://");
	if (pos0 == std::string::npos) return "";
	pos0 += 3;
	return std::string(std::find(url.begin() + pos0, url.end(), '/'), url.end());
}
static std::string remove_leading_whitespaces(std::string str) {
	size_t i = 0;
	while (i < str.size() && str[i] == ' ') i++;
	return str.substr(i, str.size() - i);
}

struct ResponseHeader {
	int status_code = -1;
	std::string status_message;
	std::map<std::string, std::string> headers;
};
static ResponseHeader parse_header(std::string header) {
	ResponseHeader res;
	std::vector<std::string> header_lines;
	{
		std::vector<std::string> header_lines_tmp = {""};
		for (auto c : header) {
			if (c == '\n') header_lines_tmp.push_back("");
			else header_lines_tmp.back().push_back(c);
		}	
		for (auto line : header_lines_tmp) {
			if (line.size() && line.back() == '\r') line.pop_back();
			if (!line.size()) continue;
			header_lines.push_back(line);
		}
	}
	// parse status line
	{
		if (!header_lines.size()) {
			Util_log_save("http", "Empty header");
			return res;
		}
		const std::string &status_line = header_lines[0];
		if (status_line.substr(0, 8) != "HTTP/1.1" && status_line.substr(0, 8) != "HTTP/1.0") {
			Util_log_save("http", "Invalid status line : " + status_line);
			return res;
		}
		size_t head = 8;
		while (head < status_line.size() && isspace(status_line[head])) head++;
		char *end;
		res.status_code = strtol(status_line.c_str() + head, &end, 10);
		if (end == status_line.c_str() + head) {
			Util_log_save("http", "failed to parse status code in the status line: " + status_line);
			res.status_code = -1;
			return res;
		}
		head = end - status_line.c_str();
		while (head < status_line.size() && isspace(status_line[head])) head++;
		res.status_message = status_line.c_str() + head;
		header_lines.erase(header_lines.begin());
	}
	for (auto line : header_lines) {
		auto colon = std::find(line.begin(), line.end(), ':');
		if (colon == line.end()) {
			Util_log_save("http", "Header line without a colon, ignoring : " + line);
			continue;
		}
		auto key = remove_leading_whitespaces(std::string(line.begin(), colon));
		auto value = remove_leading_whitespaces(std::string(colon + 1, line.end()));
		res.headers[key] = value;
	}
	return res;
}
struct ChunkProcessor {
	std::string result;
	std::deque<char> buffer;
	int size = -1;
	int size_size = -1;
	bool error = false;
	
	int parse_hex(std::string str) {
		int res = 0;
		for (auto c : str) {
			res *= 16;
			if (isdigit(c)) res += c - '0';
			else if (c >= 'A' && c <= 'F') res += c - 'A' + 10;
			else if (c >= 'a' && c <= 'f') res += c - 'a' + 10;
			else {
				Util_log_save("hex", "unexpected char '" + std::string(1, c) + "' in " + str);
				error = true;
				return -1;
			}
		}
		return res;
	}
	void try_to_parse_size() {
		size_t pos = 0;
		for (; pos + 1 < buffer.size(); pos++) if (buffer[pos] == '\r' && buffer[pos + 1] == '\n') break;
		if (pos + 1 >= buffer.size()) size = size_size = -1;
		else {
			size_size = pos;
			size = parse_hex(std::string(buffer.begin(), buffer.begin() + size_size));
		}
	}
	
	// -1 : error
	// 0 : not the end
	// 1 : end reached
	template<class T> int push(const T &str) {
		buffer.insert(buffer.end(), str.begin(), str.end());
		if (size == -1) {
			try_to_parse_size();
			if (error) return -1;
			if (size == -1) return 0;
		}
		while ((int) buffer.size() >= size_size + 2 + size + 2) {
			{
				auto tmp = std::string(buffer.begin() + size_size + 2 + size, buffer.begin() + size_size + 2 + size + 2);
				if (tmp != "\r\n") {
					Util_log_save("http-chunk", "expected \\r\\n after chunk content end : " + std::to_string(tmp[0]) + " " + std::to_string(tmp[1]));
					return -1;
				}
			}
			result.insert(result.end(), buffer.begin() + size_size + 2, buffer.begin() + size_size + 2 + size);
			buffer.erase(buffer.begin(), buffer.begin() + size_size + 2 + size + 2);
			// Util_log_save("http-chunk", "read chunk size : " + std::to_string(size));
			if (size == 0) {
				if (buffer.size()) {
					Util_log_save("http-chunk", "trailing data : " + std::string(buffer.begin(), buffer.end()));
					return -1;
				}
				return 1;
			}
			try_to_parse_size();
			if (error) return -1;
			if (size == -1) break;
		}
		return 0;
	}
};
static bool perform_http_request(NetworkSession &session, const std::string &request_content, std::vector<u8> &buffer, NetworkResult &result) {
	if (session.fail) return false;
	
	static constexpr int TIMEOUT_MS = 1000 * 15;
	
	{
		Result lictru_res = 0;
		TickCounter clock;
		osTickCounterStart(&clock);
		osTickCounterUpdate(&clock);
		
		// Util_log_save("sslc", "Starting the TLS connection...");
		

		while (!exiting) {
			lictru_res = sslcStartConnection(&session.sslc_context, NULL, NULL);
			if ((unsigned int) lictru_res == 0xD840B807) {
				// Util_log_save("sslc", "sslcStartConnection would block");
				usleep(5000);
				if (osTickCounterRead(&clock) >= TIMEOUT_MS) {
					result.error = "StartConnection Timeout";
					Util_log_save("sslc", "sslcStartConnection timed out");
					goto fail;
				}
			} else if(R_FAILED(lictru_res)) {
				Util_log_save("sslc", "sslcStartConnection() failed: ", (unsigned int) lictru_res);
				goto fail;
			} else break;
		}
		if (exiting) return false;
		
		osTickCounterUpdate(&clock);
		
		size_t total_sent_size = 0;
		while (!exiting && total_sent_size < request_content.size()) {
			lictru_res = sslcWrite(&session.sslc_context, (u8 *) request_content.c_str() + total_sent_size, request_content.size() - total_sent_size);
			if ((u32) lictru_res == 0xD840B803) { // would block
				usleep(3000);
				if (osTickCounterRead(&clock) >= TIMEOUT_MS) {
					result.error = "Write Timeout";
					Util_log_save("sslc", "sslcWrite timed out");
					goto fail;
				}
			} else if(R_FAILED(lictru_res)) goto fail;
			else {
				total_sent_size += lictru_res;
				// Util_log_save("sslc", "=> Send data: " + std::to_string(lictru_res));
				osTickCounterUpdate(&clock);
			}
		}
		if (exiting) return false;
		osTickCounterUpdate(&clock);
		
		
		ResponseHeader response_header;
		std::string content;
		ChunkProcessor chunk_processor;
		int content_length = -1;
		bool header_end_encountered = false;
		while (!exiting) {
			if (content_length != -1 && (int) content.size() >= content_length) break;
			lictru_res = sslcRead(&session.sslc_context, &buffer[0], buffer.size(), false);
			
			if ((u32) lictru_res == 0xD840B802) {
				// Util_log_save("sslc", "would block");
				usleep(3000);
				if (osTickCounterRead(&clock) >= TIMEOUT_MS) {
					result.error = "Read Timeout";
					Util_log_save("sslc", "sslcRead timed out");
					goto fail;
				}
			} else if ((u32) lictru_res == 0xD8A0B805) { // probably session expired
				Util_log_save("sslc", "session expired, reopening...");
				session.close();
				session.open(session.host_name);
				Util_log_save("sslc", "session reopened... fail:" + std::to_string(session.fail));
				if (!session.fail) return perform_http_request(session, request_content, buffer, result);
				else goto fail;
			} else if (R_FAILED(lictru_res)) {
				Util_log_save("sslc", "sslcRead() failed : ", lictru_res);
				goto fail;
			} else {
				osTickCounterUpdate(&clock);
				// Util_log_save("sslc", "<= Recv data: " + std::to_string(lictru_res));
				if (content_length == -1 && header_end_encountered) { // chunked
					int push_res = chunk_processor.push(std::string(buffer.begin(), buffer.begin() + lictru_res));
					if (push_res == -1) {
						Util_log_save("http", "push failed");
						goto fail;
					} else if (push_res == 1) {
						content = std::move(chunk_processor.result);
						content_length = content.size();
						break;
					}
				} else {
					content.insert(content.end(), buffer.begin(), buffer.begin() + lictru_res);
					if (!header_end_encountered) {
						auto end_pos = content.find("\r\n\r\n", std::max<int>((int) content.size() - lictru_res - 3, 0));
						if (end_pos != std::string::npos) {
							// Util_log_save("http", "header end, size: " + std::to_string(end_pos + 4));
							response_header = parse_header(content.substr(0, end_pos + 4));
							header_end_encountered = true;
							if (response_header.headers.count("Content-Length")) {
								content = content.substr(end_pos + 4, content.size());
								// Util_log_save("http", "content length : " + response_header.headers["Content-Length"]);
								content_length = stoll(response_header.headers["Content-Length"]); // TODO : error handling
							} else if (response_header.status_code == HTTP_STATUS_CODE_NO_CONTENT) {
								content = content.substr(end_pos + 4, content.size());
								content_length = 0;
								break;
							} else if (response_header.headers.count("Transfer-Encoding") && response_header.headers["Transfer-Encoding"] == "chunked") {
								// Util_log_save("http-chunk", "start chunk-transfer");
								int push_res = chunk_processor.push(content.substr(end_pos + 4, content.size()));
								if (push_res == -1) {
									Util_log_save("http", "push failed");
									goto fail;
								} else if (push_res == 1) {
									content = chunk_processor.result;
									content_length = content.size();
									break;
								}
							} else {
								Util_log_save("http", "Neither Content-Length nor Transfer-Encoding: chunked is specified");
								goto fail;
							}
						}
					}
				}
			}
		}
		if (exiting) return false;
		
		// for (auto i : response_header.headers) Util_log_save("sslc", "header " + i.first + ": " + i.second);
		// Util_log_save("http", "status code : " + std::to_string(response_header.status_code));
		if (content_length != (int) content.size()) {
			Util_log_save("http", "content size          : ", content_length);
			Util_log_save("http", "content size (actual): ", (unsigned int) content.size());
			goto fail;
		}
		result.data = std::vector<u8> (content.begin(), content.end());
		result.status_code = response_header.status_code;
		result.status_message = response_header.status_message;
		for (auto header : response_header.headers) {
			auto key = header.first;
			for (auto &c : key) c = tolower(c);
			result.response_headers[key] = header.second;
		}
		auto connection_header = result.get_header("Connection");
		for (auto &c : connection_header) c = tolower(c);
		if (connection_header == "close") {
			// Util_log_save("http", "Connection: Close specified, closing...");
			session.close();
		}
		return true;
	}
	
	fail :
	// prevent session resumption 
	session.fail = true;
	return false;
}

static size_t curl_receive_data_callback_func(char *in_ptr, size_t, size_t len, void *user_data) {
	std::vector<u8> *out = (std::vector<u8> *) user_data;
	out->insert(out->end(), in_ptr, in_ptr + len);
	
	// Util_log_save("curl", "received : " + std::to_string(len));
	return len;
}
size_t curl_receive_headers_callback_func(char* in_ptr, size_t, size_t len, void *user_data) {
	std::map<std::string, std::string> *out = (std::map<std::string, std::string> *) user_data;
	
	std::string cur_line = std::string(in_ptr, in_ptr + len);
	if (cur_line.size() && cur_line.back() == '\n') cur_line.pop_back();
	if (cur_line.size() && cur_line.back() == '\r') cur_line.pop_back();
	auto colon = std::find(cur_line.begin(), cur_line.end(), ':');
	if (colon == cur_line.end()) {
		// Util_log_save("curl", "unknown header line : " + cur_line);
	} else {
		std::string header_name = remove_leading_whitespaces(std::string(cur_line.begin(), colon));
		std::string header_content = remove_leading_whitespaces(std::string(colon + 1, cur_line.end()));
		// Util_log_save("curl", "header line : " + header_name + " : " + header_content);
		for (auto &c : header_name) c = tolower(c);
		(*out)[header_name] = header_content;
	}
    return len;
}

static NetworkResult access_http_internal(NetworkSessionList &session_list, const std::string &method, const std::string &url,
	std::map<std::string, std::string> request_headers, const std::string &body) {
	
	NetworkResult res;
	
	if (!session_list.inited) {
		res.fail = true;
		res.error = "invalid session list";
		return res;
	}
	
	if (url.substr(0, 7) != "http://" && url.substr(0, 8) != "https://") {
		res.fail = true;
		res.error = "unknown protocol";
		return res;
	}
	
	static const std::string DEFAULT_USER_AGENT = "Mozilla/5.0 (Linux; Android 11; Pixel 3a) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.101 Mobile Safari/537.36";
	static const std::map<std::string, std::string> default_headers = {
		{"Accept", "*/*"},
		{"Connection", "Keep-Alive"},
		{"User-Agent", DEFAULT_USER_AGENT}
	};
	for (auto header : default_headers) if (!request_headers.count(header.first)) request_headers[header.first] = header.second;
	
	if (var_network_framework == NETWORK_FRAMEWORK_SSLC) {
		auto host_name = url_get_host_name(url);
		request_headers["Host"] = host_name;
		
		NetworkSession &session_using = session_list.sessions[host_name];
		if (!session_using.inited || session_using.fail) {
			// Util_log_save("net-io", "init : " + host_name);
			session_using.close();
			for (int i = 0; i < 3; i++) {
				session_using.open(host_name);
				if (!session_using.inited) {
					Util_log_save("sslc", "retrying to init session : " + std::to_string(i));
					usleep(500000);
				} else break;
			}
			if (!session_using.inited) {
				res.fail = true;
				res.error = "failed to init session for " + host_name;
				return res;
			}
		}
		
		if (method == "POST") request_headers["Content-Length"] = std::to_string(body.size());
		
		auto page_url = get_page_url(url);
		std::string request_content = method + " " + page_url + " HTTP/1.1\r\n";
		for (auto header : request_headers) request_content += header.first + ": " + header.second + "\r\n";
		request_content += "\r\n";
		request_content += body;
		
		if (!perform_http_request(session_using, request_content, *session_list.buffer, res)) res.fail = true;
		if (exiting) {
			res.fail = true;
			res.error = "The app is about to exit";
			return res;
		}
	} else if (var_network_framework == NETWORK_FRAMEWORK_HTTPC) {
		u32 status_code = 0;
		Result libctru_res = 0;
		libctru_res = httpcOpenContext(&res.context, method == "GET" ? HTTPC_METHOD_GET : HTTPC_METHOD_POST, url.c_str(), 0);
		libctru_res = httpcSetSSLOpt(&res.context, SSLCOPT_DisableVerify); // to access https:// websites
		libctru_res = httpcSetKeepAlive(&res.context, HTTPC_KEEPALIVE_ENABLED);
		for (auto i : request_headers) libctru_res = httpcAddRequestHeaderField(&res.context, i.first.c_str(), i.second.c_str());
		
		if (method == "POST") httpcAddPostDataRaw(&res.context, (u32 *) body.c_str(), body.size());

		libctru_res = httpcBeginRequest(&res.context);
		if (libctru_res != 0) {
			res.fail = true;
			res.error = "httpcBeginRequest() failed : " + std::to_string(libctru_res);
			return res;
		}

		libctru_res = httpcGetResponseStatusCode(&res.context, &status_code);
		if (libctru_res != 0) {
			res.fail = true;
			res.error = "httpcGetResponseStatusCode() failed : " + std::to_string(libctru_res);
			return res;
		}
		res.status_code = status_code;
		
		auto &buffer = *session_list.buffer;
		while (1) {
			u32 len_read;
			Result ret = httpcDownloadData(&res.context, &buffer[0], buffer.size(), &len_read);
			res.data.insert(res.data.end(), buffer.begin(), buffer.begin() + len_read);
			if (ret != (s32) HTTPC_RESULTCODE_DOWNLOADPENDING) break;
		}
	} else if (var_network_framework == NETWORK_FRAMEWORK_LIBCURL) {
		CURL *&curl = session_list.curl;
		if (!curl) {
			curl = curl_easy_init();
			curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
			curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
			curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_receive_data_callback_func);
			curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_receive_headers_callback_func);
			// curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
		}
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.data);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.response_headers);
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
		if (method == "POST") curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
		else curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		
		struct curl_slist *request_headers_list = NULL;
		for (auto i : request_headers) request_headers_list = curl_slist_append(request_headers_list, (i.first + ": " + i.second).c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers_list);
		
		CURLcode curl_code = curl_easy_perform(curl);
		if (curl_code == CURLE_OK) {
			long status_code;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
			res.status_code = status_code;
		} else Util_log_save("curl", "deep fail");
		
		curl_slist_free_all(request_headers_list);
	}
	return res;
}
NetworkResult Access_http_get(NetworkSessionList &session_list, std::string url, const std::map<std::string, std::string> &request_headers) {
	NetworkResult result;
	while (1) {
		result = access_http_internal(session_list, "GET", url , request_headers, "");
		if (result.status_code / 100 != 3) {
			result.redirected_url = url;
			return result;
		}
		Util_log_save("http", "redir");
		auto new_url = result.get_header("Location");
		if (var_network_framework == NETWORK_FRAMEWORK_SSLC) {
			auto old_host = url_get_host_name(url);
			auto new_host = url_get_host_name(new_url);
			if (old_host != new_host) {
				session_list.sessions[old_host].close();
				session_list.sessions.erase(old_host);
			}
		}
		url = new_url;
	}
}
NetworkResult Access_http_post(NetworkSessionList &session_list, const std::string &url, const std::map<std::string, std::string> &request_headers,
	const std::string &data) {
	
	auto result = access_http_internal(session_list, "POST", url , request_headers, data);
	result.redirected_url = url;
	return result;
}
void NetworkResult::finalize () {
	if (var_network_framework == NETWORK_FRAMEWORK_HTTPC) httpcCloseContext(&context);
}

std::string NetworkResult::get_header(std::string key) {
	if (var_network_framework == NETWORK_FRAMEWORK_HTTPC) {
		char buffer[0x1000] = { 0 };
		httpcGetResponseHeader(&context, key.c_str(), buffer, sizeof(buffer));
		return buffer;
	} else {
		for (auto &c : key) c = tolower(c);
		return response_headers.count(key) ? response_headers[key] : "";
	}
}



