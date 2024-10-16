#include "TrackFile.h"

#include "UnixIO.h" // to invoke posix read
#include "Config.h"
#include "Connection.h"
#include "ConnectionPool.h"
#include "FileCacheRegister.h"
#include "Message.h"
#include "Request.h"
#include "Timer.h"
#include "UnixIO.h"
#include "lz4.h"
#include "xxhash.h"

#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string.h>
#include <string>
#include <tuple>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <cassert>
#include <functional>
#include <chrono>

#include <json.hpp> // for logging json file
#include <limits.h> // for HOST_NAME_MAX

using namespace std::chrono;
#ifdef LIBDEBUG
#define DPRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define BLK_SIZE 4096 // 4KB 8KB

// #define GATHERSTAT 1
#define USE_HASH 1
// #define ENABLE_TRACE 1
// #define WRITE_STAT_EACH 1


TrackFile::TrackFile(std::string name, int fd, bool openFile) : 
  MonitorFile(MonitorFile::Type::TrackLocal, name, name, fd),
  _fileSize(0),
  _numBlks(0),
  _fd_orig(fd),
  _filename(name),
  total_time_spent_read(0),
  total_time_spent_write(0)

{ 
  DPRINTF("In Trackfile constructor openfile bool: %d\n", openFile);
  _blkSize = Config::blockSizeForStat;
  open();
  _active.store(true);
}

TrackFile::~TrackFile() {
    *this << "Destroying file " << _metaName << std::endl;
    // close();
}

void TrackFile::open() {
  DPRINTF("[MONITOR] TrackFile open: %s\n", _name.c_str()) ;
  // #if 0
  // _closed = false;

#ifdef BLK_IDX
  // trace_read_blk_order.emplace(_name, std::vector<TraceData>());
  // trace_write_blk_order.emplace(_name, std::vector<TraceData>());
  trace_read_blk_order.emplace(_name, TraceData());
  trace_write_blk_order.emplace(_name, TraceData());
#endif

#ifdef GATHERSTAT
  if (track_file_blk_r_stat.find(_name) == track_file_blk_r_stat.end()) {
    track_file_blk_r_stat.insert(std::make_pair(_name, 
						std::map<int, 
						std::atomic<int64_t> >()));
  }
  if (track_file_blk_r_stat_size.find(_name) == track_file_blk_r_stat_size.end()) {
    track_file_blk_r_stat_size.insert(std::make_pair(_name, 
						std::map<int, 
						std::atomic<int64_t> >()));
  }
  if (track_file_blk_w_stat.find(_name) == track_file_blk_w_stat.end()) {
    track_file_blk_w_stat.insert(std::make_pair(_name, 
						std::map<int, 
						std::atomic<int64_t> >()));
  }

  if (track_file_blk_w_stat_size.find(_name) == track_file_blk_w_stat_size.end()) {
    track_file_blk_w_stat_size.insert(std::make_pair(_name, 
						std::map<int, 
						std::atomic<int64_t> >()));
  }

  if (trace_read_blk_seq.find(_name) == trace_read_blk_seq.end()) {
    trace_read_blk_seq.insert(std::make_pair(_name, std::vector<int>()));
  }

  if (trace_write_blk_seq.find(_name) == trace_write_blk_seq.end()) {
    trace_write_blk_seq.insert(std::make_pair(_name, std::vector<int>()));
  }
#endif

  open_file_start_time = high_resolution_clock::now();

  // #endif
  DPRINTF("Returning from trackfile open\n");

}

ssize_t TrackFile::read(void *buf, size_t count, uint32_t index) {
  DPRINTF("In trackfile read count %u \n", count); // read start time
  auto read_file_start_time = high_resolution_clock::now();
  unixread_t unixRead = (unixread_t)dlsym(RTLD_NEXT, "read");
  auto bytes_read = (*unixRead)(_fd_orig, buf, count);
  auto read_file_end_time = high_resolution_clock::now();
  auto duration = duration_cast<seconds>(read_file_end_time - read_file_start_time); 
  total_time_spent_read += duration;
  // DPRINTF("bytes_read: %ld _fd_orig: %d _name: %s \n", bytes_read, _fd_orig, _name.c_str());

#ifdef BLK_IDX
    auto start_block = _filePos[index] / BLK_SIZE;
    auto end_block = (_filePos[index] + count) / BLK_SIZE;
    auto& trace_vector = trace_read_blk_order[_name];
    trace_vector.push_back(start_block);
    trace_vector.push_back(end_block);

#else
    auto start_block = _filePos[index] / BLK_SIZE;
    auto end_block = (_filePos[index] + count) / BLK_SIZE;

    if (first_access_block == -1){
        first_access_block = start_block;
    } 

    // Retrieve the trace vector for the current file
    auto& trace_vector = trace_read_blk_order[_name];

    // Check if trace_vector has at least 3 elements
    if (trace_vector.size() < 3) {
        // Initialize vector if it does not have enough elements
        trace_vector.resize(3, -2); // Default to -2
    }

    // Update the first and second values in trace_vector
    trace_vector[0] = start_block;
    trace_vector[1] = end_block;

    // Determine the status (-1 or -2)
    if (prev_start_block != -1 && prev_end_block != -1 && !has_been_random) {
        if (start_block >= prev_end_block) {
            // Sequential: store -1
            trace_vector[2] = -1;
            // Update previous blocks
            prev_start_block = start_block;
            prev_end_block = end_block;
        } else {
            // Random: store -2 and flag to stop further checks
            trace_vector[2] = -2;
            has_been_random = true;
        }
    } else {
        // No previous blocks to compare or already flagged as random
        if (prev_start_block == -1 && prev_end_block == -1) {
            // First block
            trace_vector[2] = -1;
            prev_start_block = start_block;
            prev_end_block = end_block;
        } else {
            // No valid previous blocks for comparison
            trace_vector[2] = -2;
        }
    }
#endif

#ifdef GATHERSTAT
  if (bytes_read != -1) { // Only update stats if nonzero byte counts are read
    auto blockSizeForStat = Config::blockSizeForStat;
    auto diff = _filePos[index]; //- _filePos[0]; // technically index is always equal to 0 for us, assuming there is only one fp for a file open at a time.
    auto precNumBlocks = diff / blockSizeForStat;
    uint32_t startBlockForStat = precNumBlocks; 
    uint32_t endBlockForStat = (diff + bytes_read) / blockSizeForStat;
  
    if (((diff + bytes_read) % blockSizeForStat)) {
      endBlockForStat++;
    }
    DPRINTF("bytes_read: %d; startBlockForStat: %d; endBlockForStat: %d; blockSizeForStat: %d", bytes_read, startBlockForStat, endBlockForStat, blockSizeForStat);

    for (auto i = startBlockForStat; i <= endBlockForStat; i++) {
      auto index = i;
#ifdef USE_HASH
      auto sample = hashed(index) % Config::hashtableSizeForStat;
      if (sample < Config::hashtableSizeForStat/2) { // Sample only 50%
	// index = sample;
#endif      
	if (track_file_blk_r_stat[_name].find(index) == 
	    track_file_blk_r_stat[_name].end()) {
	  track_file_blk_r_stat[_name].insert(std::make_pair(index, 1));
	//track_file_blk_r_stat_size[_name].insert(std::make_pair(i, bytes_read - ((i - startBlockForStat) * blockSizeForStat)));
	  track_file_blk_r_stat_size[_name].insert(std::make_pair(index, bytes_read));
	} else {
	  track_file_blk_r_stat[_name][index]++;
	}
	// For tracing order
	trace_read_blk_seq[_name].push_back(index);

#ifdef USE_HASH    
      } else {
	continue;
      }
#endif
    }
  }
#endif
  if (bytes_read != -1) {
    DPRINTF("Successfully read the TrackFile\n");
    _filePos[index] += bytes_read;
  }
#ifdef WRITE_STAT_EACH
  close();
#endif
  return bytes_read; // // read end time
  //  return 0;
}

ssize_t TrackFile::write(const void *buf, size_t count, uint32_t index) {
  DPRINTF("In trackfile write count %u \n", count); // read start time
  auto write_file_start_time = high_resolution_clock::now();
  unixwrite_t unixWrite = (unixwrite_t)dlsym(RTLD_NEXT, "write");
  DPRINTF("About to write %u count to file with fd %d and file_name: %s\n", 
	  count, _fd_orig, _filename.c_str());
  auto bytes_written = (*unixWrite)(_fd_orig, buf, count);
  // DPRINTF("bytes_written: %ld _fd_orig: %d _name: %s \n", bytes_written, _fd_orig, _name.c_str());
  auto write_file_end_time = high_resolution_clock::now();
  auto duration = duration_cast<seconds>(write_file_end_time - write_file_start_time);
  total_time_spent_write += duration;

#ifdef BLK_IDX
    auto start_block = _filePos[index] / BLK_SIZE;
    auto end_block = (_filePos[index] + count) / BLK_SIZE;
    auto& trace_vector = trace_write_blk_order[_name];
    trace_vector.push_back(start_block);
    trace_vector.push_back(end_block);

#else

  auto start_block = _filePos[index] / BLK_SIZE;
  auto end_block = (_filePos[index] + count) / BLK_SIZE;

  if (first_access_block == -1){
    first_access_block = start_block;
  } 

  // Retrieve the trace vector for the current file
  auto& trace_vector = trace_write_blk_order[_name];

  // Check if trace_vector has at least 3 elements
  if (trace_vector.size() < 3) {
      // Initialize vector if it does not have enough elements
      trace_vector.resize(3, -2); // Default to -2
  }

  // Update the first and second values in trace_vector
  trace_vector[0] = start_block;
  trace_vector[1] = end_block;

  // Determine the status (-1 or -2)
  if (prev_start_block != -1 && prev_end_block != -1 && !has_been_random) {
      if (start_block >= prev_end_block) {
          // Sequential: store -1
          trace_vector[2] = -1;
          // Update previous blocks
          prev_start_block = start_block;
          prev_end_block = end_block;
      } else {
          // Random: store -2 and flag to stop further checks
          trace_vector[2] = -2;
          has_been_random = true;
      }
  } else {
      // No previous blocks to compare or already flagged as random
      if (prev_start_block == -1 && prev_end_block == -1) {
          // First block
          trace_vector[2] = -1;
          prev_start_block = start_block;
          prev_end_block = end_block;
      } else {
          // No valid previous blocks for comparison
          trace_vector[2] = -2;
      }
  }
#endif

#ifdef GATHERSTAT
  if (bytes_written != -1) {
    auto diff = _filePos[index]; //  - _filePos[0];
    auto precNumBlocks = diff / _blkSize;
    uint32_t startBlockForStat = precNumBlocks; 
    uint32_t endBlockForStat = (diff + bytes_written) / _blkSize; 
    if (((diff + bytes_written) % _blkSize)) {
      endBlockForStat++;
    }

    // DPRINTF("w startBlockForStat: %d endBlockForStat: %d \n", startBlockForStat, endBlockForStat);
    for (auto i = startBlockForStat; i <= endBlockForStat; i++) {
      auto index = i;
#ifdef USE_HASH
      auto sample = hashed(index) % Config::hashtableSizeForStat;
      if (sample < Config::hashtableSizeForStat/2) { // Sample only 50%
	// index = sample;
#endif      
	if (track_file_blk_w_stat[_name].find(index) == track_file_blk_w_stat[_name].end()) {
	  // DPRINTF("%s: 1 \n",_name.c_str());
	  track_file_blk_w_stat[_name].insert(std::make_pair(index, 1)); // not thread-safe
	track_file_blk_w_stat_size[_name].insert(std::make_pair(i, bytes_written)); // not thread-safe
        }
        else {
	  // DPRINTF("%s: 2 \n",_name.c_str());
	  track_file_blk_w_stat[_name][index]++;
        }

	trace_write_blk_seq[_name].push_back(index);

#ifdef USE_HASH    
      } else {
	continue;
      }
#endif
    }
  }
#endif
  if (bytes_written != -1) {
    // DPRINTF("Successfully wrote to the TrackFile\n");
    _filePos[index] += bytes_written;
    // _fileSize += bytes_written;  
  }
  return bytes_written; // read end time
}

int TrackFile::vfprintf(unsigned int pos, int count) {
  DPRINTF("In trackfile vfprintf\n");
#ifdef GATHERSTAT
  if (count != -1) {
    auto diff = _filePos[pos]; //  - _filePos[0];
    auto precNumBlocks = diff / _blkSize;
    uint32_t startBlockForStat = precNumBlocks; 
    uint32_t endBlockForStat = (diff + count) / _blkSize; 
    if (((diff + count) % _blkSize)) {
      endBlockForStat++;
    }

    // DPRINTF("w startBlockForStat: %d endBlockForStat: %d \n", startBlockForStat, endBlockForStat);
    for (auto i = startBlockForStat; i <= endBlockForStat; i++) {
      auto index = i;
      if (track_file_blk_w_stat[_name].find(index) == track_file_blk_w_stat[_name].end()) {
	track_file_blk_w_stat[_name].insert(std::make_pair(index, 1)); // not thread-safe
	track_file_blk_w_stat_size[_name].insert(std::make_pair(i, count)); // not thread-safe
      }
      else {
	// DPRINTF("%s: 2 \n",_name.c_str());
	track_file_blk_w_stat[_name][index]++;
      }

      trace_write_blk_seq[_name].push_back(index);
    }
  }

  if (count != -1) {
    DPRINTF("Successfully wrote to the TrackFile\n");
    _filePos[pos] += count;
    // _fileSize += bytes_written;  
  }
#endif
  return count;
}

uint64_t TrackFile::fileSize() {
        return _fileSize;
}

off_t TrackFile::seek(off_t offset, int whence, uint32_t index) {
  struct stat sb;
  fstat(_fd_orig, &sb);
  auto _fileSize = sb.st_size;

  switch (whence) {
  case SEEK_SET:
    _filePos[index] = offset;
    break;
  case SEEK_CUR:
    _filePos[index] += offset;
    if (_filePos[index] > _fileSize) {
      _filePos[index] = _fileSize;
    }
    break;
  case SEEK_END:
    _filePos[index] = _fileSize + offset;
    break;
  }
  // _eof[index] = false;


  DPRINTF("Calling Seek in Trackfile\n");
  unixlseek_t unixLseek = (unixlseek_t)dlsym(RTLD_NEXT, "lseek");
  auto offset_loc = (*unixLseek)(_fd_orig, offset, whence);
  return  offset_loc; 
}

void write_trace_data(const std::string& filename, TraceData& blk_trace_info, const std::string& pid) {
    if (blk_trace_info.empty()) {
        return;  // Do nothing if blk_trace_info is empty
    }

    // Create JSON object
    nlohmann::json jsonOutput;

#ifdef BLK_IDX
    jsonOutput["io_blk_range"] = blk_trace_info;
#else
    //TODO: modify the first index of blk_trace_info to first_access_block
    // Ensure blk_trace_info has at least one element before modifying
    if (!blk_trace_info.empty()) {
        blk_trace_info[0] = first_access_block;
    }
    jsonOutput["io_blk_range"] = blk_trace_info;
#endif

    // Clear the vector after creating JSON object
    blk_trace_info.clear();

    // Write the JSON object to the file
    std::ofstream file(filename, std::ios::out | std::ios::trunc); // Use trunc to overwrite the file
    if (!file) {
        std::cerr << "File for trace stat collection not created!" << std::endl;
        return;
    }
    file << jsonOutput.dump(4); // Pretty print with an indent of 4 spaces
    file.close();
}

void TrackFile::close() {
  // #if 0  
  DPRINTF("Calling TrackFile close \n");
  // if (!_closed) {
  // unixclose_t unixClose = (unixclose_t)dlsym(RTLD_NEXT, "close");
  // auto close_success = (*unixClose)(_fd_orig);
  // if (close_success) {
  //   // _closed = true;
  //   DPRINTF("Closed file with fd %d with name %s successfully\n", _fd_orig, _name.c_str());
  // }
    // }

    // Get the current host name
    // Get the current host name
    char hostname[256]; // Buffer to store the host name
    std::string host_name = (gethostname(hostname, sizeof(hostname)) == 0) ? hostname : "unknown_host";
    auto pid = std::to_string(getpid());
  
    close_file_end_time = high_resolution_clock::now();
    auto elapsed_time = duration_cast<seconds>(close_file_end_time - open_file_start_time);

    DPRINTF("Writing r blk access order stat\n");
    // std::string file_name_trace_r = _filename + "_" + pid + "_r_blk_trace";
    std::string file_name_trace_r = _filename + "." + pid + "-" + host_name + ".r_blk_trace.json";
    auto& blk_trace_info_r = trace_read_blk_order[_filename];
    auto future_r = std::async(std::launch::async, write_trace_data, file_name_trace_r, std::ref(blk_trace_info_r), pid);

    DPRINTF("Writing w blk access order stat\n");
    // std::string file_name_trace_w = _filename + "_" + pid + "_w_blk_trace";
    std::string file_name_trace_w = _filename + "." + pid + "-" + host_name + ".w_blk_trace.json";
    auto& blk_trace_info_w = trace_write_blk_order[_filename];
    auto future_w = std::async(std::launch::async, write_trace_data, file_name_trace_w, std::ref(blk_trace_info_w), pid);

    // Wait for both async tasks to complete
    future_r.get();
    future_w.get();



#ifdef GATHERSTAT
   // write blk access stat in a file
  DPRINTF("Writing r blk access stat\n");
  std::fstream current_file_stat_r;
  std::string file_name_r = _filename;
  file_name_r.append("_");
  file_name_r.append(pid);
  auto file_stat_r = file_name_r.append("_r_stat");
  current_file_stat_r.open(file_stat_r, std::ios::out | std::ios::app);
  if (!current_file_stat_r) { DPRINTF("File for read stat collection not created!");}
  current_file_stat_r << _filename << " " << "Block no." << " " << "Frequency" << " " 
		      << "Access size in byte" << std::endl;

  auto sum_weight_r = 0; // TODO: Fix FPE
  auto cumulative_weighted_sum_r = 0;
  for (auto& blk_info  : track_file_blk_r_stat[_name]) {
    cumulative_weighted_sum_r += blk_info.second * track_file_blk_r_stat_size[_name][blk_info.first];
    sum_weight_r += blk_info.second;
    current_file_stat_r << blk_info.first << " " << blk_info.second << " " 
			<< track_file_blk_r_stat_size[_name][blk_info.first] << std::endl;
  }

  if (sum_weight_r !=0 ) {
    auto read_io_rate = cumulative_weighted_sum_r / sum_weight_r; // per byte
    auto read_request_rate = elapsed_time / sum_weight_r;
  }
  DPRINTF("Writing w blk access stat\n");
  // write blk access stat in a file
  std::fstream current_file_stat_w;
  std::string file_name_w = _filename;
  file_name_w.append("_");
  file_name_w.append(pid);
  auto file_stat_w = file_name_w.append("_w_stat");
  current_file_stat_w.open(file_stat_w, std::ios::out | std::ios::app);
  if (!current_file_stat_w) {DPRINTF("File for write stat collection not created!");}
  current_file_stat_w << _filename << " " << "Block no." << " " << "Frequency" << " " 
		      << "Access size in byte" << std::endl;
  
  auto sum_weight_w = 0; // TODO: Fix FPE
  auto cumulative_weighted_sum_w = 0;
  for (auto& blk_info  : track_file_blk_w_stat[_name]) {
    cumulative_weighted_sum_w += blk_info.second * track_file_blk_w_stat_size[_name][blk_info.first];
    sum_weight_w += blk_info.second;
    current_file_stat_w << blk_info.first << " " << blk_info.second << " " << track_file_blk_w_stat_size[_name][blk_info.first] << std::endl;
    //current_file_stat_w << std::get<0>(blk_info) << " " << std::get<1>(blk_info) << " " << std::get<2>(blk_info) << std::endl;
  }

  if (sum_weight_w !=0 ) {
    auto write_io_rate = cumulative_weighted_sum_w / sum_weight_w; 
    auto write_request_rate =  elapsed_time / sum_weight_w;
  // TODO: fix below: elapsed_time  // if (elapsed_time == 0) {elapsed_time = 1;}  
  //  auto io_intensity = (total_time_spent_read + total_time_spent_write)/elapsed_time;
  }
  DPRINTF("Writing r blk access order stat\n");
  // write blk access stat in a file
  std::fstream current_file_trace_r;
  std::string file_name_trace_r = _filename;
  file_name_trace_r.append("_");
  file_name_trace_r.append(pid);
  auto file_trace_stat_r = file_name_trace_r.append("_r_trace_stat");
  current_file_trace_r.open(file_trace_stat_r, std::ios::out | std::ios::app);
  if (!current_file_trace_r) {DPRINTF("File for read trace stat collection not created!");}
  auto const& blk_trace_info_r  = trace_read_blk_seq[_name]; 
  for (auto const& blk_: blk_trace_info_r) {
    current_file_trace_r << blk_ << std::endl;
  }
  

  DPRINTF("Writing w blk access order stat\n");
  // write blk access stat in a file
  std::fstream current_file_trace_w;
  std::string file_name_trace_w = _filename;
  file_name_trace_w.append("_");
  file_name_trace_w.append(pid);
  auto file_trace_stat_w = file_name_trace_w.append("_w_trace_stat");
  current_file_trace_w.open(file_trace_stat_w, std::ios::out | std::ios::app);
  if (!current_file_trace_w) {DPRINTF("File for write trace stat collection not created!");}
  // current_file_trace_w << _filename << " " << "Block no." << " " << "Frequency" << std::endl;
  auto const& blk_trace_info_w = trace_write_blk_seq[_name];
  for (auto const& blk_: blk_trace_info_w) {
    current_file_trace_w << blk_ << std::endl;
  }
  // #endif
  #endif
}