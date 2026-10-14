#include "engine/usecode/npc_intrinsics.hpp"

#include "engine/world/actor_anim.hpp"
#include "engine/world/actor_create.hpp"
#include "engine/world/attack.hpp"
#include "engine/world/chargers.hpp"
#include "engine/world/inventory.hpp"
#include "engine/world/item_damage.hpp"
#include "engine/world/sprite_process.hpp"
#include "engine/world/teleport.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>

namespace crusader {

namespace {

using Args = std::span<const std::uint8_t>;

std::uint16_t arg16(Args a, std::size_t pos) {
    return pos + 2 <= a.size() ? static_cast<std::uint16_t>(a[pos] | a[pos + 1] << 8) : 0;
}

}  // namespace

// @orig none: binds the intrinsics, numbered by USECODE/UNKCOFF.DAT
std::vector<std::uint16_t> register_npc_intrinsics(UCMachine& m, World& w, const AnimDat& anims,
                                                   const dtable::NpcTypes* types) {
    std::vector<std::uint16_t> bound;
    // Registers `fn` (taking the NPC id) under every alias number. The first
    // argument is a pointer to the object id.
    auto bind = [&](std::initializer_list<std::uint16_t> numbers, std::function<std::uint32_t(ObjId, Args)> fn) {
        for (std::uint16_t n : numbers) {
            m.set_intrinsic(n, [&w, fn](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
                std::array<std::uint8_t, 2> id{};
                const std::uint32_t ptr = arg16(a, 0) | std::uint32_t{arg16(a, 2)} << 16;
                if (!machine.dereference(ptr, id)) return 0;
                const ObjId npc = static_cast<ObjId>(id[0] | id[1] << 8);
                return w.get(npc) && !w.npc_record(npc).empty() ? fn(npc, a) : 0;
            });
            bound.push_back(n);
        }
    };
    bind({0x052}, [&w](ObjId id, Args) -> std::uint32_t { return w.npc_word(id, npc::kActivity); });
    bind({0x0E1}, [&w](ObjId id, Args) -> std::uint32_t { return w.npc_word(id, npc::kDefaultActivity); });
    bind({0x0E2}, [&w](ObjId id, Args) -> std::uint32_t { return w.npc_word(id, npc::kDefaultActivity + 2); });
    bind({0x0E3}, [&w](ObjId id, Args) -> std::uint32_t { return w.npc_word(id, npc::kDefaultActivity + 4); });
    // Actor::I_setDefaultActivity0..2(npc, activity) (10F0:2BEA, 2C07, 2C24).
    for (int i = 0; i < 3; ++i) {
        bind({static_cast<std::uint16_t>(0x054 + i)}, [&w, i](ObjId id, Args a) -> std::uint32_t {
            w.set_npc_word(id, npc::kDefaultActivity + 2 * static_cast<std::size_t>(i), arg16(a, 4));
            return 0;
        });
    }
    bind({0x0CD}, [&w](ObjId id, Args) -> std::uint32_t { return in_combat(w, id); });
    bind({0x057, 0x07E, 0x0CE, 0x0DB, 0x0F2}, [&m, &w, &anims](ObjId id, Args a) -> std::uint32_t {
        set_activity(m.kernel(), w, anims, id, arg16(a, 4));
        return 0;
    });
    bind({0x0E5}, [&m, &w, &anims](ObjId id, Args a) -> std::uint32_t {
        set_target(m.kernel(), w, anims, id, arg16(a, 4));
        return 0;
    });
    bind({0x053}, [&m, &w, &anims](ObjId id, Args) -> std::uint32_t {
        clear_in_combat(m.kernel(), w, anims, id);
        return 0;
    });
    // Actor::I_doAnim(npc, action, dir, max_queued, replace) (10F0:0245): the pid.
    bind({0x039}, [&m, &w, &anims](ObjId id, Args a) -> std::uint32_t {
        return do_anim(m.kernel(), w, anims, id, arg16(a, 4), arg16(a, 6) & 0xFF, arg16(a, 8),
                       (arg16(a, 10) & 0xFF) != 0);
    });
    // Actor::I_turnToward(npc, dir, always) (10F0:326B): SPRITE_1150_009C in
    // the combat or relaxed stance as the NPC is; the last pid.
    bind({0x00E}, [&m, &w, &anims](ObjId id, Args a) -> std::uint32_t {
        return turn_to(m.kernel(), w, anims, id, arg16(a, 4) & 0xFF, (arg16(a, 6) & 0xFF) != 0, false);
    });
    // Actor::I_getUnkByte(npc) (10F0:3DFC): record +0x63, the quality low of
    // the egg that made the NPC (a camera monitor takes control of the NPC
    // whose byte is its own quality low).
    bind({0x00B}, [&w](ObjId id, Args) -> std::uint32_t { return w.npc_record(id)[npc::kEggQuality]; });
    // Actor::I_setMana(npc, value) (10F0:346E): record +0x57, the energy.
    bind({0x084}, [&w](ObjId id, Args a) -> std::uint32_t {
        w.set_npc_word(id, 0x57, arg16(a, 4));
        return 0;
    });
    // Actor::I_setImmortal / I_clrImmortal (10F0:216A / 2185): record +0x13
    // bit 0x02, which cancels damage (projectile.cpp).
    bind({0x086}, [&w](ObjId id, Args) -> std::uint32_t {
        w.npc_record(id)[npc::kFlags13] |= 0x02;
        return 0;
    });
    bind({0x07C, 0x12A}, [&w](ObjId id, Args) -> std::uint32_t {
        w.npc_record(id)[npc::kFlags13] &= static_cast<std::uint8_t>(~0x02);
        return 0;
    });
    // Item::I_explode(item, size, destroy) (1150:0859): with splash damage.
    bind({0x017}, [&m, &w, &anims](ObjId id, Args a) -> std::uint32_t {
        explode(m.kernel(), w, anims, id, arg16(a, 4), (arg16(a, 6) & 0xFF) != 0, true);
        return 0;
    });
    // SpriteProcess::I_createSprite(shape, first, last, delay, x, y, z) (1140:00DE).
    m.set_intrinsic(0x0C7, [&w](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
        return create_sprite(machine.kernel(), w, arg16(a, 0), static_cast<std::int16_t>(arg16(a, 2)),
                             static_cast<std::int16_t>(arg16(a, 4)), arg16(a, 6),
                             World::Point{arg16(a, 8), arg16(a, 10), arg16(a, 12) & 0xFF});
    });
    bound.push_back(0x0C7);
    // MainActor::I_getMaxEnergy (10F0:348B).
    bind({0x082}, [&w](ObjId id, Args) -> std::uint32_t { return static_cast<std::uint32_t>(max_energy(w, id)); });
    // BatteryChargerProcess::I_create, CruHealerProcess::I_create (1130:20C5,
    // 20E5): the pid.
    m.set_intrinsic(0x0EE, [&w](UCMachine& machine, UCProcess&, Args) -> std::uint32_t {
        return start_battery_charger(machine.kernel(), w);
    });
    m.set_intrinsic(0x0F6, [&w](UCMachine& machine, UCProcess&, Args) -> std::uint32_t {
        return start_healer(machine.kernel(), w);
    });
    bound.insert(bound.end(), {0x0EE, 0x0F6});
    // Actor::I_teleport(npc, x, y, z, map) (10F0:29FA).
    bind({0x065}, [&w](ObjId id, Args a) -> std::uint32_t {
        w.teleport(id, arg16(a, 4), arg16(a, 6), arg16(a, 8) & 0xFF, arg16(a, 10) & 0xFF);
        return 0;
    });
    // Actor::I_setEquip(npc, slot, item) (10F0:27FB).
    bind({0x0E0}, [&w](ObjId id, Args a) -> std::uint32_t { return set_equip(w, id, arg16(a, 4), arg16(a, 6)); });
    // MainActor::I_teleportToEgg(map, egg, wait) (1098:04CE) and (egg, wait)
    // on the current map (1098:04F7): the pid. MainActor::I_switchMap(map)
    // (1028:049C).
    m.set_intrinsic(0x07A, [&w](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
        return start_teleport_to_egg(machine.kernel(), w, arg16(a, 0), arg16(a, 2), (arg16(a, 4) & 0xFF) != 0);
    });
    m.set_intrinsic(0x097, [&w](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
        return start_teleport_to_egg(machine.kernel(), w, w.current_map(), arg16(a, 0), (arg16(a, 2) & 0xFF) != 0);
    });
    m.set_intrinsic(0x0CC, [&w](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
        start_switch_map(machine.kernel(), w, arg16(a, 0));
        return 0;
    });
    // MainActor::I_addItemCru(item, show) (13E8:0330): the item id, not a pointer.
    m.set_intrinsic(0x03A, [&w](UCMachine&, UCProcess&, Args a) -> std::uint32_t {
        return add_item_cru(w, arg16(a, 0), (arg16(a, 2) & 0xFF) != 0);
    });
    m.set_intrinsic(0x0B9, [&w](UCMachine&, UCProcess&, Args a) -> std::uint32_t {
        return add_item_cru(w, arg16(a, 0), (arg16(a, 2) & 0xFF) != 0);
    });
    // Item::I_setQAndCombine(item, quality) (10A8:0DD1).
    m.set_intrinsic(0x0CF, [&w](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
        std::array<std::uint8_t, 2> id{};
        const std::uint32_t ptr = arg16(a, 0) | std::uint32_t{arg16(a, 2)} << 16;
        if (machine.dereference(ptr, id)) set_quality_and_combine(w, static_cast<ObjId>(id[0] | id[1] << 8), arg16(a, 4));
        return 0;
    });
    // MainActor::I_clrKeycards (10F0:3D4B), I_hasKeycard(n) (10F0:3CE8).
    m.set_intrinsic(0x079, [&w](UCMachine&, UCProcess&, Args) -> std::uint32_t {
        w.state.keycards = 0;
        return 0;
    });
    m.set_intrinsic(0x0FD, [&w](UCMachine&, UCProcess&, Args a) -> std::uint32_t {
        return (w.state.keycards >> (arg16(a, 0) & 0x1F)) & 1;
    });
    bound.insert(bound.end(), {0x07A, 0x097, 0x0CC, 0x03A, 0x0B9, 0x0CF, 0x079, 0x0FD});
    // World::I_setControlledNPCNum(npc) (1138:0000).
    m.set_intrinsic(0x058, [&w, &anims](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
        set_controlled_npc(machine.kernel(), w, anims, arg16(a, 0));
        return 0;
    });
    bound.push_back(0x058);
    // Actor::I_getHp (10F0:32F4): record +0.
    bind({0x07D}, [&w](ObjId id, Args) -> std::uint32_t { return w.npc_hp(id); });
    // Actor::I_createActorCru(egg, kind) (10A8:3D3F): the new NPC or 0.
    if (types) {
        // Actor::I_addHp(npc, hp) (10F0:3748): 0 when the hit points are at
        // the maximum (NPC_10f0_3375: 150 for the player, else the NPC
        // type's), else raised up to it, 1.
        bind({0x0CB}, [&w, types](ObjId id, Args a) -> std::uint32_t {
            const Item* npc = w.get(id);
            const int max = id == 1 ? 150 : types->max_hp(types->index(npc ? npc->shape : 0));
            const int hp = static_cast<std::int16_t>(w.npc_hp(id));
            if (hp == max) return 0;
            w.set_npc_word(id, 0x00, static_cast<std::uint16_t>(std::min(max, hp + static_cast<std::int16_t>(arg16(a, 4)))));
            return 1;
        });
        m.set_intrinsic(0x129, [&w, &anims, types](UCMachine& machine, UCProcess&, Args a) -> std::uint32_t {
            std::array<std::uint8_t, 2> id{};
            const std::uint32_t ptr = arg16(a, 0) | std::uint32_t{arg16(a, 2)} << 16;
            if (!machine.dereference(ptr, id)) return 0;
            return create_actor(machine.kernel(), w, anims, *types, static_cast<ObjId>(id[0] | id[1] << 8), arg16(a, 4));
        });
        bound.push_back(0x129);
    }
    return bound;
}

}  // namespace crusader
