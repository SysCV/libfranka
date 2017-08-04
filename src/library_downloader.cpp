#include "library_downloader.h"

#include <exception>
#include <fstream>
#include <vector>

#include <research_interface/robot/service_types.h>

#include <franka/exception.h>

namespace franka {

LibraryDownloader::LibraryDownloader(Network& network) {
  using research_interface::robot::LoadModelLibrary;

  uint32_t command_id = network.tcpSendRequest<LoadModelLibrary>(
      LoadModelLibrary::Architecture::kX64, LoadModelLibrary::System::kLinux);
  LoadModelLibrary::Response response =
      network.tcpBlockingReceiveResponse<LoadModelLibrary>(command_id);
  if (response.status != LoadModelLibrary::Status::kSuccess) {
    throw ModelException("libfranka: Server reports error when loading model library.");
  }

  try {
    std::vector<uint8_t> buffer(response.size);
    network.tcpReceiveIntoBuffer(buffer.data(), static_cast<int>(buffer.size()));
    std::ofstream model_library_stream(path().c_str(), std::ios_base::out | std::ios_base::binary);
    model_library_stream.write(reinterpret_cast<char*>(buffer.data()), buffer.size());
  } catch (const std::exception& ex) {
    throw ModelException("libfranka: Cannot save model library.");
  }
}

const std::string& LibraryDownloader::path() const noexcept {
  return model_library_file_.path();
}

};  // namespace franka
