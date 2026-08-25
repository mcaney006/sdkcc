#ifndef SDKCC_COMPILER_BACKEND_C_GENERATOR_HPP
#define SDKCC_COMPILER_BACKEND_C_GENERATOR_HPP

#include <sdkcc/compiler/backend/code_writer.hpp>
#include <sdkcc/compiler/ir/nair.hpp>
#include <sdkcc/compiler/passes/prepare_codegen.hpp>

#include <vector>

namespace sdkcc::compiler::backend::c {

[[nodiscard]] std::vector<GeneratedFile> generate(const nair::Module &module,
                                                  const CodegenPlan &plan,
                                                  bool include_cpp_target);

} // namespace sdkcc::compiler::backend::c

#endif
