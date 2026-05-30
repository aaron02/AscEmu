/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once
#include <vector>
#include <atomic>
#include <memory>
#include <future>
#include <variant>

#include "VisibilityTypes.hpp"

namespace visibility 
{

    // --- Befehls-Payloads ---
struct CmdSub
    {
        ObjectHandle who;
        int radius{ 2 };
        bool asPlayer{ true };
        int nonPlayerRadius { 2 };
    };

    struct CmdUnsub
    {
        ObjectHandle who;
    };

    struct CmdClearRole
    {
        ObjectHandle who;
        bool asPlayer{ true }; // true = viewer role, false = activator role
    };

    struct CmdPublish
    {
        visibility::ObjectHandle who;
        int extraCells;
        bool playersOnly;
    };

    struct CmdUnpublish
    {
        visibility::ObjectHandle who;
    };

    struct CmdActivateGrid
    {
        int gid;
    };

    // Statt union: std::variant (default = monostate)
    using CmdPayload = std::variant<
        std::monostate,
        CmdSub,
        CmdUnsub,
        CmdClearRole,
        CmdPublish,
        CmdUnpublish,
        CmdActivateGrid
    >;

    struct WorldCmd 
    {
        CmdPayload payload{};                               // active command
        std::shared_ptr<std::promise<void>> fence{};        // PushToWorld
    };

    // Single-Producer/Consumer Ringpuffer (MPSC aufrufbar, hier 1 Writer im Grid-Thread)
    class CommandQueue 
    {
    public:
        explicit CommandQueue(std::size_t cap = 4096) : buffer_(cap), mask_(cap - 1)
        {
            for (std::size_t i = 0; i < cap; ++i) buffer_[i].seq.store(i, std::memory_order_relaxed);
        }

        bool Push(WorldCmd cmd)
        {
            std::uint64_t pos = head_.load(std::memory_order_relaxed);
            for (;;) 
            {
                Slot& s = buffer_[pos & mask_];
                const std::uint64_t seq = s.seq.load(std::memory_order_acquire);
                const auto dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
                if (dif == 0) 
                {
                    if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) 
                    {
                        s.cmd = std::move(cmd);
                        s.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (dif < 0) 
                {
                    return false;
                }
                else 
                {
                    pos = head_.load(std::memory_order_relaxed);
                }
            }
        }

        bool Pop(WorldCmd& out)
        {
            std::uint64_t pos = tail_.load(std::memory_order_relaxed);
            Slot& s = buffer_[pos & mask_];
            const std::uint64_t seq = s.seq.load(std::memory_order_acquire);
            const auto dif = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);
            if (dif == 0) 
            {
                tail_.store(pos + 1, std::memory_order_relaxed);
                out = std::move(s.cmd);
                s.seq.store(pos + mask_ + 1, std::memory_order_release);
                return true;
            }
            return false;
        }

    private:
        struct Slot { std::atomic<std::uint64_t> seq; WorldCmd cmd; };
        std::vector<Slot> buffer_{};
        std::atomic<std::uint64_t> head_{ 0 };
        std::atomic<std::uint64_t> tail_{ 0 };
        const std::size_t mask_;
    };

} // namespace visibility
