/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2003 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/EiKeyState.h"
#include "platform/XWindowsUtil.h"

#include "base/Log.h"
#include "common/stdmap.h"

#include <cstddef>
#include <algorithm>
#include <memory>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

static const size_t ModifiersFromXDefaultSize = 32;

EiKeyState::EiKeyState(EiScreen* impl, IEventQueue* events) :
    KeyState(events)
{
    m_impl = impl;
    m_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

void
EiKeyState::initDefaultKeymap()
{
    const struct xkb_rule_names names = {
        NULL, // Use libxkbcommon compile-time defaults/env vars
    };

    m_xkb_keymap = xkb_keymap_new_from_names(m_xkb, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

void
EiKeyState::init(int fd, size_t len)
{
    auto buffer = std::make_unique<char[]>(len + 1);
    buffer[len] = '\0'; // guarantee null-termination
    auto sz = read(fd, buffer.get(), len);

    if ((size_t)sz < len) {
        LOG((CLOG_NOTE "Failed to create XKB context: %s", strerror(errno)));
        return;
    }

    auto keymap = xkb_keymap_new_from_buffer(m_xkb, buffer.get(), len,
                                             XKB_KEYMAP_FORMAT_TEXT_V1,
                                             XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        LOG((CLOG_NOTE "Failed to compile keymap, falling back to defaults"));
        // Falling back to layout "us" is a lot more useful than segfaulting
        initDefaultKeymap();
        return;
    }

    m_xkb_keymap = keymap;
}


EiKeyState::~EiKeyState()
{
    xkb_context_unref(m_xkb);
    xkb_keymap_unref(m_xkb_keymap);
}

bool
EiKeyState::fakeCtrlAltDel()
{
    // pass keys through unchanged
    return false;
}

KeyModifierMask
EiKeyState::pollActiveModifiers() const
{
    // FIXME
    return 0;
}

int32_t
EiKeyState::pollActiveGroup() const
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
    return 0;
}

void
EiKeyState::pollPressedKeys(KeyButtonSet& pressedKeys) const
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
    return;
}

uint32_t
EiKeyState::convertModMask(uint32_t xkb_mask)
{
    uint32_t barrier_mask = 0;

    for (auto xkbmod = 0; xkbmod < xkb_keymap_num_mods(m_xkb_keymap); xkbmod++) {
        if ((xkb_mask & (1 << xkbmod)) == 0)
            continue;

        const char *name = xkb_keymap_mod_get_name(m_xkb_keymap, xkbmod);
        if (strcmp(XKB_MOD_NAME_SHIFT, name) == 0)
            barrier_mask |= (1 << kKeyModifierBitShift);
        else if (strcmp(XKB_MOD_NAME_CAPS, name) == 0)
            barrier_mask |= (1 << kKeyModifierBitCapsLock);
        else if (strcmp(XKB_MOD_NAME_CTRL, name) == 0)
            barrier_mask |= (1 << kKeyModifierBitControl);
        else if (strcmp(XKB_MOD_NAME_ALT, name) == 0)
            barrier_mask |= (1 << kKeyModifierBitAlt);
    }

    return barrier_mask;
}

// Only way to figure out whether a key is a modifier key is to press it,
// check if a modifier changed state and then release it again.
// Luckily xkbcommon allows us to do this in a separate
void
EiKeyState::assignGeneratedModifiers(uint32_t keycode, inputleap::KeyMap::KeyItem &item)
{
    uint32_t mods_generates = 0;
    auto state = xkb_state_new(m_xkb_keymap);
    enum xkb_state_component changed = xkb_state_update_key(state, keycode, XKB_KEY_DOWN);

    if (changed) {
        for (auto m = 0; m < xkb_keymap_num_mods(m_xkb_keymap); m++) {
            if (xkb_state_mod_index_is_active(state, m, XKB_STATE_MODS_LOCKED))
                item.m_lock = true;

            if (xkb_state_mod_index_is_active(state, m, XKB_STATE_MODS_EFFECTIVE)) {
                mods_generates |= (1 << m);
            }
        }
    }
    xkb_state_update_key(state, keycode, XKB_KEY_UP);

    // If we locked a modifier, press again to hopefully unlock
    if (changed & (XKB_STATE_MODS_LOCKED|XKB_STATE_MODS_LATCHED)) {
        xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
        xkb_state_update_key(state, keycode, XKB_KEY_UP);
    }
    xkb_state_unref(state);

    item.m_generates = convertModMask(mods_generates);
}

void
EiKeyState::getKeyMap(inputleap::KeyMap& keyMap)
{
    inputleap::KeyMap::KeyItem item;

    auto min_keycode = xkb_keymap_min_keycode(m_xkb_keymap);
    auto max_keycode = xkb_keymap_max_keycode(m_xkb_keymap);

    // X keycodes are evdev keycodes + 8 (libei gives us evdev keycodes)
    for (auto keycode = min_keycode; keycode <= max_keycode; keycode++) {

        // skip keys with no groups (they generate no symbols)
        if (xkb_keymap_num_layouts_for_key(m_xkb_keymap, keycode) == 0)
            continue;

        for (auto group = 0U; group < xkb_keymap_num_layouts(m_xkb_keymap); group++) {
            item.m_group = group;

            for (auto level = 0U;
                 level < xkb_keymap_num_levels_for_key(m_xkb_keymap, keycode, group);
                 level++) {
                const xkb_keysym_t *syms;
                xkb_mod_mask_t masks[64];
                auto nmasks = xkb_keymap_key_get_mods_for_level(m_xkb_keymap, keycode, group,
                                                                level, masks, 64);
                auto nsyms = xkb_keymap_key_get_syms_by_level(m_xkb_keymap, keycode, group, level, &syms);

                if (nsyms == 0)
                    continue;

                if (nsyms > 1)
                    LOG((CLOG_WARN " Multiple keysyms per keycode are not supported, keycode %d", keycode));

                xkb_keysym_t keysym = syms[0];
                KeySym sym = static_cast<KeyID>(keysym);
                item.m_id = XWindowsUtil::mapKeySymToKeyID(sym);
                item.m_button   = static_cast<KeyButton>(keycode) - 8; // X keycode offset
                item.m_client   = 0;
                item.m_sensitive = 0;

                // For debugging only
                char keysym_name[128] = {0};
                xkb_keysym_get_name(keysym, keysym_name, sizeof(keysym_name));

                // Set to all modifiers this key may be affected by
                uint32_t mods_sensitive = 0;
                for (auto n = 0U; n < nmasks; n++) {
                    mods_sensitive |= masks[n];
                }
                item.m_sensitive = convertModMask(mods_sensitive);

                uint32_t mods_required = 0;
                for (auto m = 0; m < nmasks; m++) {
                    mods_required = masks[m];
                    if (keycode < 13) {
                        printf("..... %d: %#x (%s) modmask: %#06x (sensitive: %#06x)\n",
                               keycode, keysym, keysym_name,
                               mods_required,
                               mods_sensitive);
                    }

                }
                item.m_required = convertModMask(mods_required);

                assignGeneratedModifiers(keycode, item);

                keyMap.addKeyEntry(item);
            }
        }
    }

    // allow composition across groups
    keyMap.allowGroupSwitchDuringCompose();
}

void
EiKeyState::fakeKey(const Keystroke& keystroke)
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    switch (keystroke.m_type) {
    case Keystroke::kButton:
        LOG((CLOG_DEBUG1 "  %03x (%08x) %s", keystroke.m_data.m_button.m_button,
             keystroke.m_data.m_button.m_client,
             keystroke.m_data.m_button.m_press ? "down" : "up"));
        m_impl->fakeKey(keystroke.m_data.m_button.m_button,
                        keystroke.m_data.m_button.m_press);
        break;
    default:
        break;
    }
}
