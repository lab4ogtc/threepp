#import "MetalDynamicShaderCache.hpp"

#include <algorithm>
#include <iostream>
#include <list>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace threepp::metal {

    namespace {

        SourceKey keyFor(std::string_view source) {
            return {std::hash<std::string_view>{}(source), source.size()};
        }

        bool sourceMatches(const std::string& cached, std::string_view incoming) {
            return cached.size() == incoming.size() &&
                   std::string_view{cached.data(), cached.size()} == incoming;
        }

        std::string functionName(NSString* name) {
            const char* value = name ? [name UTF8String] : nullptr;
            return value ? std::string(value) : std::string{};
        }

        NSString* makeNSString(std::string_view source) {
            return [[NSString alloc] initWithBytes:source.data()
                                           length:source.size()
                                         encoding:NSUTF8StringEncoding];
        }

        struct CompileLruEntry {
            CompileKey key;
            std::string source;
        };

        struct LibraryLruEntry {
            SourceKey key;
            std::string source;
        };

        struct FunctionLruEntry {
            FunctionKey key;
            std::string source;
        };

    } // namespace

    struct MetalDynamicShaderCache::Impl {
        struct CompileRecord {
            std::string source;
            CompileResult result;
            std::list<CompileLruEntry>::iterator lruIt;
        };

        struct LibraryRecord {
            std::string source;
            id<MTLLibrary> library = nil;
            std::list<LibraryLruEntry>::iterator lruIt;
        };

        struct FunctionRecord {
            std::string source;
            std::string functionName;
            id<MTLFunction> function = nil;
            std::list<FunctionLruEntry>::iterator lruIt;
        };

        id<MTLDevice> device = nil;
        std::size_t capacity = 128;
        EvictFunctionCallback evictFunctionCallback;

        std::unordered_map<CompileKey, std::vector<CompileRecord>, CompileKeyHash> compileRecords;
        std::list<CompileLruEntry> compileLru;

        std::unordered_map<SourceKey, std::vector<LibraryRecord>, SourceKeyHash> libraryRecords;
        std::list<LibraryLruEntry> libraryLru;

        std::unordered_map<FunctionKey, std::vector<FunctionRecord>, FunctionKeyHash> functionRecords;
        std::list<FunctionLruEntry> functionLru;

        Impl(id<MTLDevice> dev, std::size_t cap)
            : device(dev),
              capacity(std::max<std::size_t>(cap, 1)) {}

        CompileResult compile(ShaderCompiler& compiler,
                              std::string_view source,
                              ShaderStage stage,
                              TargetLanguage targetLanguage) {
            const CompileKey key{keyFor(source), stage, targetLanguage};
            auto& bucket = compileRecords[key];
            if (auto* record = findCompileRecord(bucket, source)) {
                touchCompile(*record, key);
                return record->result;
            }

            auto result = compiler.compile(source, stage, targetLanguage);
            compileLru.push_front({key, std::string(source)});
            bucket.push_back({std::string(source), result, compileLru.begin()});
            evictCompileRecordsIfNeeded();
            return result;
        }

        id<MTLFunction> getFunction(std::string_view mslSource, NSString* name) {
            const auto resolvedName = functionName(name);
            if (resolvedName.empty()) {
                std::cerr << "MetalRenderer: dynamic MSL function has an empty name\n";
                return nil;
            }

            const SourceKey sourceKey = keyFor(mslSource);
            const FunctionKey key{sourceKey, resolvedName};
            auto& bucket = functionRecords[key];
            if (auto* record = findFunctionRecord(bucket, mslSource, resolvedName)) {
                touchFunction(*record, key);
                return record->function;
            }

            auto library = getLibrary(mslSource);
            if (!library) return nil;

            auto function = [library newFunctionWithName:name];
            if (!function) {
                std::cerr << "MetalRenderer: dynamic MSL function not found: " << resolvedName << "\n";
                return nil;
            }

            functionLru.push_front({key, std::string(mslSource)});
            bucket.push_back({std::string(mslSource), resolvedName, function, functionLru.begin()});
            evictFunctionRecordsIfNeeded();
            return function;
        }

        void setEvictFunctionCallback(EvictFunctionCallback callback) {
            evictFunctionCallback = std::move(callback);
        }

        void clear() {
            while (!functionLru.empty()) {
                evictFunctionRecord(functionLru.back());
            }
            libraryRecords.clear();
            libraryLru.clear();
            compileRecords.clear();
            compileLru.clear();
        }

    private:
        static CompileRecord* findCompileRecord(std::vector<CompileRecord>& records, std::string_view source) {
            for (auto& record : records) {
                if (sourceMatches(record.source, source)) {
                    return &record;
                }
            }
            return nullptr;
        }

        static LibraryRecord* findLibraryRecord(std::vector<LibraryRecord>& records, std::string_view source) {
            for (auto& record : records) {
                if (sourceMatches(record.source, source)) {
                    return &record;
                }
            }
            return nullptr;
        }

        static FunctionRecord* findFunctionRecord(std::vector<FunctionRecord>& records,
                                                  std::string_view source,
                                                  const std::string& name) {
            for (auto& record : records) {
                if (record.functionName == name && sourceMatches(record.source, source)) {
                    return &record;
                }
            }
            return nullptr;
        }

        void touchCompile(CompileRecord& record, const CompileKey& key) {
            compileLru.erase(record.lruIt);
            compileLru.push_front({key, record.source});
            record.lruIt = compileLru.begin();
        }

        void touchLibrary(LibraryRecord& record, SourceKey key) {
            libraryLru.erase(record.lruIt);
            libraryLru.push_front({key, record.source});
            record.lruIt = libraryLru.begin();
        }

        void touchFunction(FunctionRecord& record, const FunctionKey& key) {
            functionLru.erase(record.lruIt);
            functionLru.push_front({key, record.source});
            record.lruIt = functionLru.begin();
        }

        id<MTLLibrary> getLibrary(std::string_view source) {
            if (!device) {
                std::cerr << "MetalRenderer: dynamic MSL library requested without a Metal device\n";
                return nil;
            }

            const SourceKey key = keyFor(source);
            auto& bucket = libraryRecords[key];
            if (auto* record = findLibraryRecord(bucket, source)) {
                touchLibrary(*record, key);
                return record->library;
            }

            NSError* error = nil;
            NSString* metalSource = makeNSString(source);
            id<MTLLibrary> library = [device newLibraryWithSource:metalSource options:nil error:&error];
            if (!library) {
                std::cerr << "MetalRenderer: failed to compile dynamic MSL library: "
                          << (error ? [error.localizedDescription UTF8String] : "unknown error") << "\n";
                return nil;
            }

            libraryLru.push_front({key, std::string(source)});
            bucket.push_back({std::string(source), library, libraryLru.begin()});
            evictLibraryRecordsIfNeeded();
            return library;
        }

        void evictCompileRecordsIfNeeded() {
            while (compileLru.size() > capacity) {
                const auto entry = compileLru.back();
                auto it = compileRecords.find(entry.key);
                if (it != compileRecords.end()) {
                    auto& records = it->second;
                    records.erase(std::remove_if(records.begin(), records.end(), [&](const CompileRecord& record) {
                                      return sourceMatches(record.source, entry.source);
                                  }),
                                  records.end());
                    if (records.empty()) {
                        compileRecords.erase(it);
                    }
                }
                compileLru.pop_back();
            }
        }

        void evictLibraryRecordsIfNeeded() {
            while (libraryLru.size() > capacity) {
                const auto entry = libraryLru.back();
                auto it = libraryRecords.find(entry.key);
                if (it != libraryRecords.end()) {
                    auto& records = it->second;
                    records.erase(std::remove_if(records.begin(), records.end(), [&](const LibraryRecord& record) {
                                      return sourceMatches(record.source, entry.source);
                                  }),
                                  records.end());
                    if (records.empty()) {
                        libraryRecords.erase(it);
                    }
                }
                libraryLru.pop_back();
            }
        }

        void evictFunctionRecordsIfNeeded() {
            while (functionLru.size() > capacity) {
                evictFunctionRecord(functionLru.back());
            }
        }

        void evictFunctionRecord(const FunctionLruEntry& entry) {
            auto it = functionRecords.find(entry.key);
            if (it != functionRecords.end()) {
                auto& records = it->second;
                for (auto recordIt = records.begin(); recordIt != records.end(); ++recordIt) {
                    if (recordIt->functionName == entry.key.name && sourceMatches(recordIt->source, entry.source)) {
                        if (evictFunctionCallback && recordIt->function) {
                            evictFunctionCallback((__bridge void*) recordIt->function);
                        }
                        records.erase(recordIt);
                        break;
                    }
                }

                if (records.empty()) {
                    functionRecords.erase(it);
                }
            }
            functionLru.pop_back();
        }
    };

    MetalDynamicShaderCache::MetalDynamicShaderCache(void* device, std::size_t capacity)
        : pimpl_(std::make_unique<Impl>((__bridge id<MTLDevice>) device, capacity)) {}

    MetalDynamicShaderCache::~MetalDynamicShaderCache() = default;

    CompileResult MetalDynamicShaderCache::compile(ShaderCompiler& compiler,
                                                   std::string_view source,
                                                   ShaderStage stage,
                                                   TargetLanguage targetLanguage) {
        return pimpl_->compile(compiler, source, stage, targetLanguage);
    }

    id<MTLFunction> MetalDynamicShaderCache::getFunction(std::string_view mslSource, NSString* name) {
        return pimpl_->getFunction(mslSource, name);
    }

    void MetalDynamicShaderCache::setEvictFunctionCallback(EvictFunctionCallback callback) {
        pimpl_->setEvictFunctionCallback(std::move(callback));
    }

    void MetalDynamicShaderCache::clear() {
        pimpl_->clear();
    }

} // namespace threepp::metal
