#include <MountWheel.h>
#include <MumbleLink.h>
#include <Wheel.h>

namespace GW2Radial
{
MountWheel::MountWheel(std::shared_ptr<Texture2D> bgTexture)
    : Wheel(std::move(bgTexture), "mounts", "Mounts")
    , quickDismountOption_("Quick dismount", "quick_dismount", "wheel_" + nickname_, true)
    , dismountKeybind_("dismount", "Dismount", "wheel_" + nickname_)
{
    IterateEnum<MountType>(
        [&](auto i)
        {
            AddElement(std::make_unique<WheelElement>(static_cast<u32>(i), std::string("mount_") + GetMountNicknameFromType(i), "Mounts", GetMountNameFromType(i),
                                                      GetMountColorFromType(i), GetMountPropsFromType(i)));
        });
}

glm::vec4 MountWheel::GetMountColorFromType(MountType m)
{
    switch (m)
    {
        case MountType::Raptor:
            return { 213 / 255.f, 100 / 255.f, 89 / 255.f, 1 };
        case MountType::Springer:
            return { 212 / 255.f, 198 / 255.f, 94 / 255.f, 1 };
        case MountType::Skimmer:
            return { 108 / 255.f, 128 / 255.f, 213 / 255.f, 1 };
        case MountType::Jackal:
            return { 120 / 255.f, 183 / 255.f, 197 / 255.f, 1 };
        case MountType::Griffon:
            return { 136 / 255.f, 123 / 255.f, 195 / 255.f, 1 };
        case MountType::Beetle:
            return { 199 / 255.f, 131 / 255.f, 68 / 255.f, 1 };
        case MountType::Warclaw:
            return { 181 / 255.f, 255 / 255.f, 244 / 255.f, 1 };
        case MountType::Skyscale:
            return { 211 / 255.f, 142 / 255.f, 244 / 255.f, 1 };
        case MountType::Turtle:
            return { 56 / 255.f, 228 / 255.f, 85 / 255.f, 1 };
        case MountType::Skiff:
            return { 255 / 255.f, 255 / 255.f, 255 / 255.f, 1 };
        default:
            return { 1, 1, 1, 1 };
    }
}

void MountWheel::MenuSectionKeybinds(Keybind** currentlyModifiedKeybind)
{
    ImGui::KeybindInput(dismountKeybind_, currentlyModifiedKeybind, "Mount/Dismount keybind in game. If set, used to dismount instead of sending an arbitrary mount key.",
                        false); // single-bind only
}

void MountWheel::MenuSectionInteraction()
{
    ImGui::ConfigurationWrapper(&ImGui::Checkbox, quickDismountOption_);
    UI::HelpTooltip("If enabled, using any keybind while mounted will directly send a mount keybind to dismount without showing the radial menu.");
}

bool MountWheel::BypassCheck(WheelElement*& we, Keybind*& kb)
{
    const auto& mumble = MumbleLink::i();
    if (quickDismountOption_.value() && mumble.isMounted())
    {
        if (previousUsed_ != nullptr)
            we = previousUsed_;
        else
        {
            const auto& activeElems = GetUsableElements(mumble.currentState());
            if (!activeElems.empty())
                we = activeElems.front();
        }

        if (dismountKeybind_.isSet())
            kb = &dismountKeybind_;

        if (we || kb)
            return true;

        return false;
    }

    return false;
}

bool MountWheel::ResetMouseCheck(WheelElement* we)
{
    return we == wheelElements_[MountIndex(MountType::Skiff)].get();
}

} // namespace GW2Radial
