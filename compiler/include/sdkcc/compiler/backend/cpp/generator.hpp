#ifndef SDKCC_COMPILER_BACKEND_CPP_GENERATOR_HPP
#define SDKCC_COMPILER_BACKEND_CPP_GENERATOR_HPP

#include <sdkcc/compiler/backend/code_writer.hpp>
#include <sdkcc/compiler/ir/nair.hpp>
#include <sdkcc/compiler/passes/prepare_codegen.hpp>

#include <vector>

namespace sdkcc::compiler::backend::cpp {

[[nodiscard]] std::vector<GeneratedFile> generate(const nair::Module &module,
                                                  const CodegenPlan &plan);

} // namespace sdkcc::compiler::backend::cpp

#endif
