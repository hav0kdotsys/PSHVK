#pragma once

#include "imgui.h"

// Lightweight binding helpers to pass emissive texture data through ImGui draw commands.
// The struct is intentionally plain-old-data so it can be cast to ImTextureID and later
// decoded by the renderer backends.
struct HvkEmissiveBinding
{
    static constexpr unsigned int kMagic = 0x48564B45; // "HVKE"

    unsigned int Magic = kMagic;
    ImTextureID   BaseTexture = nullptr;
    ImTextureID   EmissiveTexture = nullptr;
    float         EmissiveStrength = 0.0f;
    bool          Additive = false;
};

inline bool ImTextureIdHasEmissive(const ImTextureID id)
{
    const HvkEmissiveBinding* binding = reinterpret_cast<const HvkEmissiveBinding*>(id);
    return binding != nullptr && binding->Magic == HvkEmissiveBinding::kMagic;
}

