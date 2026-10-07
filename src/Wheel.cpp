#include <Core.h>
#include <GFXSettings.h>
#include <ImGuiExtensions.h>
#include <ImGuiPopup.h>
#include <Input.h>
#include <Main.h>
#include <MumbleLink.h>
#include <ShaderManager.h>
#include <Utility.h>
#include <Wheel.h>
#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace GW2Radial
{
ConstantBufferWPtr<Wheel::WheelCB> Wheel::cb_s;

Wheel::Favorite                    Wheel::MakeDefaultFavorite()
{
    Favorite fav;
    fav.bits.baseline   = 0;
    fav.bits.inWvW      = -1;
    fav.bits.inCombat   = -1;
    fav.bits.onWater    = -1;
    fav.bits.underwater = -1;
    return fav;
}

Wheel::Wheel(std::shared_ptr<Texture2D> bgTexture, std::string nickname, std::string displayName)
    : nickname_(std::move(nickname))
    , displayName_(std::move(displayName))
    , keybind_(nickname_, "Show on mouse", nickname_)
    , centralKeybind_(nickname_ + "_cl", "Show in center", nickname_)
    , centerBehaviorOption_("Center behavior", "center_behavior", "wheel_" + nickname_)
    , centerFavoriteOption_("Favorite choice##Center", "center_favorite.2", "wheel_" + nickname_, MakeDefaultFavorite())
    , delayFavoriteOption_("Favorite choice##Delay", "delay_favorite", "wheel_" + nickname_, MakeDefaultFavorite())
    , scaleOption_("Scale", "scale", "wheel_" + nickname_, 1.f)
    , centerScaleOption_("Center scale", "center_scale", "wheel_" + nickname_, 0.2f)
    , displayDelayOption_("Pop-up delay", "delay", "wheel_" + nickname_)
    , animationTimeOption_("Fade-in time", "anim_time", "wheel_" + nickname_, 750)
    , resetCursorOnLockedKeybindOption_("Reset cursor to center with Center Locked keybind", "reset_cursor_cl", "wheel_" + nickname_, true)
    , lockCameraWhenOverlayedOption_("Lock camera when overlay is displayed", "lock_camera", "wheel_" + nickname_, true)
    , showOverGameUIOption_("Show on top of game UI", "show_over_ui", "wheel_" + nickname_, true)
    , noHoldOption_("Activate first hovered option without holding down", "no_hold", "wheel_" + nickname_, false)
    , clickSelectOption_("Require click on option to select", "click_select", "wheel_" + nickname_, false)
    , behaviorOnReleaseBeforeDelay_("Behavior when released before delay has lapsed", "behavior_before_delay", "wheel_" + nickname_)
    , resetCursorAfterKeybindOption_("Move cursor to original location after release", "reset_cursor_after", "wheel_" + nickname_, true)
    , enableConditionsOption_("Enable conditional keybinds", "conditions_enabled", "wheel_" + nickname_, false)
    , enableSkipOWOption_("Enable fast on water mode", "skip_ow_enabled", "wheel_" + nickname_, true)
    , enableSkipUWOption_("Enable fast underwater mode", "skip_uw_enabled", "wheel_" + nickname_, true)
    , enableSkipWvWOption_("Enable fast WvW mode", "skip_wvw_enabled", "wheel_" + nickname_, true)
    , visibleInMenuOption_(displayName_ + "##Visible", "menu_visible", "wheel_" + nickname_, true)
    , opacityMultiplierOption_("Opacity multiplier", "opacity", "wheel_" + nickname_, 100)
    , animationScale_("Animation scale", "anim_scale", "wheel_" + nickname_, 1.f)
    , backgroundTexture_(bgTexture)
{
    conditions_ = std::make_shared<ConditionSet>("wheel_" + nickname_);
    conditions_->enable(enableConditionsOption_.value());
    keybind_.conditions(conditions_);
    centralKeybind_.conditions(conditions_);
    keybind_.callback([&](Activated a) { return KeybindEvent(false, a); });
    centralKeybind_.callback([&](Activated a) { return KeybindEvent(true, a); });

    mouseMoveCallbackID_   = Input::i().mouseMoveEvent().AddCallback([this](bool& rv) { OnMouseMove(rv); });
    mouseButtonCallbackID_ = Input::i().mouseButtonEvent().AddCallback([this](EventKey ek, bool& rv) { OnMouseButton(ek.sc, ek.down, rv); });

    SettingsMenu::i().AddImplementer(this);

    vs_                   = ShaderManager::i().GetShader(L"ScreenQuad.hlsl", D3D11_SHVER_VERTEX_SHADER, "ScreenQuad");
    psWheel_              = ShaderManager::i().GetShader(L"Wheel.hlsl", D3D11_SHVER_PIXEL_SHADER, "Wheel");
    psWheelElement_       = ShaderManager::i().GetShader(L"WheelElement.hlsl", D3D11_SHVER_PIXEL_SHADER, "WheelElement");
    psCursor_             = ShaderManager::i().GetShader(L"Cursor.hlsl", D3D11_SHVER_PIXEL_SHADER, "Cursor");
    auto              dev = Core::i().device();

    CD3D11_BLEND_DESC blendDesc(D3D11_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable    = true;
    blendDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    GW2_CHECKED_HRESULT(dev->CreateBlendState(&blendDesc, blendState_.GetAddressOf()));

    CD3D11_SAMPLER_DESC sampDesc(D3D11_DEFAULT);
    GW2_CHECKED_HRESULT(dev->CreateSamplerState(&sampDesc, baseSampler_.GetAddressOf()));

    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    std::fill_n(sampDesc.BorderColor, std::size(sampDesc.BorderColor), 0.f);
    GW2_CHECKED_HRESULT(dev->CreateSamplerState(&sampDesc, borderSampler_.GetAddressOf()));

    if (auto cb = cb_s.lock())
        cb_ = cb;
    else
    {
        cb_  = ShaderManager::i().MakeConstantBuffer<WheelCB>();
        cb_s = cb_;
    }
}

Wheel::~Wheel()
{
    Input::f(
        [&](auto& i)
        {
            // A FastWithRangeIndicator selection may have pressed-and-held the underlying keybind,
            // waiting on the player's confirm click -- release it now instead of leaving it stuck
            // held in GW2.
            if (pendingGroundTargetKeybind_ && pendingGroundTargetAction_ == KeybindAction::Up)
                for (const auto& kc : pendingGroundTargetKeybind_->keyCombos())
                    if (kc.key() != ScanCode::None)
                        i.SendKeybind(kc, std::nullopt, KeybindAction::Up);

            i.mouseMoveEvent().RemoveCallback(std::move(mouseMoveCallbackID_));
            i.mouseButtonEvent().RemoveCallback(std::move(mouseButtonCallbackID_));
        });
    SettingsMenu::f([&](auto& i) { i.RemoveImplementer(this); });
}

void Wheel::UpdateHover()
{
    const auto& io = ImGui::GetIO();

    glm::vec2   mousePos;
    mousePos.x = io.MousePos.x / float(Core::i().screenWidth());
    mousePos.y = io.MousePos.y / float(Core::i().screenHeight());
    mousePos.x -= currentPosition_.x;
    mousePos.y -= currentPosition_.y;

    mousePos.y *= float(Core::i().screenHeight()) / float(Core::i().screenWidth());

    WheelElement* lastHovered    = currentHovered_;

    auto          activeElements = GetVisibleElements(MumbleLink::i().currentState());

    float         mpLenSq        = mousePos.x * mousePos.x + mousePos.y * mousePos.y;

    // Middle circle does not count as a hover event
    if (!activeElements.empty() && mpLenSq > Square(scaleOption_.value() * 0.125f * 0.8f * centerScaleOption_.value()))
    {
        float mouseAngle = atan2(-mousePos.y, -mousePos.x) - 0.5f * float(M_PI);
        if (mouseAngle < 0)
            mouseAngle += float(2 * M_PI);

        const float elementAngle = float(2 * M_PI) / activeElements.size();
        const int   elementId    = int((mouseAngle - elementAngle / 2) / elementAngle + 1) % int(activeElements.size());

        currentHovered_          = activeElements[elementId];
    }
    else
        currentHovered_ = GetCenterHoveredElement();

    if (lastHovered != currentHovered_)
    {
        const auto time = TimeInMilliseconds();

        if (lastHovered)
            lastHovered->currentExitTime(time);
        if (currentHovered_)
            currentHovered_->currentHoverTime(time);
    }
}

void Wheel::DrawMenu(Keybind** currentEditedKeybind)
{
    ImGui::PushID((nickname_ + "Elements").c_str());

    UI::Title("In-game Keybinds");

    ImGui::TextUnformatted("Set the following to your in-game keybinds (F11, Control Options).");

    for (auto& we : wheelElements_)
        ImGui::KeybindInput(we->keybind(), currentEditedKeybind, nullptr, allowMultipleBinds_);

    UI::Title("Keybinds");

    ImGui::KeybindInput((Keybind&)keybind_, currentEditedKeybind, "Pressing this key combination will open the radial menu at your cursor's current location.",
                        false); // single-bind only � opens the wheel
    ImGui::KeybindInput((Keybind&)centralKeybind_, currentEditedKeybind,
                        "Pressing this key combination will open the radial menu in the middle of the screen. Your cursor will be moved to the middle of the screen and moved back "
                        "after you have selected an option.",
                        false); // single-bind only � opens the wheel

    MenuSectionKeybinds(currentEditedKeybind);

    if (ImGui::ConfigurationWrapper(&ImGui::Checkbox, enableConditionsOption_) && conditions_)
        conditions_->enable(enableConditionsOption_.value());

    {
        UI::Scoped::Disable disable(!conditions_ || !enableConditionsOption_.value());

        ImGui::Indent();

        conditions_->DrawMenu();

        ImGui::Unindent();
    }

    UI::Title("Display Options");

    ImGui::ConfigurationWrapper(&ImGui::SliderInt, opacityMultiplierOption_, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp);
    UI::HelpTooltip("Transparency of the entire overlay. Setting to 0% hides it entirely.");

    ImGui::ConfigurationWrapper(&ImGui::SliderInt, animationTimeOption_, 0, 2000, "%d ms", ImGuiSliderFlags_AlwaysClamp);
    UI::HelpTooltip("Amount of time, in milliseconds, for the radial menu to fade in.");

    ImGui::ConfigurationWrapper(&ImGui::SliderFloat, animationScale_, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    UI::HelpTooltip("Intensity of the 3D animations.");

    ImGui::ConfigurationWrapper(&ImGui::SliderFloat, scaleOption_, 0.25f, 4.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    UI::HelpTooltip("Scale factor for the size of the whole radial menu.");

    ImGui::ConfigurationWrapper(&ImGui::SliderFloat, centerScaleOption_, 0.05f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    UI::HelpTooltip("Scale factor for the size of just the central region of the radial menu.");

    ImGui::ConfigurationWrapper(&ImGui::SliderInt, displayDelayOption_, 0, 3000, "%d ms", ImGuiSliderFlags_AlwaysClamp);
    UI::HelpTooltip("Amount of time, in milliseconds, to wait before displaying the radial menu. The input bound to the central region can still be sent by releasing the key, "
                    "even before the menu is visible.");

    ImGui::ConfigurationWrapper(&ImGui::Checkbox, showOverGameUIOption_);
    UI::HelpTooltip("Either show the radial menu over or under the game's UI.");

    MenuSectionDisplay();

    UI::Title("Interaction Options");

    {
        UI::Scoped::Disable disable(clickSelectOption_.value());
        if (clickSelectOption_.value())
            noHoldOption_.value() = false;

        ImGui::ConfigurationWrapper(&ImGui::Checkbox, noHoldOption_);
        UI::HelpTooltip("This option will activate whichever option is hovered first. Any key pressed to activate the menu can be released immediately without dismissing it. "
                        "Mutually exclusive with \"click to select\".");
    }

    {
        UI::Scoped::Disable disable(noHoldOption_.value());
        if (noHoldOption_.value())
            clickSelectOption_.value() = false;

        ImGui::ConfigurationWrapper(&ImGui::Checkbox, clickSelectOption_);
        UI::HelpTooltip("This option will only activate an option when it is clicked on. Any key pressed to activate the menu can be released immediately without dismissing it. "
                        "Mutually exclusive with \"hover to select\".");
    }

    auto favoriteCombo = [](const std::vector<std::unique_ptr<WheelElement>>& elements, ConfigurationOption<Favorite>& opt)
    {
        Favorite fav    = opt.value();

        auto     single = [&opt](int v, const char* prefix, const char* tooltip, std::ranges::forward_range auto&& elements, bool showDefault = true)
        {
            static const char* defaultString = "(default value)";
            if (ImGui::BeginCombo((prefix + opt.displayName()).c_str(),
                                  v >= 0 ? std::get<1>(*std::ranges::next(std::ranges::begin(elements), v))->displayName().c_str() : defaultString))
            {
                if (showDefault)
                    if (ImGui::Selectable(defaultString, v == -1))
                        v = -1;

                for (auto&& [index, element] : elements)
                {
                    if (ImGui::Selectable(element->displayName().c_str(), index == v))
                        v = static_cast<int>(index);
                }
                ImGui::EndCombo();
            }
            UI::HelpTooltip(tooltip);

            return v;
        };

        namespace rv      = std::ranges::views;

        const auto& all   = elements | rv::enumerate;
        auto        wvw   = elements | rv::enumerate | rv::filter([](auto&& e) { return std::get<1>(e)->isUsable(ConditionalState::InWvW); });

        fav.bits.baseline = single(fav.bits.baseline, "Default ", "This determines the default favorite option.", all, false);
        if (!std::ranges::empty(wvw | rv::drop(1)))
            fav.bits.inWvW = single(fav.bits.inWvW, "In WvW ", "This determines the favorite option when in WvW.", wvw);
        else
            fav.bits.inWvW = 0;
        fav.bits.inCombat   = single(fav.bits.inCombat, "In combat ", "This determines the favorite option when in combat.", all);
        fav.bits.onWater    = single(fav.bits.onWater, "On water ", "This determines the favorite option when on the water surface.", all);
        fav.bits.underwater = single(fav.bits.underwater, "Underwater ", "This determines the favorite option when underwater.", all);

        if (fav.value != opt.value().value)
            opt.value(fav);
    };

    {
        UI::Scoped::Disable disable(displayDelayOption_.value() == 0);

        ImGui::Text((behaviorOnReleaseBeforeDelay_.displayName() + ":").c_str());

        bool (*rb)(const char*, int*, int) = &ImGui::RadioButton;
        ImGui::ConfigurationWrapper(rb, "Nothing##ReleaseBeforeDelay", behaviorOnReleaseBeforeDelay_, int(BehaviorBeforeDelay::Nothing));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "Previous##ReleaseBeforeDelay", behaviorOnReleaseBeforeDelay_, int(BehaviorBeforeDelay::Previous));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "Favorite##ReleaseBeforeDelay", behaviorOnReleaseBeforeDelay_, int(BehaviorBeforeDelay::Favorite));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "As if opened##ReleaseBeforeDelay", behaviorOnReleaseBeforeDelay_, int(BehaviorBeforeDelay::Direction));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "Pass to game##ReleaseBeforeDelay", behaviorOnReleaseBeforeDelay_, int(BehaviorBeforeDelay::PassToGame));
        UI::HelpTooltip(
            "Determines the behavior of the menu if it is dismissed before it becomes visible. By default, it does nothing, but it can also (1) trigger the last selected item; "
            "(2) trigger a fixed \"favorite\" option; (3) function exactly as if the menu was opened, making it possible to select an item with the mouse without seeing them; "
            "(4) forward the input to the game.");

        if (BehaviorBeforeDelay(behaviorOnReleaseBeforeDelay_.value()) == BehaviorBeforeDelay::Favorite)
        {
            const auto textSize = ImGui::CalcTextSize(delayFavoriteOption_.displayName().c_str());
            const auto itemSize = ImGui::CalcItemWidth() - textSize.x - ImGui::GetCurrentWindowRead()->WindowPadding.x;

            favoriteCombo(wheelElements_, delayFavoriteOption_);
        }
    }

    {
        UI::Scoped::Disable disable(noHoldOption_.value());

        ImGui::Text((centerBehaviorOption_.displayName() + ":").c_str());
        ImGui::SameLine();

        bool (*rb)(const char*, int*, int) = &ImGui::RadioButton;
        ImGui::ConfigurationWrapper(rb, "Nothing##CenterBehavior", centerBehaviorOption_, int(CenterBehavior::Nothing));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "Previous##CenterBehavior", centerBehaviorOption_, int(CenterBehavior::Previous));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "Favorite##CenterBehavior", centerBehaviorOption_, int(CenterBehavior::Favorite));
        ImGui::SameLine();
        ImGui::ConfigurationWrapper(rb, "Pass to game##CenterBehavior", centerBehaviorOption_, int(CenterBehavior::PassToGame));
        UI::HelpTooltip("Determines the behavior of the central region of the menu. By default, it does nothing, but it can alternatively: "
                        "(1) trigger the last selected item; "
                        "(2) trigger a fixed \"favorite\" option;"
                        "(3) forward the input to the game.");

        if (CenterBehavior(centerBehaviorOption_.value()) == CenterBehavior::Favorite)
        {
            const auto textSize = ImGui::CalcTextSize(centerFavoriteOption_.displayName().c_str());
            const auto itemSize = ImGui::CalcItemWidth() - textSize.x - ImGui::GetCurrentWindowRead()->WindowPadding.x;

            favoriteCombo(wheelElements_, centerFavoriteOption_);
        }
    }

    ImGui::ConfigurationWrapper(&ImGui::Checkbox, resetCursorOnLockedKeybindOption_);
    UI::HelpTooltip("Moves the cursor to the center of the screen when the \"show in center\" keybind is used.");

    ImGui::ConfigurationWrapper(&ImGui::Checkbox, lockCameraWhenOverlayedOption_);
    UI::HelpTooltip("Prevents the camera from being affected by mouse movements while the menu is displayed.");

    if (!alwaysResetCursorPositionBeforeKeyPress_)
    {
        ImGui::ConfigurationWrapper(&ImGui::Checkbox, resetCursorAfterKeybindOption_);
        UI::HelpTooltip("Once the menu is dismissed, moves the cursor to where it was on screen before the menu was displayed.");
    }

    MenuSectionInteraction();

    UI::Title("Fast Mode");

    ImGui::ConfigurationWrapper(&ImGui::Checkbox, enableSkipOWOption_);
    UI::HelpTooltip("If only one item is available on water (e.g., mounting while on water surface with Skimmer unlocked, but Turtle disabled), "
                    "bypass showing the radial menu and trigger the input immediately.");
    ImGui::ConfigurationWrapper(&ImGui::Checkbox, enableSkipUWOption_);
    UI::HelpTooltip("If only one item is available underwater (e.g., mounting while underwater with Skimmer unlocked and fully mastered, but Turtle disabled), "
                    "bypass showing the radial menu and trigger the input immediately.");
    ImGui::ConfigurationWrapper(&ImGui::Checkbox, enableSkipWvWOption_);
    UI::HelpTooltip("If only one item is available in WvW (e.g., mounting in WvW), bypass showing the radial menu and trigger the input immediately.");

    UI::Title("Visibility & Ordering");

    ImGui::Text("Ordering top to bottom is clockwise starting at noon.");

    ImGui::PushStyleColor(ImGuiCol_TableRowBg, 0);
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, (ImGui::GetColorU32(ImGuiCol_FrameBg) & 0xFFFFFF) | 0x33000000);
    if (ImGui::BeginTable("##OrderingTable", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Show/Use Conditions", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("##UpDown", ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableHeadersRow();

        for (auto it = sortedWheelElements_.begin(); it != sortedWheelElements_.end(); ++it)
        {
            const auto extremum = it == sortedWheelElements_.begin() ? 1 : it == std::prev(sortedWheelElements_.end()) ? -1 : 0;
            auto&      e        = *it;
            if (const auto dir = e->DrawPriority(extremum); dir != 0)
            {
                if (dir == 1 && e == sortedWheelElements_.front() || dir == -1 && e == sortedWheelElements_.back())
                    continue;

                auto& eOther = dir == 1 ? *std::prev(it) : *std::next(it);
                std::swap(e, eOther);
                const auto tempPriority = eOther->sortingPriority();
                eOther->sortingPriority(e->sortingPriority());
                e->sortingPriority(tempPriority);
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleColor(2);

    MenuSectionMisc();

    ImGui::PopID();
}

void Wheel::OnUpdate()
{
    if (showEmptyPopup_)
        ImGuiPopup("Radial menu missing keybinds")
            .Position({ 0.5f, 0.45f })
            .Size({ 400.f, 200.f }, false)
            .Display([&](const ImVec2&) { ImGui::TextWrapped("A radial menu was triggered, but no keybinds are currently bound for it, so nothing could be shown."); },
                     [&]() { showEmptyPopup_ = false; });
}

void Wheel::OnMapChange(u32 prevId, u32 newId) {}

void Wheel::OnCharacterChange(const std::wstring& prevCharacterName, const std::wstring& newCharacterName) {}

void Wheel::Draw(ID3D11DeviceContext* ctx)
{
    if (opacityMultiplierOption_.value() == 0)
        return;

    const int  screenWidth                 = Core::i().screenWidth();
    const int  screenHeight                = Core::i().screenHeight();

    glm::vec4  screenSize                  = { float(screenWidth), float(screenHeight), 1.f / screenWidth, 1.f / screenHeight };

    const auto currentTime                 = TimeInMilliseconds();

    const auto resetCursorPositionToCenter = [&]()
    {
        RECT rect = {};
        if (GetWindowRect(Core::i().gameWindow(), &rect))
        {
            if (SetCursorPos((rect.right - rect.left) / 2 + rect.left, (rect.bottom - rect.top) / 2 + rect.top))
            {
                auto& io      = ImGui::GetIO();
                io.MousePos.x = screenWidth * 0.5f;
                io.MousePos.y = screenHeight * 0.5f;
            }
        }
        resetCursorPositionToCenter_ = false;
    };

    if (behaviorOnReleaseBeforeDelay_.value() == int(BehaviorBeforeDelay::Direction) && resetCursorPositionToCenter_)
        resetCursorPositionToCenter();

    ID3D11SamplerState* samplers[] = { baseSampler_.Get(), baseSampler_.Get() };
    ctx->PSSetSamplers(0, 2, samplers);

    if (isVisible_)
    {
        if (currentTime >= currentTriggerTime_ + displayDelayOption_.value())
        {
            if (resetCursorPositionToCenter_)
                resetCursorPositionToCenter();

            D3D11_VIEWPORT vp;
            vp.TopLeftX = vp.TopLeftY = 0;
            vp.Width                  = float(screenWidth);
            vp.Height                 = float(screenHeight);
            vp.MinDepth               = 0.0f;
            vp.MaxDepth               = 1.0f;
            ctx->RSSetViewports(1, &vp);

            auto activeElements = GetVisibleElements(MumbleLink::i().currentState());
            if (!activeElements.empty())
            {
                glm::vec4 baseSpriteDimensions;
                baseSpriteDimensions.x = currentPosition_.x;
                baseSpriteDimensions.y = currentPosition_.y;
                baseSpriteDimensions.z = scaleOption_.value() * 0.5f * screenSize.y * screenSize.z;
                baseSpriteDimensions.w = scaleOption_.value() * 0.5f;

                const float fadeTimer  = std::min(1.f, (currentTime - (currentTriggerTime_ + displayDelayOption_.value())) / float(animationTimeOption_.value() * 0.5f));

                std::array<float, MaxHoverFadeIns> hoveredFadeIns;
                std::transform(activeElements.begin(), activeElements.end(), hoveredFadeIns.begin(),
                               [&](const WheelElement* elem) { return elem->hoverFadeIn(currentTime, this); });

                auto& lastHoverFadeIn = hoveredFadeIns[activeElements.size()];
                switch (CenterBehavior(centerBehaviorOption_.value()))
                {
                    case CenterBehavior::Previous:
                        if (previousUsed_)
                            lastHoverFadeIn = previousUsed_->hoverFadeIn(currentTime, this);
                        break;
                    case CenterBehavior::Favorite:
                    {
                        if (auto* fav = GetFavorite(centerFavoriteOption_.value()))
                        {
                            lastHoverFadeIn = fav->hoverFadeIn(currentTime, this);
                            break;
                        }
                    }
                        [[fallthrough]];
                    default:
                        lastHoverFadeIn = 0.f;
                        break;
                }

                for (u32 i = u32(activeElements.size()) + 1; i < MaxHoverFadeIns; i++)
                    hoveredFadeIns[i] = 0.f;

                ShaderManager::i().SetShaders(ctx, vs_, psWheel_);
                ctx->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffff);
                UpdateConstantBuffer(ctx, baseSpriteDimensions, fadeTimer, fmod(currentTime / 1010.f, 55000.f), activeElements, hoveredFadeIns, 0.f, false, true);

                ctx->PSSetShaderResources(0, 1, backgroundTexture_->srv.GetAddressOf());

                DrawScreenQuad(ctx);

                ShaderManager::i().SetShaders(ctx, vs_, psWheelElement_);
                ctx->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffff);

                int n = 0;
                for (auto it : activeElements)
                {
                    it->Draw(ctx, n, baseSpriteDimensions, activeElements.size(), currentTime, currentHovered_, this);
                    n++;
                }
            }

            {
                const auto& io = ImGui::GetIO();

                ShaderManager::i().SetShaders(ctx, vs_, psCursor_);
                ctx->OMSetBlendState(blendState_.Get(), nullptr, 0xffffffff);

                glm::vec4 spriteDimensions = { io.MousePos.x * screenSize.z, io.MousePos.y * screenSize.w, 0.08f * screenSize.y * screenSize.z, 0.08f };

                UpdateConstantBuffer(ctx, spriteDimensions);
                DrawScreenQuad(ctx);
            }
        }
    }

}

void Wheel::UpdateConstantBuffer(ID3D11DeviceContext* ctx, const glm::vec4& spriteDimensions, float fadeIn, float animationTimer, const std::vector<WheelElement*>& activeElements,
                                 const std::span<float>& hoveredFadeIns, float timeLeft, bool showIcon, bool tilt)
{
    auto& cb           = *cb_;
    cb->wipeMaskData   = wipeMaskData_;
    cb->wheelFadeIn    = fadeIn;
    cb->animationTimer = animationTimer;
    cb->centerScale    = centerScaleOption_.value();
    cb->elementCount   = int(activeElements.size());
    cb->globalOpacity  = opacityMultiplierOption_.value() * 0.01f;
    cb->timeLeft       = timeLeft;
    cb->showIcon       = showIcon;
    memcpy_s(cb->hoverFadeIns, sizeof(cb->hoverFadeIns), hoveredFadeIns.data(), MaxHoverFadeIns * sizeof(float));

    cb.Update(ctx);
    ctx->PSSetConstantBuffers(0, 1, cb.buffer().GetAddressOf());

    glm::mat4x4 tiltMatrix;
    if (tilt)
    {
        auto      mousePos = ImGui::GetIO().MousePos;
        glm::vec2 mouseDist{ currentPosition_.x - mousePos.x / float(Core::i().screenWidth()), currentPosition_.y - mousePos.y / float(Core::i().screenHeight()) };
        mouseDist = -mouseDist / glm::vec2(spriteDimensions.z, spriteDimensions.w);
        if (glm::length(mouseDist) > 0.2f)
            mouseDist *= 0.2f / glm::length(mouseDist);
        mouseDist *= 0.4f * animationScale_.value();

        tiltMatrix = glm::eulerAngleXY(-mouseDist.y, mouseDist.x);
    }
    else
        tiltMatrix = glm::identity<glm::mat4x4>();

    auto& vscb             = *Core::i().vertexCB();
    vscb->spriteDimensions = spriteDimensions;
    vscb->tiltMatrix       = tiltMatrix;
    vscb->spriteZ          = 0.f;
    vscb.Update(ctx);
    ctx->VSSetConstantBuffers(0, 1, vscb.buffer().GetAddressOf());
}

void Wheel::UpdateConstantBuffer(ID3D11DeviceContext* ctx, const glm::vec4& spriteDimensions)
{
    auto& vscb             = *Core::i().vertexCB();
    vscb->spriteDimensions = spriteDimensions;
    vscb->spriteZ          = 0.f;
    vscb.Update(ctx);
    ctx->VSSetConstantBuffers(0, 1, vscb.buffer().GetAddressOf());
}

void Wheel::OnFocusLost()
{
    currentHovered_     = nullptr;
    isVisible_          = false;
    currentTriggerTime_ = 0;
}

bool Wheel::CanActivate(const WheelElement* we) const
{
    const auto& mumble = MumbleLink::i();
    if (!mumble.gameHasFocus() || mumble.isMapOpen())
        return false;

    auto cs = mumble.currentState();

    return we->isUsable(cs);
}

void Wheel::Sort()
{
    sortedWheelElements_.resize(wheelElements_.size());
    std::ranges::transform(wheelElements_, sortedWheelElements_.begin(), [](auto& p) { return p.get(); });

    std::ranges::sort(sortedWheelElements_, [](const WheelElement* a, const WheelElement* b) { return a->sortingPriority() < b->sortingPriority(); });
    minElementSortingPriority_ = sortedWheelElements_.front()->sortingPriority();
}

WheelElement* Wheel::GetCenterHoveredElement()
{
    if (noHoldOption_.value())
        return nullptr;

    switch (CenterBehavior(centerBehaviorOption_.value()))
    {
        case CenterBehavior::Previous:
            return previousUsed_;
        case CenterBehavior::Favorite:
            return GetFavorite(centerFavoriteOption_.value());
        default:
            return nullptr;
    }
}

WheelElement* Wheel::GetFavorite(Favorite fav) const
{
    ConditionalState cs = MumbleLink::i().currentState();

    int              favoriteId;
    if (NotNone(cs & ConditionalState::InWvW) && fav.bits.inWvW != -1)
        favoriteId = fav.bits.inWvW;
    else if (NotNone(cs & ConditionalState::InCombat) && fav.bits.inCombat != -1)
        favoriteId = fav.bits.inCombat;
    else if (NotNone(cs & ConditionalState::OnWater) && fav.bits.onWater != -1)
        favoriteId = fav.bits.onWater;
    else if (NotNone(cs & ConditionalState::Underwater) && fav.bits.underwater != -1)
        favoriteId = fav.bits.underwater;
    else
        favoriteId = fav.bits.baseline;

    if (favoriteId < 0 || favoriteId >= int(wheelElements_.size()))
        return nullptr;

    auto* elem = wheelElements_[favoriteId].get();

    return elem->isBound() ? elem : nullptr;
}

std::vector<WheelElement*> Wheel::GetVisibleElements(ConditionalState cs, bool sorted) const
{
    std::vector<WheelElement*> elems;
    if (sorted)
    {
        for (auto& we : sortedWheelElements_)
            if (we->isVisible(cs))
                elems.push_back(we);
    }
    else
    {
        for (auto& we : wheelElements_)
            if (we->isVisible(cs))
                elems.push_back(we.get());
    }

    return elems;
}

bool Wheel::HasVisibleElements(ConditionalState cs) const
{
    for (auto& we : wheelElements_)
        if (we->isVisible(cs))
            return true;

    return false;
}

std::vector<WheelElement*> Wheel::GetUsableElements(ConditionalState cs, bool sorted) const
{
    std::vector<WheelElement*> elems;
    if (sorted)
    {
        for (auto& we : sortedWheelElements_)
            if (we->isUsable(cs))
                elems.push_back(we);
    }
    else
    {
        for (auto& we : wheelElements_)
            if (we->isUsable(cs))
                elems.push_back(we.get());
    }

    return elems;
}

bool Wheel::HasUsableElements(ConditionalState cs) const
{
    for (auto& we : wheelElements_)
        if (we->isUsable(cs))
            return true;

    return false;
}

bool Wheel::HasVisibleOrUsableElements(ConditionalState cs) const
{
    for (auto& we : wheelElements_)
        if (we->isUsable(cs) || we->isVisible(cs))
            return true;

    return false;
}

void Wheel::OnMouseMove(bool& rv)
{
    if (isVisible_)
    {
        UpdateHover();

        // If holding down the button is not necessary, modify behavior
        if (noHoldOption_.value() && isVisible_ && currentHovered_ != nullptr)
            DeactivateWheel();
    }

    rv |= isVisible_ && lockCameraWhenOverlayedOption_.value();
}

void Wheel::OnMouseButton(ScanCode sc, bool down, bool& rv)
{
    if (pendingGroundTargetKeybind_ && !isVisible_ && down && sc == ScanCode::LButton)
    {
        for (const auto& kc : pendingGroundTargetKeybind_->keyCombos())
            if (kc.key() != ScanCode::None)
                Input::i().SendKeybind(kc, std::nullopt, pendingGroundTargetAction_);
        pendingGroundTargetKeybind_ = nullptr;
    }

    if (clickSelectOption_.value() && isVisible_)
    {
        const bool previousVisibility = isVisible_;
        isVisible_                    = isVisible_ && sc != ScanCode::LButton;
        if (!isVisible_ && previousVisibility)
        {
            rv = true;
            DeactivateWheel();
        }
    }
    else if (isVisible_ && down && (sc == ScanCode::LButton || sc == ScanCode::RButton))
    {
        // This wheel doesn't use a click to select (hold-to-open or select-on-hover): a mouse
        // click while it's open means the player wants out, not "select whatever's hovered".
        rv = true;
        DeactivateWheel(true);
    }
}

PassToGame Wheel::KeybindEvent(bool center, Activated activated)
{
    const bool previousVisibility = isVisible_;

    if (MumbleLink::i().isMapOpen())
        isVisible_ = false;
    else
    {
        WheelElement* bypassElement = nullptr;
        Keybind*      bypassKeybind = nullptr;
        if (activated == Activated::Yes && !waitingForBypassComplete_ && (BypassCheck(bypassElement, bypassKeybind) || ShouldSkip(bypassElement)))
        {
            previousUsed_             = bypassElement;
            isVisible_                = false;
            waitingForBypassComplete_ = true;
            if (!bypassKeybind)
                SendKeybind(bypassElement, std::nullopt);
            else
            {
                Log::i().Print(Severity::Debug, "Sending bypass keybind.");

                Input::i().KeyUpActive();
                Input::i().SendKeybind(bypassKeybind->keyCombo(), std::nullopt);
            }
        }

        if (waitingForBypassComplete_)
        {
            if (activated == Activated::No)
                waitingForBypassComplete_ = false;
        }
        else
        {
            isVisible_ = activated == Activated::Yes;

            // If holding down the button is not necessary, modify behavior
            if (previousVisibility && (clickSelectOption_.value() || noHoldOption_.value() && currentHovered_ == nullptr))
                isVisible_ = true;

            if (isVisible_ && !previousVisibility)
                ActivateWheel(center);
        }
    }

    if (!isVisible_ && previousVisibility)
        DeactivateWheel();

    return isVisible_ != previousVisibility ? PassToGame::Prevent : PassToGame::Allow;
}

void Wheel::ActivateWheel(bool isMountOverlayLocked)
{
    auto& io             = ImGui::GetIO();

    cursorResetPosition_ = { static_cast<int>(io.MousePos.x), static_cast<int>(io.MousePos.y) };
    LogDebug("Storing cursor position ({}, {}) for restore...", cursorResetPosition_.x, cursorResetPosition_.y);

    if (!HasVisibleOrUsableElements(MumbleLink::i().currentState()))
    {
        LogWarn("Triggered menu '{}', but no element is visible or usable!", displayName_);
        bool isAnyBound = false;
        for (auto& we : wheelElements_)
            if (we->isBound())
            {
                isAnyBound = true;
                break;
            }

        if (!isAnyBound)
            showEmptyPopup_ = true;
    }

    // Mount overlay is turned on
    if (isMountOverlayLocked)
    {
        currentPosition_.x = currentPosition_.y = 0.5f;

        if (resetCursorOnLockedKeybindOption_.value())
            resetCursorPositionToCenter_ = true;
    }
    else
    {
        resetCursorPositionToCenter_ = false;
        currentPosition_.x           = io.MousePos.x / float(Core::i().screenWidth());
        currentPosition_.y           = io.MousePos.y / float(Core::i().screenHeight());
    }

    currentTriggerTime_ = TimeInMilliseconds();

    wipeMaskData_       = { frand() * 0.20f + 0.40f, frand() * 0.20f + 0.40f, frand() * 2 * float(M_PI) };

    UpdateHover();
}

void Wheel::PassToGame()
{
    bool centerKeybind = currentPosition_.x == 0.5f && currentPosition_.y == 0.5f;
    Input::i().SendKeybind((centerKeybind ? centralKeybind_ : keybind_).keyCombo());
    currentHovered_ = nullptr;
}


void Wheel::DeactivateWheel(bool cancel)
{
    isVisible_                   = false;
    resetCursorPositionToCenter_ = false;

    if (cancel)
        // Explicit cancel (e.g. a mouse click on a wheel that doesn't use clicks to select):
        // hide with no action, skipping every auto-selection fallback below.
        currentHovered_ = nullptr;
    // If keybind release was done before the wheel is visible, check our behavior
    else if (currentTriggerTime_ + displayDelayOption_.value() > TimeInMilliseconds())
    {
        switch (BehaviorBeforeDelay(behaviorOnReleaseBeforeDelay_.value()))
        {
            default:
                currentHovered_ = nullptr;
                break;
            case BehaviorBeforeDelay::Favorite:
                currentHovered_ = GetFavorite(delayFavoriteOption_.value());
                break;
            case BehaviorBeforeDelay::Previous:
                currentHovered_ = previousUsed_;
                break;
            case BehaviorBeforeDelay::Direction:
                if (!currentHovered_)
                    currentHovered_ = GetCenterHoveredElement();
                break;
            case BehaviorBeforeDelay::PassToGame:
                PassToGame();
                break;
        }
    }
    else if (!currentHovered_)
    {
        if (CenterBehavior(centerBehaviorOption_.value()) == CenterBehavior::PassToGame)
            PassToGame();
        else
            currentHovered_ = GetCenterHoveredElement();
    }

    auto groundTargetingMode = Core::i().groundTargetingMode();
    if (currentHovered_ != nullptr && ResetMouseCheck(currentHovered_) && groundTargetingMode != GroundTargetingMode::Normal)
    {
        // Skiff/Summon Conjured Doorway under Fast/Instant: GW2 casts under the cursor's position
        // at the moment the confirm keypress happens, so defer that keypress to the player's own
        // next real left-click (OnMouseButton) instead of firing immediately -- the wheel's cursor
        // is very likely not where the player actually wants to place this.
        Input::i().SendKeybind({}, cursorResetPosition_);

        auto* kb        = &currentHovered_->keybind();
        previousUsed_   = currentHovered_;
        currentHovered_ = nullptr;

        if (groundTargetingMode == GroundTargetingMode::FastWithRangeIndicator)
        {
            // Press (and hold) now to show GW2's own range indicator; release on the next click.
            for (const auto& kc : kb->keyCombos())
                if (kc.key() != ScanCode::None)
                    Input::i().SendKeybind(kc, std::nullopt, KeybindAction::Down);
            pendingGroundTargetKeybind_ = kb;
            pendingGroundTargetAction_  = KeybindAction::Up;
        }
        else // Instant
        {
            pendingGroundTargetKeybind_ = kb;
            pendingGroundTargetAction_  = KeybindAction::Both;
        }

        return;
    }

    bool resetMouse = alwaysResetCursorPositionBeforeKeyPress_ || resetCursorAfterKeybindOption_.value() || ResetMouseCheck(currentHovered_);

    // Mount overlay is turned off, send the keybind
    if (currentHovered_ != nullptr)
    {
        SendKeybind(currentHovered_, resetMouse ? std::make_optional(cursorResetPosition_) : std::nullopt);
        previousUsed_   = currentHovered_;
        currentHovered_ = nullptr;
    }
    else
        SendKeybind({}, resetMouse ? std::make_optional(cursorResetPosition_) : std::nullopt);
}

void Wheel::SendKeybind(OptKeybindWheelElement kbwe, std::optional<Point> mousePos)
{
    if (!OptHasValue(kbwe))
    {
        if (mousePos)
            Input::i().SendKeybind({}, mousePos);
        return;
    }

    Input::i().SendKeybind({}, mousePos);

    auto kb = GetKeybindFromOpt(kbwe);
    if (kb)
        for (const auto& kc : kb->keyCombos())
            if (kc.key() != ScanCode::None)
                Input::i().SendKeybind(kc, std::nullopt);
}
} // namespace GW2Radial
