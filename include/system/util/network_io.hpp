#pragma once
#include <vector>
#include <map>

// one instance per one url (once constructed, the url is not changeable)
struct NetworkStream {
	static constexpr size_t BLOCK_SIZE = 0x20000; // 128 KiB
	static constexpr size_t MAX_CACHE_BLOCKS = 100;
	
	size_t block_num = 0;
	std::string url;
	Handle downloaded_data_lock; // std::map needs locking when searching and inserting at the same time
	std::map<size_t, std::vector<u8> > downloaded_data;
	
	
	// anything above here is not supposed to be used from outside network_io.cpp and network_io.hpp
	size_t len = 0;
	volatile bool suspend_request = false;
	volatile bool quit_request = false;
	volatile bool error = false;
	volatile size_t read_head = 0;
	
	NetworkStream (std::string url, size_t len);
	
	double get_download_percentage();
	std::vector<double> get_download_percentage_list(size_t res_len);
	
	// check if the data of the current stream of range [start, start + size) is already downloaded and available
	bool is_data_available(size_t start, size_t size);
	
	// this function must only be called when is_data_available(start, size) returns true
	// returns the data of the stream of range [start, start + size)
	std::vector<u8> get_data(size_t start, size_t size);
	
	// this function is supposed to be called from NetworkStreamDownloader::*
	void set_data(size_t block, const std::vector<u8> &data);
};


// each instance of this class is paired with one downloader thread
// it owns NetworkStream instances, and the one with the least margin (as in proportion to the length of the entire stream) is the target of next downloading
class NetworkStreamDownloader {
private :
	static constexpr size_t BLOCK_SIZE = NetworkStream::BLOCK_SIZE;
	static constexpr size_t MAX_FORWARD_READ_BLOCKS = 50;
	Handle streams_lock;
	std::vector<NetworkStream *> streams;
	
	bool thread_exit_reqeusted = false;
public :
	NetworkStreamDownloader ();
	
	// returns the stream id of the added stream
	// the pointer must be one that has been new-ed : it will be deleted once quit_request is made
	size_t add_stream(NetworkStream *stream);
	
	void request_thread_exit() { thread_exit_reqeusted = true; }
	void delete_all();
	
	void downloader_thread();
};
// 'arg' should be a pointer to an instance of NetworkStreamDownloader
void network_downloader_thread(void *arg);

// it's just useful
std::pair<std::string, httpcContext> access_http_get(std::string url, std::map<std::string, std::string> request_headers);
std::pair<std::string, httpcContext> access_http_post(std::string url, std::vector<u8> content, std::map<std::string, std::string> request_headers);





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
	volatile bool is_locked = false;
	
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


// 'arg' should be a pointer to a **fresh** NetworkStreamCacherData instance
void network_downloader_thread(void *arg);

