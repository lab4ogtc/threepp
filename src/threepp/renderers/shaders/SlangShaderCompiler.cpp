
#include "threepp/renderers/shaders/SlangShaderCompiler.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

using namespace threepp;

namespace {

    std::string blobString(slang::IBlob* blob) {
        if (!blob || blob->getBufferSize() == 0) return {};

        return {static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()};
    }

    void appendDiagnostics(std::string& target, slang::IBlob* diagnostics) {
        auto text = blobString(diagnostics);
        if (text.empty()) return;

        if (!target.empty() && target.back() != '\n') {
            target.push_back('\n');
        }
        target += text;
    }

    slang::IGlobalSession& globalSession() {
        static Slang::ComPtr<slang::IGlobalSession> session = [] {
            Slang::ComPtr<slang::IGlobalSession> result;
            if (SLANG_FAILED(slang::createGlobalSession(result.writeRef())) || !result) {
                throw std::runtime_error("Failed to create Slang global session");
            }
            return result;
        }();

        return *session;
    }

    SlangStage slangStage(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:
                return SLANG_STAGE_VERTEX;
            case ShaderStage::Fragment:
                return SLANG_STAGE_FRAGMENT;
        }

        return SLANG_STAGE_NONE;
    }

    const char* entryPointName(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:
                return "vertexMain";
            case ShaderStage::Fragment:
                return "fragmentMain";
        }

        return "";
    }

    std::string moduleNameFor(std::size_t hash) {
        std::ostringstream stream;
        stream << "threepp_dynamic_" << hash;
        return stream.str();
    }

}// namespace

struct SlangShaderCompiler::Impl {
    Slang::ComPtr<slang::ISession> session;
    std::unordered_map<std::string, Slang::ComPtr<slang::IModule>> moduleCache;

    Impl() {
        std::array<slang::TargetDesc, 3> targets{};
        targets[0].format = SLANG_METAL;
        targets[1].format = SLANG_GLSL;
        targets[2].format = SLANG_SPIRV;

        slang::SessionDesc sessionDesc{};
        sessionDesc.targets = targets.data();
        sessionDesc.targetCount = static_cast<SlangInt>(targets.size());
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        if (SLANG_FAILED(globalSession().createSession(sessionDesc, session.writeRef())) || !session) {
            throw std::runtime_error("Failed to create Slang session");
        }
    }

    slang::IModule* moduleForSource(std::string_view source, std::string& diagnostics) {
        std::string sourceString{source};
        if (auto it = moduleCache.find(sourceString); it != moduleCache.end()) {
            return it->second;
        }

        auto moduleName = moduleNameFor(std::hash<std::string>{}(sourceString));
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        Slang::ComPtr<slang::IModule> module;
        module = session->loadModuleFromSourceString(
                moduleName.c_str(),
                nullptr,
                sourceString.c_str(),
                diagnosticsBlob.writeRef());
        appendDiagnostics(diagnostics, diagnosticsBlob);
        if (!module) return nullptr;

        auto* modulePtr = module.get();
        moduleCache.emplace(std::move(sourceString), std::move(module));
        return modulePtr;
    }
};

SlangShaderCompiler::SlangShaderCompiler()
    : impl_(std::make_unique<Impl>()) {}

SlangShaderCompiler::~SlangShaderCompiler() = default;

SlangShaderCompiler::SlangShaderCompiler(SlangShaderCompiler&&) noexcept = default;

SlangShaderCompiler& SlangShaderCompiler::operator=(SlangShaderCompiler&&) noexcept = default;

CompileResult SlangShaderCompiler::compile(std::string_view source, ShaderStage stage, TargetLanguage targetLanguage) {

    CompileResult result;
    auto* module = impl_->moduleForSource(source, result.diagnostics);
    if (!module) return result;

    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    auto status = module->findEntryPointByName(entryPointName(stage), entryPoint.writeRef());
    if (SLANG_FAILED(status) || !entryPoint) {
        diagnosticsBlob.setNull();
        status = module->findAndCheckEntryPoint(
                entryPointName(stage),
                slangStage(stage),
                entryPoint.writeRef(),
                diagnosticsBlob.writeRef());
        appendDiagnostics(result.diagnostics, diagnosticsBlob);
    }
    if (SLANG_FAILED(status) || !entryPoint) {
        if (!result.diagnostics.empty() && result.diagnostics.back() != '\n') {
            result.diagnostics.push_back('\n');
        }
        result.diagnostics += "Slang entry point not found: ";
        result.diagnostics += entryPointName(stage);
        return result;
    }

    std::array<slang::IComponentType*, 2> components{module, entryPoint.get()};
    Slang::ComPtr<slang::IComponentType> program;
    diagnosticsBlob.setNull();
    status = impl_->session->createCompositeComponentType(
            components.data(),
            static_cast<SlangInt>(components.size()),
            program.writeRef(),
            diagnosticsBlob.writeRef());
    appendDiagnostics(result.diagnostics, diagnosticsBlob);
    if (SLANG_FAILED(status) || !program) return result;

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    diagnosticsBlob.setNull();
    status = program->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    appendDiagnostics(result.diagnostics, diagnosticsBlob);
    if (SLANG_FAILED(status) || !linkedProgram) return result;

    const auto targetIndex = static_cast<SlangInt>(targetLanguage == TargetLanguage::MSL ? 0 : targetLanguage == TargetLanguage::GLSL ? 1 : 2);
    Slang::ComPtr<slang::IBlob> codeBlob;
    diagnosticsBlob.setNull();
    status = linkedProgram->getEntryPointCode(
            0,
            targetIndex,
            codeBlob.writeRef(),
            diagnosticsBlob.writeRef());
    appendDiagnostics(result.diagnostics, diagnosticsBlob);
    if (SLANG_FAILED(status) || !codeBlob) return result;

    result.code = blobString(codeBlob);
    result.success = !result.code.empty();
    return result;
}
