// Copyright 2014 Citra Emulator Project / PPSSPP Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "core/arm/cpu_interrupt_handler.h"
#include "core/hardware_properties.h"
#include "core/hle/kernel/memory/memory_types.h"
#include "core/hle/kernel/object.h"

namespace Core {
class CPUInterruptHandler;
class ExclusiveMonitor;
class System;
} // namespace Core

namespace Core::Timing {
class CoreTiming;
struct EventType;
} // namespace Core::Timing

namespace Kernel {

namespace Memory {
class MemoryManager;
template <typename T>
class SlabHeap;
} // namespace Memory

class AddressArbiter;
class ClientPort;
class GlobalScheduler;
class HandleTable;
class PhysicalCore;
class Process;
class ResourceLimit;
class Scheduler;
class SharedMemory;
class Synchronization;
class Thread;
class TimeManager;

/// Represents a single instance of the kernel.
class KernelCore {
private:
    using NamedPortTable = std::unordered_map<std::string, std::shared_ptr<ClientPort>>;

public:
    /// Constructs an instance of the kernel using the given System
    /// instance as a context for any necessary system-related state,
    /// such as threads, CPU core state, etc.
    ///
    /// @post After execution of the constructor, the provided System
    ///       object *must* outlive the kernel instance itself.
    ///
    explicit KernelCore(Core::System& system);
    ~KernelCore();

    KernelCore(const KernelCore&) = delete;
    KernelCore& operator=(const KernelCore&) = delete;

    KernelCore(KernelCore&&) = delete;
    KernelCore& operator=(KernelCore&&) = delete;

    /// Sets if emulation is multicore or single core, must be set before Initialize
    void SetMulticore(bool is_multicore);

    /// Resets the kernel to a clean slate for use.
    void Initialize();

    /// Clears all resources in use by the kernel instance.
    void Shutdown();

    /// Retrieves a shared pointer to the system resource limit instance.
    std::shared_ptr<ResourceLimit> GetSystemResourceLimit() const;

    /// Retrieves a shared pointer to a Thread instance within the thread wakeup handle table.
    std::shared_ptr<Thread> RetrieveThreadFromGlobalHandleTable(Handle handle) const;

    /// Adds the given shared pointer to an internal list of active processes.
    void AppendNewProcess(std::shared_ptr<Process> process);

    /// Makes the given process the new current process.
    void MakeCurrentProcess(Process* process);

    /// Retrieves a pointer to the current process.
    Process* CurrentProcess();

    /// Retrieves a const pointer to the current process.
    const Process* CurrentProcess() const;

    /// Retrieves the list of processes.
    const std::vector<std::shared_ptr<Process>>& GetProcessList() const;

    /// Gets the sole instance of the global scheduler
    Kernel::GlobalScheduler& GlobalScheduler();

    /// Gets the sole instance of the global scheduler
    const Kernel::GlobalScheduler& GlobalScheduler() const;

    /// Gets the sole instance of the Scheduler assoviated with cpu core 'id'
    Kernel::Scheduler& Scheduler(std::size_t id);

    /// Gets the sole instance of the Scheduler assoviated with cpu core 'id'
    const Kernel::Scheduler& Scheduler(std::size_t id) const;

    /// Gets the an instance of the respective physical CPU core.
    Kernel::PhysicalCore& PhysicalCore(std::size_t id);

    /// Gets the an instance of the respective physical CPU core.
    const Kernel::PhysicalCore& PhysicalCore(std::size_t id) const;

    /// Gets the sole instance of the Scheduler at the current running core.
    Kernel::Scheduler& CurrentScheduler();

    /// Gets the sole instance of the Scheduler at the current running core.
    const Kernel::Scheduler& CurrentScheduler() const;

    /// Gets the an instance of the current physical CPU core.
    Kernel::PhysicalCore& CurrentPhysicalCore();

    /// Gets the an instance of the current physical CPU core.
    const Kernel::PhysicalCore& CurrentPhysicalCore() const;

    /// Gets the an instance of the Synchronization Interface.
    Kernel::Synchronization& Synchronization();

    /// Gets the an instance of the Synchronization Interface.
    const Kernel::Synchronization& Synchronization() const;

    /// Gets the an instance of the TimeManager Interface.
    Kernel::TimeManager& TimeManager();

    /// Gets the an instance of the TimeManager Interface.
    const Kernel::TimeManager& TimeManager() const;

    /// Stops execution of 'id' core, in order to reschedule a new thread.
    void PrepareReschedule(std::size_t id);

    Core::ExclusiveMonitor& GetExclusiveMonitor();

    const Core::ExclusiveMonitor& GetExclusiveMonitor() const;

    std::array<Core::CPUInterruptHandler, Core::Hardware::NUM_CPU_CORES>& Interrupts();

    const std::array<Core::CPUInterruptHandler, Core::Hardware::NUM_CPU_CORES>& Interrupts() const;

    void InvalidateAllInstructionCaches();

    /// Adds a port to the named port table
    void AddNamedPort(std::string name, std::shared_ptr<ClientPort> port);

    /// Finds a port within the named port table with the given name.
    NamedPortTable::iterator FindNamedPort(const std::string& name);

    /// Finds a port within the named port table with the given name.
    NamedPortTable::const_iterator FindNamedPort(const std::string& name) const;

    /// Determines whether or not the given port is a valid named port.
    bool IsValidNamedPort(NamedPortTable::const_iterator port) const;

    /// Gets the current host_thread/guest_thread handle.
    Core::EmuThreadHandle GetCurrentEmuThreadID() const;

    /// Gets the current host_thread handle.
    u32 GetCurrentHostThreadID() const;

    /// Register the current thread as a CPU Core Thread.
    void RegisterCoreThread(std::size_t core_id);

    /// Register the current thread as a non CPU core thread.
    void RegisterHostThread();

    /// Gets the virtual memory manager for the kernel.
    Memory::MemoryManager& MemoryManager();

    /// Gets the virtual memory manager for the kernel.
    const Memory::MemoryManager& MemoryManager() const;

    /// Gets the slab heap allocated for user space pages.
    Memory::SlabHeap<Memory::Page>& GetUserSlabHeapPages();

    /// Gets the slab heap allocated for user space pages.
    const Memory::SlabHeap<Memory::Page>& GetUserSlabHeapPages() const;

    /// Gets the shared memory object for HID services.
    Kernel::SharedMemory& GetHidSharedMem();

    /// Gets the shared memory object for HID services.
    const Kernel::SharedMemory& GetHidSharedMem() const;

    /// Gets the shared memory object for font services.
    Kernel::SharedMemory& GetFontSharedMem();

    /// Gets the shared memory object for font services.
    const Kernel::SharedMemory& GetFontSharedMem() const;

    /// Gets the shared memory object for IRS services.
    Kernel::SharedMemory& GetIrsSharedMem();

    /// Gets the shared memory object for IRS services.
    const Kernel::SharedMemory& GetIrsSharedMem() const;

    /// Gets the shared memory object for Time services.
    Kernel::SharedMemory& GetTimeSharedMem();

    /// Gets the shared memory object for Time services.
    const Kernel::SharedMemory& GetTimeSharedMem() const;

    /// Suspend/unsuspend the OS.
    void Suspend(bool in_suspention);

    /// Exceptional exit the OS.
    void ExceptionalExit();

    bool IsMulticore() const;

    void EnterSVCProfile();

    void ExitSVCProfile();

private:
    friend class Object;
    friend class Process;
    friend class Thread;

    /// Creates a new object ID, incrementing the internal object ID counter.
    u32 CreateNewObjectID();

    /// Creates a new process ID, incrementing the internal process ID counter;
    u64 CreateNewKernelProcessID();

    /// Creates a new process ID, incrementing the internal process ID counter;
    u64 CreateNewUserProcessID();

    /// Creates a new thread ID, incrementing the internal thread ID counter.
    u64 CreateNewThreadID();

    /// Provides a reference to the global handle table.
    Kernel::HandleTable& GlobalHandleTable();

    /// Provides a const reference to the global handle table.
    const Kernel::HandleTable& GlobalHandleTable() const;

    struct Impl;
    std::unique_ptr<Impl> impl;
    bool exception_exited{};
};

} // namespace Kernel
