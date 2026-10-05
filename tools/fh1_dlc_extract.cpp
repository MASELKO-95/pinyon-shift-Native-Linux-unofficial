#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>

namespace fs = std::filesystem;
using rex::X_STATUS;

static void Extract(rex::filesystem::Entry* entry, const fs::path& destination) {
  if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
    if (!fs::create_directory(destination)) throw std::runtime_error("duplicate directory");
    for (const auto& child : entry->children()) {
      const auto& name = child->name();
      if (name.empty() || name == "." || name == ".." ||
          name.find_first_of("/\\:") != std::string::npos || name.find('\0') != std::string::npos)
        throw std::runtime_error("invalid package entry name");
      Extract(child.get(), destination / name);
    }
    return;
  }
  if (fs::exists(destination)) throw std::runtime_error("duplicate package entry");
  rex::filesystem::File* raw = nullptr;
  if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &raw) != X_STATUS_SUCCESS)
    throw std::runtime_error("cannot open package entry");
  auto destroy = [](rex::filesystem::File* file) { file->Destroy(); };
  std::unique_ptr<rex::filesystem::File, decltype(destroy)> input(raw, destroy);
  std::ofstream output(destination, std::ios::binary);
  output.exceptions(std::ios::failbit | std::ios::badbit);
  std::vector<uint8_t> buffer(1024 * 1024);
  size_t offset = 0;
  while (offset < entry->size()) {
    size_t got = 0;
    const auto size = std::min(buffer.size(), entry->size() - offset);
    if (input->ReadSync(std::span<uint8_t>(buffer.data(), size), offset, &got) != X_STATUS_SUCCESS ||
        !got || got > size) throw std::runtime_error("truncated package entry");
    output.write(reinterpret_cast<const char*>(buffer.data()), got);
    offset += got;
  }
  output.close();
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "Usage: pinyon_shift_dlc_extract PACKAGE NEW-STAGING-DIRECTORY CONTENT-NAME\n";
    return 2;
  }
  try {
    const fs::path package = argv[1], destination = argv[2];
    const std::string name = argv[3];
    if (name.empty() || name.size() > 40 || name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
      throw std::runtime_error("content name must be a hexadecimal package identifier");
    auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
    if (!header || uint32_t(header->metadata.execution_info.title_id) != 0x4D5309C9 ||
        uint32_t(header->metadata.content_type.get()) != 2)
      throw std::runtime_error("expected an original Forza Horizon DLC package (title 4D5309C9, type 00000002)");
    rex::filesystem::StfsContainerDevice device("", package);
    if (!device.Initialize()) throw std::runtime_error("invalid or unsupported STFS package");
    auto* root = device.ResolvePath("");
    if (!root || !fs::create_directory(destination)) throw std::runtime_error("staging directory must be new");
    Extract(root, destination / "payload");
    rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
    data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
    data.content_type = rex::system::XContentType::kMarketplaceContent;
    data.title_id = 0x4D5309C9;
    data.xuid = 0;
    data.set_file_name(name);
    data.set_display_name(header->metadata.display_name(rex::system::XLanguage::kEnglish));
    uint32_t license_mask = 0;
    for (const auto& license : header->header.licenses)
      if (license.license_flags) license_mask |= license.license_bits;
    std::ofstream metadata(destination / "package.header", std::ios::binary);
    metadata.exceptions(std::ios::failbit | std::ios::badbit);
    metadata.write(reinterpret_cast<const char*>(&data), sizeof(data));
    metadata.write(reinterpret_cast<const char*>(&license_mask), sizeof(license_mask));
    metadata.close();
    std::cout << "Extracted FH1 DLC; original license metadata preserved.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "DLC import failed: " << error.what() << '\n';
    return 1;
  }
}
