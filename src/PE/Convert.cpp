/* Copyright 2021 - 2023 R. Thomas
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
#include "LIEF/BinaryStream/Convert.hpp"
#include "LIEF/BinaryStream/BinaryStream.hpp"
#include "LIEF/PE/LoadConfigurations.hpp"
#include "PE/Structures.hpp"

namespace LIEF {
namespace Convert {
  // TODO: Implement endian convert
template<>
void swap_endian<LIEF::PE::details::pe_unwind_info>(LIEF::PE::details::pe_unwind_info*) {

}
template<>
void swap_endian<LIEF::PE::details::pe_unwind_code>(LIEF::PE::details::pe_unwind_code*) {

}
template<>
void swap_endian<LIEF::PE::details::pe_exception_entry_x64>(LIEF::PE::details::pe_exception_entry_x64*) {

}

}
}
