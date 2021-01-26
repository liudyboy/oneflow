/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#ifndef ONEFLOW_CORE_FRAMEWORK_INSTRUCTIONS_BUILDER_H_
#define ONEFLOW_CORE_FRAMEWORK_INSTRUCTIONS_BUILDER_H_

#include "oneflow/core/vm/instruction.cfg.h"
#include "oneflow/core/vm/id_generator.h"
#include "oneflow/core/vm/string_symbol.h"
#include "oneflow/core/job/job_desc.h"
#include "oneflow/core/job/parallel_desc.h"
#include "oneflow/core/eager/eager_symbol.cfg.h"
#include "oneflow/core/framework/symbol_id_cache.h"
#include "oneflow/core/common/global.h"
#include "oneflow/core/common/maybe.h"
#include "oneflow/core/framework/object.h"

namespace oneflow {

namespace detail {

template<typename T>
struct CreateSymbolIdHelper {
  static Maybe<int64_t> Call(vm::IdGenerator* id_generator,
                             vm::cfg::InstructionListProto* instruction_list,
                             eager::cfg::EagerSymbolList* eager_symbol_list, const T& conf);
};

}  // namespace detail

class InstructionsBuilder {
 public:
  InstructionsBuilder(const InstructionsBuilder&) = delete;
  InstructionsBuilder(InstructionsBuilder&&) = delete;
  explicit InstructionsBuilder(const std::shared_ptr<vm::IdGenerator>& id_generator)
      : id_generator_(id_generator),
        instruction_list_(std::make_shared<vm::cfg::InstructionListProto>()),
        eager_symbol_list_(std::make_shared<eager::cfg::EagerSymbolList>()) {}
  InstructionsBuilder(const std::shared_ptr<vm::IdGenerator>& id_generator,
                      const std::shared_ptr<vm::cfg::InstructionListProto>& instruction_list,
                      const std::shared_ptr<eager::cfg::EagerSymbolList>& symbol_list,
                      const std::function<void(compatible_py::BlobObject*)>& release_object)
      : id_generator_(id_generator),
        instruction_list_(instruction_list),
        eager_symbol_list_(symbol_list),
        release_object_(release_object) {}
  ~InstructionsBuilder() = default;

  std::shared_ptr<vm::IdGenerator> id_generator() const { return id_generator_; }
  std::shared_ptr<vm::cfg::InstructionListProto> instruction_list() const {
    return instruction_list_;
  }
  std::shared_ptr<eager::cfg::EagerSymbolList> eager_symbol_list() const {
    return eager_symbol_list_;
  }

  const std::function<void(compatible_py::BlobObject*)>& release_object() const {
    return release_object_;
  }

  template<typename T>
  Maybe<int64_t> FindOrCreateSymbolId(const T& conf) {
    auto* id_cache = Global<symbol::IdCache<T>>::Get();
    return id_cache->FindOrCreate(conf, [&] { return CreateSymbolId<T>(conf); });
  }

  int64_t NewSymbolId();

  int64_t NewObjectId(const std::shared_ptr<ParallelDesc>& parallel_desc_sym);

  int64_t NewSymbolId4OpNodeSignature(std::shared_ptr<cfg::OpNodeSignature> op_node_signature_sym);

  int64_t NewSymbolId4String(std::string str);

  int64_t NewSymbolId4JobConf(const std::shared_ptr<cfg::JobConfigProto>& job_conf);

  int64_t NewSymbolId4ParallelConf(const std::shared_ptr<cfg::ParallelConf>& parallel_conf);

  std::shared_ptr<JobDesc> GetJobConfSymbol(const std::shared_ptr<cfg::JobConfigProto>& job_conf);

  std::shared_ptr<StringSymbol> GetSymbol4String(std::string str);

  std::shared_ptr<ParallelDesc> GetParallelDescSymbol(
      const std::shared_ptr<cfg::ParallelConf>& parallel_conf);

  void DeleteObject(compatible_py::BlobObject* blob_object);

 private:
  void InitStringSymbol(int64_t symbol_id, std::string str);

  void NewParallelConfSymbol(int64_t symbol_id,
                             const std::shared_ptr<cfg::ParallelConf>& parallel_conf);

  void InitJobConfSymbol(int64_t symbol_id, const std::shared_ptr<cfg::JobConfigProto>& job_conf);

  void InitOpNodeSignatureDescSymbol(int64_t symbol_id,
                                     std::shared_ptr<cfg::OpNodeSignature> op_node_signature_sym);

  void _TryClearObject(compatible_py::BlobObject* blob_object);

  void _DeleteObject(compatible_py::BlobObject* blob_object);

  template<typename T>
  Maybe<int64_t> CreateSymbolId(const T& conf) {
    return detail::CreateSymbolIdHelper<T>::Call(mut_id_generator(), mut_instruction_list(),
                                                 mut_eager_symbol_list(), conf);
  }

  vm::cfg::InstructionListProto* mut_instruction_list() { return instruction_list_.get(); }
  eager::cfg::EagerSymbolList* mut_eager_symbol_list() { return eager_symbol_list_.get(); }

  vm::IdGenerator* mut_id_generator() { return id_generator_.get(); }

  std::shared_ptr<vm::IdGenerator> id_generator_;
  std::shared_ptr<vm::cfg::InstructionListProto> instruction_list_;
  std::shared_ptr<eager::cfg::EagerSymbolList> eager_symbol_list_;
  std::function<void(compatible_py::BlobObject*)> release_object_;
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_FRAMEWORK_INSTRUCTIONS_BUILDER_H_
