//
// Created by zero on 2022/7/9.
//

#pragma once

#include "rhi/command.h"
#include <cuda.h>
namespace ocarina {

class CUDADevice;
class CUDACommandVisitor final : public CommandVisitor {
private:
    CUDADevice *device_{};
    CUstream stream_{};

public:
    explicit CUDACommandVisitor(CUDADevice *device, CUstream stream = nullptr)
        : device_(device),
          stream_(stream) {}
    void visit(const BufferUploadCommand *cmd) noexcept override;
    void visit(const BufferDownloadCommand *cmd) noexcept override;
    void visit(const BufferByteSetCommand *cmd) noexcept override;
    void visit(const BufferCopyCommand *cmd) noexcept override;
    void visit(const BufferReallocateCommand *cmd) noexcept override;
    void visit(const TextureUploadCommand *cmd) noexcept override;
    void visit(const TextureDownloadCommand *cmd) noexcept override;
    void visit(const TextureCopyCommand *cmd) noexcept override;
    void visit(const BufferToTextureCommand *cmd) noexcept override;
    void visit(const BLASBuildCommand *cmd) noexcept override;
    void visit(const TLASBuildCommand *cmd) noexcept override;
    void visit(const TLASUpdateCommand *cmd) noexcept override;
    void visit(const SynchronizeCommand *cmd) noexcept override;
    void visit(const ShaderDispatchCommand *cmd) noexcept override;
    void visit(const HostFunctionCommand *cmd) noexcept override;
};

}// namespace ocarina