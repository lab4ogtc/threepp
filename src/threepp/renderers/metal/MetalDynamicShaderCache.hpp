#ifndef THREEPP_METAL_DYNAMIC_SHADER_CACHE_HPP
#define THREEPP_METAL_DYNAMIC_SHADER_CACHE_HPP

#import <Metal/Metal.h>

#include "threepp/renderers/shaders/ShaderCompiler.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace threepp::metal {

    /**
     * @brief 着色器源码的轻量 hash key。
     *
     * key 只用于快速定位缓存桶，缓存记录仍保存完整源码并在命中时做碰撞校验。
     */
    struct SourceKey {
        std::size_t hash = 0;
        std::size_t length = 0;

        bool operator==(const SourceKey& other) const {
            return hash == other.hash && length == other.length;
        }
    };

    struct SourceKeyHash {
        std::size_t operator()(const SourceKey& key) const {
            return key.hash ^ (key.length << 1u);
        }
    };

    /**
     * @brief Slang 编译结果缓存 key。
     */
    struct CompileKey {
        SourceKey source;
        ShaderStage stage = ShaderStage::Vertex;
        TargetLanguage targetLanguage = TargetLanguage::MSL;

        bool operator==(const CompileKey& other) const {
            return source == other.source &&
                   stage == other.stage &&
                   targetLanguage == other.targetLanguage;
        }
    };

    struct CompileKeyHash {
        std::size_t operator()(const CompileKey& key) const {
            return SourceKeyHash{}(key.source) ^
                   (std::hash<int>{}(static_cast<int>(key.stage)) << 2u) ^
                   (std::hash<int>{}(static_cast<int>(key.targetLanguage)) << 3u);
        }
    };

    /**
     * @brief 动态 MSL function 缓存 key。
     */
    struct FunctionKey {
        SourceKey source;
        std::string name;

        bool operator==(const FunctionKey& other) const {
            return source == other.source && name == other.name;
        }
    };

    struct FunctionKeyHash {
        std::size_t operator()(const FunctionKey& key) const {
            return SourceKeyHash{}(key.source) ^ (std::hash<std::string>{}(key.name) << 1u);
        }
    };

    /**
     * @brief 管理 Slang 编译结果、动态 MSL library 和 MTLFunction 的 LRU 缓存。
     *
     * @param device Metal device，`getFunction` 需要有效 device；仅测试编译缓存时可传入 nullptr。
     * @param capacity 每类缓存的最大记录数，传入 0 时按 1 处理。
     */
    class MetalDynamicShaderCache {

    public:
        using EvictFunctionCallback = std::function<void(void*)>;

        explicit MetalDynamicShaderCache(void* device, std::size_t capacity = 128);
        ~MetalDynamicShaderCache();

        MetalDynamicShaderCache(const MetalDynamicShaderCache&) = delete;
        MetalDynamicShaderCache& operator=(const MetalDynamicShaderCache&) = delete;

        /**
         * @brief 编译源码并缓存结果。
         * @return 缓存或新编译得到的结果；源码 hash 碰撞时会用完整源码区分记录。
         */
        CompileResult compile(ShaderCompiler& compiler,
                              std::string_view source,
                              ShaderStage stage,
                              TargetLanguage targetLanguage);

        /**
         * @brief 从动态 MSL 源码获取指定 MTLFunction。
         * @return 成功时返回 function；library 编译或函数查找失败时返回 nil 并输出诊断。
         */
        id<MTLFunction> getFunction(std::string_view mslSource, NSString* name);

        /**
         * @brief 设置 function 淘汰回调，用于同步清理依赖该 function 的 PSO。
         */
        void setEvictFunctionCallback(EvictFunctionCallback callback);

        /**
         * @brief 清空所有缓存记录，清理 function 前会触发淘汰回调。
         */
        void clear();

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;
    };

} // namespace threepp::metal

#endif
