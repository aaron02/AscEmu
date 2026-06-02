/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <variant>
#include <vector>

#include "VisibilityTypes.hpp"

namespace visibility
{
    /// Command payload used to subscribe an object to visibility updates.
    struct CmdSub
    {
        ObjectHandle who;
        int radius{ 2 };
        bool asPlayer{ true };
        int nonPlayerRadius{ 2 };
    };

    /// Command payload used to remove an object from visibility updates.
    struct CmdUnsub
    {
        ObjectHandle who;
    };

    /// Command payload used to clear the visibility role of an object.
    struct CmdClearRole
    {
        ObjectHandle who;

        // true  = clear viewer role
        // false = clear activator role
        bool asPlayer{ true };
    };

    /// Command payload used to publish an object into the visibility system.
    struct CmdPublish
    {
        ObjectHandle who;
        int extraCells;
        bool playersOnly;
    };

    /// Command payload used to remove a published object from the visibility system.
    struct CmdUnpublish
    {
        ObjectHandle who;
    };

    /// Command payload used to activate a grid.
    struct CmdActivateGrid
    {
        int gid;
    };

    /// Type-safe command payload.
    ///
    /// std::monostate is used as the default empty command state.
    using CmdPayload = std::variant<
        std::monostate,
        CmdSub,
        CmdUnsub,
        CmdClearRole,
        CmdPublish,
        CmdUnpublish,
        CmdActivateGrid
    >;

    /// Command passed from the producer side to the world processing side.
    struct WorldCmd
    {
        CmdPayload payload{};                        // Active command payload.
        std::shared_ptr<std::promise<void>> fence{}; // Optional completion fence used by PushToWorld.
    };

    /// Lock-free bounded command queue.
    ///
    /// The queue supports multiple producers and one consumer.
    /// The capacity must be a power of two because index wrapping uses mask_.
    class CommandQueue
    {
    public:
        /// Creates a command queue with the given capacity.
        ///
        /// \param cap Number of slots in the queue. Must be a power of two.
        explicit CommandQueue(std::size_t cap = 4096) :
            buffer_(cap),
            mask_(cap - 1)
        {
            for (std::size_t i = 0; i < cap; ++i)
                buffer_[i].seq.store(i, std::memory_order_relaxed);
        }

        /// Pushes a command into the queue.
        ///
        /// \param cmd Command to enqueue.
        /// \return true if the command was enqueued, false if the queue is full.
        bool Push(WorldCmd cmd)
        {
            std::uint64_t pos = head_.load(std::memory_order_relaxed);

            for (;;)
            {
                Slot& s = buffer_[pos & mask_];

                const std::uint64_t seq = s.seq.load(std::memory_order_acquire);
                const auto dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

                // Slot is available for this producer.
                if (dif == 0)
                {
                    // Reserve the slot. compare_exchange_weak updates pos on failure.
                    if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    {
                        s.cmd = std::move(cmd);

                        // Publish the written command to the consumer.
                        s.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                }
                // The producer has caught up with a slot that is still owned by the consumer.
                else if (dif < 0)
                {
                    return false;
                }
                // Another producer advanced the head. Reload the current position and retry.
                else
                {
                    pos = head_.load(std::memory_order_relaxed);
                }
            }
        }

        /// Pops one command from the queue.
        ///
        /// \param out Receives the dequeued command.
        /// \return true if a command was dequeued, false if the queue is empty.
        bool Pop(WorldCmd& out)
        {
            std::uint64_t pos = tail_.load(std::memory_order_relaxed);
            Slot& s = buffer_[pos & mask_];

            const std::uint64_t seq = s.seq.load(std::memory_order_acquire);
            const auto dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            // Slot contains a command ready for the consumer.
            if (dif == 0)
            {
                tail_.store(pos + 1, std::memory_order_relaxed);

                out = std::move(s.cmd);

                // Mark the slot as free for the next producer cycle.
                s.seq.store(pos + mask_ + 1, std::memory_order_release);
                return true;
            }

            return false;
        }

    private:
        /// Single queue slot containing a sequence marker and the command storage.
        struct Slot
        {
            std::atomic<std::uint64_t> seq;
            WorldCmd cmd;
        };

        std::vector<Slot> buffer_{};
        std::atomic<std::uint64_t> head_{ 0 };
        std::atomic<std::uint64_t> tail_{ 0 };

        // Used for fast wrapping. Requires the queue capacity to be a power of two.
        const std::size_t mask_;
    };

} // namespace visibility
