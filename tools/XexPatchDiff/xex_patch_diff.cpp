#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/system/export_resolver.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xex_module.h>

namespace fs = std::filesystem;

namespace {

struct ByteRange {
  uint32_t offset = 0;
  uint32_t size = 0;
};

struct SectionInfo {
  std::string name;
  uint32_t virtual_address = 0;
  uint32_t virtual_size = 0;
  bool executable = false;
  bool writable = false;
};

uint16_t ReadLe16(std::span<const uint8_t> data, size_t offset) {
  if (offset + 2 > data.size()) {
    throw std::runtime_error("Unexpected end of PE data.");
  }
  return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadLe32(std::span<const uint8_t> data, size_t offset) {
  if (offset + 4 > data.size()) {
    throw std::runtime_error("Unexpected end of PE data.");
  }
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) |
         (static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::string Hex(uint64_t value, int width = 8) {
  std::ostringstream ss;
  ss << "0x" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
  return ss.str();
}

std::vector<uint8_t> ReadFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open input: " + path.string());
  }

  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!data.empty()) {
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
      throw std::runtime_error("Failed to read input: " + path.string());
    }
  }
  return data;
}

void WriteFile(const fs::path& path, std::span<const uint8_t> data) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open output: " + path.string());
  }
  if (!data.empty()) {
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
}

std::vector<ByteRange> DiffRanges(std::span<const uint8_t> old_data,
                                  std::span<const uint8_t> new_data) {
  const size_t common_size = std::min(old_data.size(), new_data.size());
  const size_t max_size = std::max(old_data.size(), new_data.size());
  std::vector<ByteRange> ranges;

  size_t i = 0;
  while (i < common_size) {
    if (old_data[i] == new_data[i]) {
      ++i;
      continue;
    }

    const size_t start = i;
    while (i < common_size && old_data[i] != new_data[i]) {
      ++i;
    }
    ranges.push_back({static_cast<uint32_t>(start), static_cast<uint32_t>(i - start)});
  }

  if (common_size < max_size) {
    ranges.push_back(
        {static_cast<uint32_t>(common_size), static_cast<uint32_t>(max_size - common_size)});
  }

  return ranges;
}

size_t SumBytes(const std::vector<ByteRange>& ranges) {
  size_t total = 0;
  for (const auto& range : ranges) {
    total += range.size;
  }
  return total;
}

void WriteRangeCsv(const fs::path& path, std::span<const ByteRange> ranges,
                   uint32_t image_base) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path);
  if (!file) {
    throw std::runtime_error("Failed to open output: " + path.string());
  }

  file << "offset,guest_address,size,end_offset,end_guest_address\n";
  for (const auto& range : ranges) {
    file << Hex(range.offset) << ',' << Hex(image_base + range.offset) << ','
         << range.size << ',' << Hex(range.offset + range.size) << ','
         << Hex(image_base + range.offset + range.size) << '\n';
  }
}

std::vector<ByteRange> IntersectRangesWithSections(std::span<const ByteRange> ranges,
                                                   std::span<const SectionInfo> sections,
                                                   uint32_t image_base,
                                                   bool executable_only) {
  std::vector<ByteRange> result;
  for (const auto& range : ranges) {
    const uint32_t range_begin = image_base + range.offset;
    const uint32_t range_end = range_begin + range.size;

    for (const auto& section : sections) {
      if (executable_only && !section.executable) {
        continue;
      }

      const uint32_t section_begin = section.virtual_address;
      const uint32_t section_end = section.virtual_address + section.virtual_size;
      const uint32_t begin = std::max(range_begin, section_begin);
      const uint32_t end = std::min(range_end, section_end);
      if (begin < end) {
        result.push_back({begin - image_base, end - begin});
      }
    }
  }
  return result;
}

std::vector<SectionInfo> ParsePeSections(std::span<const uint8_t> image, uint32_t image_base) {
  if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') {
    throw std::runtime_error("Patched image does not start with an MZ header.");
  }

  const uint32_t pe_offset = ReadLe32(image, 0x3C);
  if (pe_offset + 0x18 > image.size() || image[pe_offset] != 'P' || image[pe_offset + 1] != 'E' ||
      image[pe_offset + 2] != 0 || image[pe_offset + 3] != 0) {
    throw std::runtime_error("Patched image does not contain a valid PE header.");
  }

  const size_t file_header = pe_offset + 4;
  const uint16_t number_of_sections = ReadLe16(image, file_header + 2);
  const uint16_t optional_header_size = ReadLe16(image, file_header + 16);
  const size_t section_table = file_header + 20 + optional_header_size;
  if (section_table + (static_cast<size_t>(number_of_sections) * 40) > image.size()) {
    throw std::runtime_error("PE section table extends outside patched image.");
  }

  std::vector<SectionInfo> sections;
  sections.reserve(number_of_sections);
  for (uint16_t i = 0; i < number_of_sections; ++i) {
    const size_t offset = section_table + (static_cast<size_t>(i) * 40);
    const auto* name_start = reinterpret_cast<const char*>(image.data() + offset);
    size_t name_len = 0;
    while (name_len < 8 && name_start[name_len] != '\0') {
      ++name_len;
    }

    const uint32_t virtual_size = ReadLe32(image, offset + 8);
    const uint32_t virtual_address = ReadLe32(image, offset + 12);
    const uint32_t characteristics = ReadLe32(image, offset + 36);
    sections.push_back({
        std::string(name_start, name_len),
        image_base + virtual_address,
        virtual_size,
        (characteristics & 0x20000000u) != 0,
        (characteristics & 0x80000000u) != 0,
    });
  }

  return sections;
}

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " <base.xex> <patch.xexp|dllp> <out_dir>\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    PrintUsage(argv[0]);
    return 2;
  }

  try {
    rex::InitLogging(nullptr, spdlog::level::warn);

    const fs::path base_path = fs::absolute(argv[1]);
    const fs::path patch_path = fs::absolute(argv[2]);
    const fs::path out_dir = fs::absolute(argv[3]);

    const auto base_data = ReadFile(base_path);
    const auto patch_data = ReadFile(patch_path);

    rex::memory::Memory memory;
    if (!memory.Initialize()) {
      throw std::runtime_error("Failed to initialize ReX memory.");
    }

    rex::runtime::ExportResolver export_resolver;
    rex::runtime::FunctionDispatcher dispatcher(&memory, &export_resolver);

    rex::runtime::XexModule base_module(&dispatcher, nullptr);
    rex::runtime::XexModule patch_module(&dispatcher, nullptr);

    if (!base_module.Load(base_path.filename().string(), base_path.string(), base_data.data(),
                          base_data.size())) {
      throw std::runtime_error("Failed to load base XEX.");
    }

    const uint32_t base_address = base_module.base_address();
    const uint32_t original_image_size = base_module.image_size();
    std::vector<uint8_t> original_image(original_image_size);
    std::memcpy(original_image.data(), memory.TranslateVirtual(base_address), original_image.size());

    if (!patch_module.Load(patch_path.filename().string(), patch_path.string(), patch_data.data(),
                           patch_data.size())) {
      throw std::runtime_error("Failed to load XEX patch.");
    }

    const int patch_result = patch_module.ApplyPatch(&base_module);
    if (patch_result != 0) {
      throw std::runtime_error("XEX patch application failed with code " +
                               std::to_string(patch_result) + ".");
    }

    const uint32_t patched_image_size = base_module.image_size();
    std::vector<uint8_t> patched_image(patched_image_size);
    std::memcpy(patched_image.data(), memory.TranslateVirtual(base_address), patched_image.size());

    const auto all_ranges = DiffRanges(original_image, patched_image);
    const auto sections = ParsePeSections(patched_image, base_address);
    const auto executable_ranges = IntersectRangesWithSections(all_ranges, sections, base_address, true);

    fs::create_directories(out_dir);
    WriteFile(out_dir / "original_image.bin", original_image);
    WriteFile(out_dir / "patched_image.bin", patched_image);
    WriteRangeCsv(out_dir / "diff_ranges.csv", all_ranges, base_address);
    WriteRangeCsv(out_dir / "diff_ranges_executable.csv", executable_ranges, base_address);

    std::ofstream summary(out_dir / "summary.txt");
    if (!summary) {
      throw std::runtime_error("Failed to write summary.");
    }

    summary << "base=" << base_path.string() << '\n';
    summary << "patch=" << patch_path.string() << '\n';
    summary << "image_base=" << Hex(base_address) << '\n';
    summary << "original_image_size=" << Hex(original_image_size) << '\n';
    summary << "patched_image_size=" << Hex(patched_image_size) << '\n';
    summary << "diff_range_count=" << all_ranges.size() << '\n';
    summary << "diff_byte_count=" << SumBytes(all_ranges) << '\n';
    summary << "executable_diff_range_count=" << executable_ranges.size() << '\n';
    summary << "executable_diff_byte_count=" << SumBytes(executable_ranges) << '\n';
    summary << "sections:\n";
    for (const auto& section : sections) {
      summary << "  " << section.name << " " << Hex(section.virtual_address) << "-"
              << Hex(section.virtual_address + section.virtual_size) << " size="
              << Hex(section.virtual_size) << " executable=" << (section.executable ? "yes" : "no")
              << " writable=" << (section.writable ? "yes" : "no") << '\n';
    }

    std::cout << "Wrote " << out_dir.string() << '\n';
    std::cout << "Image base " << Hex(base_address) << ", diff ranges " << all_ranges.size()
              << " (" << SumBytes(all_ranges) << " bytes), executable ranges "
              << executable_ranges.size() << " (" << SumBytes(executable_ranges) << " bytes)\n";

    rex::ShutdownLogging();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
