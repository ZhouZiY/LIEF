/* Copyright 2017 - 2021 R. Thomas
 * Copyright 2017 - 2021 Quarkslab
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cstring>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <numeric>

#include "logging.hpp"

#include "third-party/utfcpp.hpp"
#include "LIEF/exception.hpp"

#include "LIEF/PE/Builder.hpp"
#include "LIEF/PE/ResourceData.hpp"
#include "LIEF/PE/utils.hpp"
#include "LIEF/PE/ImportEntry.hpp"
#include "LIEF/PE/Import.hpp"
#include "LIEF/PE/Section.hpp"
#include "LIEF/PE/ResourceDirectory.hpp"
#include "LIEF/PE/DataDirectory.hpp"
#include "LIEF/PE/Relocation.hpp"
#include "LIEF/PE/RelocationEntry.hpp"
#include "LIEF/PE/Symbol.hpp"
#include "LIEF/PE/Export.hpp"
#include "LIEF/PE/ExportEntry.hpp"

#include "Builder.tcc"

namespace LIEF {
namespace PE {

Builder::~Builder() = default;

Builder::Builder(Binary* binary) :
  binary_{binary},
  build_imports_{false},
  patch_imports_{false},
  build_relocations_{false},
  build_tls_{false},
  build_resources_{false},
  build_overlay_{true},
  build_dos_stub_{true}
{}


Builder& Builder::build_imports(bool flag) {
  build_imports_ = flag;
  return *this;
}
Builder& Builder::patch_imports(bool flag) {
  patch_imports_ = flag;
  return *this;
}

Builder& Builder::build_relocations(bool flag) {
  build_relocations_ = flag;
  return *this;
}

Builder& Builder::build_tls(bool flag) {
  build_tls_ = flag;
  return *this;
}

Builder& Builder::build_resources(bool flag) {
  build_resources_ = flag;
  return *this;
}

Builder& Builder::build_overlay(bool flag) {
  build_overlay_ = flag;
  return *this;
}

Builder& Builder::build_dos_stub(bool flag) {
  build_dos_stub_ = flag;
  return *this;
}


void Builder::write(const std::string& filename) const {
  std::ofstream output_file{filename, std::ios::out | std::ios::binary | std::ios::trunc};
  if (output_file) {
    std::vector<uint8_t> content;
    ios_.get(content);
    std::copy(
        std::begin(content),
        std::end(content),
        std::ostreambuf_iterator<char>(output_file));
  }
}


void Builder::build() {

  LIEF_DEBUG("Build process started");

  if (binary_->has_tls() && build_tls_) {
    LIEF_DEBUG("[+] TLS");
    if (binary_->type() == PE_TYPE::PE32) {
      build_tls<details::PE32>();
    } else {
      build_tls<details::PE64>();
    }
  }

  if (binary_->has_relocations() && build_relocations_) {
    LIEF_DEBUG("[+] Relocations");
    build_relocation();
  }

  if (binary_->has_resources() && binary_->resources_ != nullptr && build_resources_) {
    LIEF_DEBUG("[+] Resources");
    try {
      build_resources();
    } catch (not_found&) {
    }
  }

  if (binary_->has_imports() && build_imports_) {
    LIEF_DEBUG("[+] Imports");
    if (binary_->type() == PE_TYPE::PE32) {
      build_import_table<details::PE32>();
    } else {
      build_import_table<details::PE64>();
    }
  }

  LIEF_DEBUG("[+] Headers");

  *this << binary_->dos_header()
        << binary_->header()
        << binary_->optional_header();

  for (const DataDirectory& directory : binary_->data_directories()) {
    *this << directory;
  }

  LIEF_DEBUG("[+] Sections");

  for (const Section& section : binary_->sections()) {
    LIEF_DEBUG("  -> {}", section.name());
    *this << section;
  }

  // LIEF_DEBUG("[+] symbols");
  //build_symbols();


  // LIEF_DEBUG("[+] string table");
  //build_string_table();

  if (!binary_->overlay().empty() && build_overlay_) {
    LIEF_DEBUG("[+] Overlay");
    build_overlay();
  }


}

const std::vector<uint8_t>& Builder::get_build() {
  return ios_.raw();
}

//
// Build relocations
//
void Builder::build_relocation() {
  std::vector<uint8_t> content;
  for (const Relocation& relocation : binary_->relocations()) {

    details::pe_base_relocation_block relocation_header;
    relocation_header.PageRVA   = static_cast<uint32_t>(relocation.virtual_address());

    uint32_t block_size = static_cast<uint32_t>((relocation.entries().size()) * sizeof(uint16_t) + sizeof(details::pe_base_relocation_block));
    relocation_header.BlockSize = align(block_size, sizeof(uint32_t));

    content.insert(
        std::end(content),
        reinterpret_cast<uint8_t*>(&relocation_header),
        reinterpret_cast<uint8_t*>(&relocation_header) + sizeof(details::pe_base_relocation_block));

    for (const RelocationEntry& entry: relocation.entries()) {
      uint16_t data = entry.data();
      content.insert(
          std::end(content),
          reinterpret_cast<uint8_t*>(&data),
          reinterpret_cast<uint8_t*>(&data) + sizeof(uint16_t));
    }

    content.insert(
      std::end(content),
      align(content.size(), sizeof(uint32_t)) - content.size(), 0);
  }

  // Align on a 32 bits

  //pe_base_relocation_block relocHeader;
  //relocHeader.PageRVA   = static_cast<uint32_t>(0);
  //relocHeader.BlockSize = static_cast<uint32_t>(0);

  //content.insert(
  //    std::end(content),
  //    reinterpret_cast<uint8_t*>(&relocHeader),
  //    reinterpret_cast<uint8_t*>(&relocHeader) + sizeof(pe_base_relocation_block));

  Section new_relocation_section{".l" + std::to_string(static_cast<uint32_t>(DATA_DIRECTORY::BASE_RELOCATION_TABLE))}; // .l5 -> lief.relocation
  new_relocation_section.characteristics(0x42000040);
  const size_t size_aligned = align(content.size(), binary_->optional_header().file_alignment());

  new_relocation_section.virtual_size(content.size());
  // Pad with 0
  content.insert(
      std::end(content),
      size_aligned - content.size(), 0);

  new_relocation_section.content(content);

  binary_->add_section(new_relocation_section, PE_SECTION_TYPES::RELOCATION);
}


//
// Build resources
//
void Builder::build_resources() {
  ResourceNode& node = binary_->resources();
  //std::cout << ResourcesManager{binary_->resources_} << std::endl;


  uint32_t headerSize = 0;
  uint32_t dataSize   = 0;
  uint32_t nameSize   = 0;

  compute_resources_size(node, &headerSize, &dataSize, &nameSize);
  std::vector<uint8_t> content(headerSize + dataSize + nameSize, 0);
  const uint64_t content_size_aligned = align(content.size(), binary_->optional_header().file_alignment());
  content.insert(std::end(content), content_size_aligned - content.size(), 0);

  uint32_t offsetToHeader = 0;
  uint32_t offsetToName   = headerSize;
  uint32_t offsetToData   = headerSize + nameSize;

  Section new_section_rsrc{".l" + std::to_string(static_cast<uint32_t>(DATA_DIRECTORY::RESOURCE_TABLE))};
  new_section_rsrc.characteristics(0x40000040);
  new_section_rsrc.content(content);

  Section& rsrc_section = binary_->add_section(new_section_rsrc, PE_SECTION_TYPES::RESOURCE);

  construct_resources(node, &content, &offsetToHeader, &offsetToData, &offsetToName, rsrc_section.virtual_address(), 0);

  rsrc_section.content(content);
}

//
// Pre-computation
//
void Builder::compute_resources_size(ResourceNode& node, uint32_t* header_size,
                                     uint32_t* data_size, uint32_t* name_size) {
  if (!node.name().empty()) {
    *name_size += sizeof(uint16_t) + (node.name().size() + 1) * sizeof(char16_t);
  }

  if (node.is_directory()) {
    *header_size += sizeof(details::pe_resource_directory_table);
    *header_size += sizeof(details::pe_resource_directory_entries);
  } else {
    auto& data_dode = reinterpret_cast<ResourceData&>(node);
    *header_size += sizeof(details::pe_resource_data_entry);
    *header_size += sizeof(details::pe_resource_directory_entries);

    // !!! Data content have to be aligned !!!
    *data_size += align(data_dode.content().size(), sizeof(uint32_t));
  }

  for (ResourceNode& child : node.childs()) {
    compute_resources_size(child, header_size, data_size, name_size);
  }
}


//
// Build level by level
//
void Builder::construct_resources(ResourceNode& node, std::vector<uint8_t>* content,
                                  uint32_t* offset_header, uint32_t* offset_data,
                                  uint32_t* offset_name, uint32_t base_rva, uint32_t depth) {

  // Build Directory
  // ===============
  if (node.is_directory()) {
    auto& rsrc_directory = reinterpret_cast<ResourceDirectory&>(node);

    details::pe_resource_directory_table rsrc_header;
    rsrc_header.Characteristics     = static_cast<uint32_t>(rsrc_directory.characteristics());
    rsrc_header.TimeDateStamp       = static_cast<uint32_t>(rsrc_directory.time_date_stamp());
    rsrc_header.MajorVersion        = static_cast<uint16_t>(rsrc_directory.major_version());
    rsrc_header.MinorVersion        = static_cast<uint16_t>(rsrc_directory.minor_version());
    rsrc_header.NumberOfNameEntries = static_cast<uint16_t>(rsrc_directory.numberof_name_entries());
    rsrc_header.NumberOfIDEntries   = static_cast<uint16_t>(rsrc_directory.numberof_id_entries());


    std::copy(reinterpret_cast<uint8_t*>(&rsrc_header),
              reinterpret_cast<uint8_t*>(&rsrc_header) + sizeof(details::pe_resource_directory_table),
              content->data() + *offset_header);

    *offset_header += sizeof(details::pe_resource_directory_table);

    //Build next level
    uint32_t current_offset = *offset_header;

    // Offset to the next RESOURCE_NODE_TYPES::DIRECTORY
    *offset_header += node.childs().size() * sizeof(details::pe_resource_directory_entries);


    // Build childs
    // ============
    for (ResourceNode& child : node.childs()) {
      if ((static_cast<uint32_t>(child.id()) & 0x80000000) != 0u) { // There is a name

        const std::u16string& name = child.name();
        child.id(0x80000000 | *offset_name);

        auto* length_ptr = reinterpret_cast<uint16_t*>(content->data() + *offset_name);
        *length_ptr = name.size();
        auto* name_ptr = reinterpret_cast<char16_t*>(content->data() + *offset_name + sizeof(uint16_t));

        std::copy(reinterpret_cast<const char16_t*>(name.data()),
                  reinterpret_cast<const char16_t*>(name.data()) + name.size(),
                  name_ptr);

        *offset_name += (name.size() + 1) * sizeof(char16_t) + sizeof(uint16_t);
      }

      // DIRECTORY
      if (child.is_directory()) {
        details::pe_resource_directory_entries entry_header;
        entry_header.NameID.IntegerID = static_cast<uint32_t>(child.id());
        entry_header.RVA              = static_cast<uint32_t>((0x80000000 | *offset_header));

        std::copy(reinterpret_cast<uint8_t*>(&entry_header),
                  reinterpret_cast<uint8_t*>(&entry_header) + sizeof(details::pe_resource_directory_entries),
                  content->data() + current_offset);

        current_offset += sizeof(details::pe_resource_directory_entries);
        construct_resources(child, content, offset_header, offset_data, offset_name, base_rva, depth + 1);
      } else { //DATA
        details::pe_resource_directory_entries entry_header;

        entry_header.NameID.IntegerID = static_cast<uint32_t>(child.id());
        entry_header.RVA              = static_cast<uint32_t>(*offset_header);

        std::copy(reinterpret_cast<uint8_t*>(&entry_header),
                  reinterpret_cast<uint8_t*>(&entry_header) + sizeof(details::pe_resource_directory_entries),
                  content->data() + current_offset);

        current_offset += sizeof(details::pe_resource_directory_entries);

        construct_resources(child, content, offset_header, offset_data, offset_name, base_rva, depth + 1);
      }
    }

  } else {
    auto& rsrc_data = reinterpret_cast<ResourceData&>(node);

    details::pe_resource_data_entry data_header;
    data_header.DataRVA  = static_cast<uint32_t>(base_rva + *offset_data);
    data_header.Size     = static_cast<uint32_t>(rsrc_data.content().size());
    data_header.Codepage = static_cast<uint32_t>(rsrc_data.code_page());
    data_header.Reserved = static_cast<uint32_t>(rsrc_data.reserved());


    std::copy(reinterpret_cast<uint8_t*>(&data_header),
              reinterpret_cast<uint8_t*>(&data_header) + sizeof(details::pe_resource_data_entry),
              content->data() + *offset_header);

    *offset_header += sizeof(details::pe_resource_directory_table);
    const std::vector<uint8_t>& resource_content = rsrc_data.content();

    std::copy(std::begin(resource_content), std::end(resource_content),
              content->data() + *offset_data);

    *offset_data += align(resource_content.size(), sizeof(uint32_t));
  }
}


void Builder::build_symbols() {
  //TODO
}


void Builder::build_string_table() {
  //TODO
}

void Builder::build_overlay() {

  const uint64_t last_section_offset = std::accumulate(
      std::begin(binary_->sections_),
      std::end(binary_->sections_), 0,
      [] (uint64_t offset, const Section* section) {
        return std::max<uint64_t>(section->offset() + section->size(), offset);
      });

  LIEF_DEBUG("Overlay offset: 0x{:x}", last_section_offset);
  LIEF_DEBUG("Overlay size: 0x{:x}", binary_->overlay().size());

  const size_t saved_offset = ios_.tellp();
  ios_.seekp(last_section_offset);
  ios_.write(binary_->overlay());
  ios_.seekp(saved_offset);
}

Builder& Builder::operator<<(const DosHeader& dos_header) {

  details::pe_dos_header raw_dos_header;
  raw_dos_header.Magic                     = static_cast<uint16_t>(dos_header.magic());
  raw_dos_header.UsedBytesInTheLastPage    = static_cast<uint16_t>(dos_header.used_bytes_in_the_last_page());
  raw_dos_header.FileSizeInPages           = static_cast<uint16_t>(dos_header.file_size_in_pages());
  raw_dos_header.NumberOfRelocationItems   = static_cast<uint16_t>(dos_header.numberof_relocation());
  raw_dos_header.HeaderSizeInParagraphs    = static_cast<uint16_t>(dos_header.header_size_in_paragraphs());
  raw_dos_header.MinimumExtraParagraphs    = static_cast<uint16_t>(dos_header.minimum_extra_paragraphs());
  raw_dos_header.MaximumExtraParagraphs    = static_cast<uint16_t>(dos_header.maximum_extra_paragraphs());
  raw_dos_header.InitialRelativeSS         = static_cast<uint16_t>(dos_header.initial_relative_ss());
  raw_dos_header.InitialSP                 = static_cast<uint16_t>(dos_header.initial_sp());
  raw_dos_header.Checksum                  = static_cast<uint16_t>(dos_header.checksum());
  raw_dos_header.InitialIP                 = static_cast<uint16_t>(dos_header.initial_ip());
  raw_dos_header.InitialRelativeCS         = static_cast<uint16_t>(dos_header.initial_relative_cs());
  raw_dos_header.AddressOfRelocationTable  = static_cast<uint16_t>(dos_header.addressof_relocation_table());
  raw_dos_header.OverlayNumber             = static_cast<uint16_t>(dos_header.overlay_number());
  raw_dos_header.OEMid                     = static_cast<uint16_t>(dos_header.oem_id());
  raw_dos_header.OEMinfo                   = static_cast<uint16_t>(dos_header.oem_info());
  raw_dos_header.AddressOfNewExeHeader     = static_cast<uint16_t>(dos_header.addressof_new_exeheader());

  const DosHeader::reserved_t& reserved   = dos_header.reserved();
  const DosHeader::reserved2_t& reserved2 = dos_header.reserved2();

  std::copy(std::begin(reserved),  std::end(reserved),  std::begin(raw_dos_header.Reserved));
  std::copy(std::begin(reserved2), std::end(reserved2), std::begin(raw_dos_header.Reserved2));

  ios_.seekp(0);
  ios_.write(reinterpret_cast<const uint8_t*>(&raw_dos_header), sizeof(details::pe_dos_header));
  if (!binary_->dos_stub().empty() && build_dos_stub_) {

    if (sizeof(details::pe_dos_header) + binary_->dos_stub().size() > dos_header.addressof_new_exeheader()) {
      LIEF_WARN("Inconsistent 'addressof_new_exeheader': 0x{:x}", dos_header.addressof_new_exeheader());
    }
    ios_.write(binary_->dos_stub());
  }

  return *this;
}


Builder& Builder::operator<<(const Header& bHeader) {
  // Standard Header
  details::pe_header header;
  header.Machine               = static_cast<uint16_t>(bHeader.machine());
  header.NumberOfSections      = static_cast<uint16_t>(binary_->sections_.size());
  //TODO: use current
  header.TimeDateStamp         = static_cast<uint32_t>(bHeader.time_date_stamp());
  header.PointerToSymbolTable  = static_cast<uint32_t>(bHeader.pointerto_symbol_table());
  header.NumberOfSymbols       = static_cast<uint32_t>(bHeader.numberof_symbols());
  //TODO: Check
  header.SizeOfOptionalHeader  = static_cast<uint16_t>(bHeader.sizeof_optional_header());
  header.Characteristics       = static_cast<uint16_t>(bHeader.characteristics());

  const Header::signature_t& signature = binary_->header_.signature();
  std::copy(std::begin(signature), std::end(signature), std::begin(header.signature));

  const uint32_t address_next_header = binary_->dos_header().addressof_new_exeheader();

  ios_.seekp(address_next_header);
  ios_.write(reinterpret_cast<const uint8_t*>(&header), sizeof(details::pe_header));
  return *this;
}


Builder& Builder::operator<<(const OptionalHeader& optional_header) {
  if (binary_->type() == PE_TYPE::PE32) {
    build_optional_header<details::PE32>(optional_header);
  } else {
    build_optional_header<details::PE64>(optional_header);
  }
  return *this;
}


Builder& Builder::operator<<(const DataDirectory& data_directory) {

  details::pe_data_directory header;

  header.RelativeVirtualAddress = data_directory.RVA();
  header.Size                   = data_directory.size();

  ios_.write(reinterpret_cast<uint8_t*>(&header), sizeof(details::pe_data_directory));
  return *this;
}


Builder& Builder::operator<<(const Section& section) {

  details::pe_section header;
  std::memset(&header, 0, sizeof(details::pe_section));

  header.VirtualAddress       = static_cast<uint32_t>(section.virtual_address());
  header.VirtualSize          = static_cast<uint32_t>(section.virtual_size());
  header.SizeOfRawData        = static_cast<uint32_t>(section.size());
  header.PointerToRawData     = static_cast<uint32_t>(section.pointerto_raw_data());
  header.PointerToRelocations = static_cast<uint32_t>(section.pointerto_relocation());
  header.PointerToLineNumbers = static_cast<uint32_t>(section.pointerto_line_numbers());
  header.NumberOfRelocations  = static_cast<uint16_t>(section.numberof_relocations());
  header.NumberOfLineNumbers  = static_cast<uint16_t>(section.numberof_line_numbers());
  header.Characteristics      = static_cast<uint32_t>(section.characteristics());

  const std::string& sec_name = section.fullname();
  uint32_t name_length = std::min<uint32_t>(sec_name.size() + 1, sizeof(header.Name));
  std::copy(sec_name.c_str(), sec_name.c_str() + name_length, std::begin(header.Name));

  ios_.write(reinterpret_cast<uint8_t*>(&header), sizeof(details::pe_section));

  size_t pad_length = 0;
  if (section.content().size() > section.size()) {
    LIEF_WARN("{} content size is bigger than section's header size", section.name());
  }
  else {
    pad_length = section.size() - section.content().size();
  }

  // Pad section content with zeroes
  std::vector<uint8_t> zero_pad (pad_length, 0);

  const size_t saved_offset = ios_.tellp();
  ios_.seekp(section.offset());
  ios_.write(section.content());
  ios_.write(zero_pad);
  ios_.seekp(saved_offset);
  return *this;
}

std::ostream& operator<<(std::ostream& os, const Builder& b) {
  os << std::left;
  os << std::boolalpha;
  os << std::setw(20) << "Build imports:"     << b.build_imports_     << std::endl;
  os << std::setw(20) << "Patch imports:"     << b.patch_imports_     << std::endl;
  os << std::setw(20) << "Build relocations:" << b.build_relocations_ << std::endl;
  os << std::setw(20) << "Build TLS:"         << b.build_tls_         << std::endl;
  os << std::setw(20) << "Build resources:"   << b.build_resources_   << std::endl;
  os << std::setw(20) << "Build overlay:"     << b.build_overlay_     << std::endl;
  os << std::setw(20) << "Build dos stub:"    << b.build_dos_stub_    << std::endl;
  return os;
}


}
}
