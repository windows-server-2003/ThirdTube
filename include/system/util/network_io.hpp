#pragma once
#include <vector>
#include <map>

// one url at a time per instance of this class
class NetworkStreamCacherData {
private :
	Handle resource_locker; 
public :
	static constexpr size_t BLOCK_SIZE = 0x20000; // 128 KB
	
	// variables to communicate between the threads, should not be accessed from outside network_io.cpp (because of a conflict between threads or an accidental write to a read-only variable)
	volatile bool error = false;
	volatile bool should_be_running = true;
	volatile size_t length = 0;
	std::string cur_inited_url = "";
	// mutex handling
	void lock();
	void release();
	// anything in this section below here is controlled by resource_locker
	std::map<size_t, std::vector<u8> > downloaded_data;
	volatile size_t start_change_request = (size_t) -1;
	std::string url_change_request = "";
	volatile bool url_change_resolving = false;
	volatile bool unseen_change_request = false;
	volatile bool inited_once = false;
	const char * volatile waiting_status = NULL;
	volatile double download_percent = 0;
	
	// actually public variables
	size_t head = 0; // head as seen from ffmpeg
	
	NetworkStreamCacherData ();
	
	// returns if the stream that is requested last time is available
	// any function that inquires the information of the stream should not be called when this function returns false
	bool latest_inited();
	
	// returns the current url, should only be called when latest_inited is true
	std::string url();
	
	// tell the downloader thread to download a different stream
	void change_url(std::string url);
	
	// if this returned true, a critical error has occured, meaning that other functions don't work until a url change request is made and resolved
	bool has_error();
	
	// returns the length of the stream in bytes
	size_t get_len();
	
	// tell the downloader thread to download the stream starting at position 'start'
	// downloading will continue forward until it reaches the end of the stream or another request_download() call is made
	// there is no queue function : it stops the previous downloading when this function is called
	void set_download_start(size_t start);
	
	// check if the data of the current stream of range [start, start + size) is already downloaded and available
	bool is_data_available(size_t start, size_t size);
	
	// this function must only be called when is_data_available(start, size) returns true
	// returns the data of the stream of range [start, start + size)
	std::vector<u8> get_data(size_t start, size_t size);
	
	// make the thread exit
	void exit();
};

// it's just useful
std::pair<std::string, httpcContext> access_http(std::string url, std::map<std::string, std::string> request_headers);

// 'arg' should be a pointer to a **fresh** NetworkStreamCacherData instance
void network_downloader_thread(void *arg);

