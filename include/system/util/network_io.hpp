#pragma once
#include <vector>
#include <map>


class NetworkStreamCacherData {
public :
	static constexpr size_t BLOCK_SIZE = 0x20000; // 128 KB
	
	// variables to communicate between the threads, should not be accessed from outside network_io.cpp
	volatile bool inited = false;
	volatile bool error = false;
	volatile bool should_be_running = true;
	volatile size_t length = 0;
	volatile size_t start_change_request = (size_t) -1;
	Handle start_change_request_mutex; // to lock start_change_request
	std::map<size_t, std::vector<u8> > downloaded_data;
	Handle downloaded_data_mutex; // to lock downloaded_data when the main thread reads it or the downloader thread modifies it
	
	// actually public variables
	const std::string url;
	size_t head = 0; // head as seen from ffmpeg
	
	NetworkStreamCacherData (std::string url);
	
	// check if the basic information of the stream is available (i.e. check if the first http connection to the stream has been completed)
	// all the other functions except exit() must only be called after this function returns true
	bool is_inited();
	
	// check if an error occured during the init
	bool bad();
	
	// check the length of the stream in bytes
	size_t get_length();
	
	// check if the data of the stream of range [start, start + size) is already downloaded and available
	bool is_data_available(size_t start, size_t size);
	
	// tell the downloader thread to download the stream starting at position 'start'
	// downloading will continue forward until it reaches the end of the stream or another request_download() call is made
	// there is no queue function : it stops the previous downloading when this function is called
	void set_download_start(size_t start);
	
	// this function must only be called after is_data_available(start, size) returns true
	// returns the data of the stream of range [start, start + size)
	std::vector<u8> get_data(size_t start, size_t size);
	
	// stop the downloading thread, finalize the http connection, and make the thread exit
	void exit();
};


// 'arg' should be a pointer to a NetworkStreamCacherData instance
void network_downloader_thread(void *arg);

