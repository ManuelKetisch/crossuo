// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// The info-command table, the default items, the display-key parser and
// the plain-text title-data compiler (the caption of the client window).

#include "displaymanager.h"
#include "ServerList.h" // g_ServerList.LastServerName (the shared server name)
#include "../core/assist.h"
#include "../core/assistantevents.h"
#include "../core/itemcounters.h"
#include "../core/itemcounters_rules.h"
#include "../config/assistantconfig.h"
#include "../core/textconv.h"
#include "../core/world.h" // g_Player / g_World
#include <common/logging/logging.h>
#include <algorithm>

namespace xuoassist {

CDisplayManager g_DisplayManager;

namespace {

int color_digit(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f')
    {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F')
    {
        return character - 'A' + 10;
    }
    return -1;
}

DisplayTitleKind display_kind(const DisplayInfo &item)
{
    if (item.Type == DIT_NAME)
    {
        return DisplayTitleKind::Character;
    }
    if (item.Type != DIT_ITEM_COUNTER)
    {
        return DisplayTitleKind::Text;
    }
    if (item.Command == "statbar")
    {
        return DisplayTitleKind::SmallStatus;
    }
    if (item.Command == "mediumstatbar")
    {
        return DisplayTitleKind::MediumStatus;
    }
    if (item.Command == "largestatbar")
    {
        return DisplayTitleKind::LargeStatus;
    }
    return DisplayTitleKind::Text;
}

void append_title_literal(const astr_t &text, vector<DisplayTitlePart> &parts, uint32_t &color)
{
    size_t start = 0;
    size_t marker = text.find("~#");
    while (marker != astr_t::npos)
    {
        size_t end = marker + 2;
        uint32_t nextColor = 0;
        if (end < text.size() && text[end] == '~')
        {
            ++end;
        }
        else if (text.size() - end >= 6)
        {
            nextColor = 0xFF000000u;
            for (size_t digit = 0; digit < 6; ++digit)
            {
                const auto value = color_digit(text[end + digit]);
                if (value < 0)
                {
                    nextColor = 0;
                    break;
                }
                nextColor |= static_cast<uint32_t>(value) << ((5 - digit) * 4);
            }
            end = nextColor != 0 ? end + 6 : marker;
        }
        else
        {
            end = marker;
        }
        if (end != marker)
        {
            if (marker > start)
            {
                DisplayTitlePart part;
                part.Text = text.substr(start, marker - start);
                part.Color = color;
                parts.push_back(std::move(part));
            }
            color = nextColor;
            start = end;
        }
        marker = text.find("~#", std::max(marker + 2, end));
    }
    if (start < text.size())
    {
        DisplayTitlePart part;
        part.Text = text.substr(start);
        part.Color = color;
        parts.push_back(std::move(part));
    }
}

uint32_t evaluate_stat_color(const DisplayInfo &obj)
{
    if (g_Player == nullptr || (obj.MinValue <= 0 && obj.MidValue <= 0))
    {
        return 0u;
    }

    int val = 0;
    int maxVal = 0;
    StatThresholdPolarity polarity = StatThresholdPolarity::HigherIsBetter;

    switch (obj.Type)
    {
        case DIT_HITS:
        case DIT_HITS_MAX_HITS:
            val = g_Player->Hits;
            maxVal = g_Player->MaxHits;
            break;
        case DIT_MANA:
        case DIT_MANA_MAX_MANA:
            val = g_Player->Mana;
            maxVal = g_Player->MaxMana;
            break;
        case DIT_STAM:
        case DIT_STAM_MAX_STAM:
            val = g_Player->Stam;
            maxVal = g_Player->MaxStam;
            break;
        case DIT_WEIGHT:
        case DIT_WEIGHT_MAX_WEIGHT:
            val = g_Player->Weight;
            maxVal = g_Player->MaxWeight;
            polarity = StatThresholdPolarity::LowerIsBetter;
            break;
        case DIT_STR:
            val = g_Player->Str;
            break;
        case DIT_INT:
            val = g_Player->Int;
            break;
        case DIT_DEX:
            val = g_Player->Dex;
            break;
        case DIT_ARMOR:
            val = g_Player->Armor;
            break;
        case DIT_GOLD:
            val = static_cast<int>(g_Player->Gold);
            break;
        case DIT_FOLLOWERS:
            val = g_Player->Followers;
            maxVal = g_Player->MaxFollowers;
            polarity = StatThresholdPolarity::LowerIsBetter;
            break;
        case DIT_FIRE_RESIST:
            val = g_Player->FireResistance;
            break;
        case DIT_COLD_RESIST:
            val = g_Player->ColdResistance;
            break;
        case DIT_POISON_RESIST:
            val = g_Player->PoisonResistance;
            break;
        case DIT_ENERGY_RESIST:
            val = g_Player->EnergyResistance;
            break;
        case DIT_LUCK:
            val = g_Player->Luck;
            break;
        case DIT_TITHING:
            val = static_cast<int>(g_Player->TithingPoints);
            break;
        case DIT_STEALTH_STEPS:
            val = g_Player->StealthSteps;
            break;
        default:
            return 0u;
    }

    const auto band = ItemCounters_EvaluateThreshold(val, maxVal, obj.MinValue, obj.MidValue, polarity);
    return ItemCounters_ThresholdColor(band);
}

} // namespace

// The info-command catalog: the rows of the type combobox, the title-data tag
// lookup and the per-type defaults a tag renders with before the per-tag
// text/min/mid overrides apply.
// clang-format off
CDisplayManager::CDisplayManager()
{
    m_InfoCommands.push_back({ "", "", DIT_NONE, "", 0, 0 });
    m_InfoCommands.push_back({ "Player name", "name", DIT_NAME, "", 0, 0 });
    m_InfoCommands.push_back({ "Server name", "server", DIT_SERVER, "", 0, 0 });
    m_InfoCommands.push_back({ "Hitpoints", "hp", DIT_HITS, "hp:", 33, 66 });
    m_InfoCommands.push_back({ "Max hitpoints", "maxhp", DIT_MAX_HITS, "", 0, 0 });
    m_InfoCommands.push_back({ "Hitpoints / [Max hitpoints]", "hp_maxhp", DIT_HITS_MAX_HITS, "hp:", 33, 66 });
    m_InfoCommands.push_back({ "Mana", "mp", DIT_MANA, "mp:", 33, 66 });
    m_InfoCommands.push_back({ "Max mana", "maxmp", DIT_MAX_MANA, "", 0, 0 });
    m_InfoCommands.push_back({ "Mana / [Max mana]", "mp_maxmp", DIT_MANA_MAX_MANA, "mp:", 33, 66 });
    m_InfoCommands.push_back({ "Stamina", "sp", DIT_STAM, "sp:", 33, 66 });
    m_InfoCommands.push_back({ "Max stamina", "maxsp", DIT_MAX_STAM, "", 0, 0 });
    m_InfoCommands.push_back({ "Stamina / [Max stamina]", "sp_maxsp", DIT_STAM_MAX_STAM, "sp:", 33, 66 });
    m_InfoCommands.push_back({ "Strength", "str", DIT_STR, "str:", 10, 30 });
    m_InfoCommands.push_back({ "Intelligence", "int", DIT_INT, "int:", 10, 30 });
    m_InfoCommands.push_back({ "Dexterity", "dex", DIT_DEX, "dex:", 10, 30 });
    m_InfoCommands.push_back({ "Weight", "weight", DIT_WEIGHT, "w:", 75, 90 });
    m_InfoCommands.push_back({ "Max weight", "maxweight", DIT_MAX_WEIGHT, "", 0, 0 });
    m_InfoCommands.push_back({ "Weight / [Max weight]", "weight_maxweight", DIT_WEIGHT_MAX_WEIGHT, "w:", 75, 90 });
    m_InfoCommands.push_back({ "Armor rating", "armor", DIT_ARMOR, "ar:", 20, 40 });
    m_InfoCommands.push_back({ "Gold", "gold", DIT_GOLD, "g:", 0, 0 });
    m_InfoCommands.push_back({ "X", "x", DIT_X, "x:", 0, 0 });
    m_InfoCommands.push_back({ "Y", "y", DIT_Y, "y:", 0, 0 });
    m_InfoCommands.push_back({ "Z", "z", DIT_Z, "z:", 0, 0 });
    m_InfoCommands.push_back({ "Character name", "char", DIT_NAME, "", 0, 0 });
    m_InfoCommands.push_back({ "Shard name", "shard", DIT_SERVER, "", 0, 0 });
    m_InfoCommands.push_back({ "Max hitpoints", "hpmax", DIT_MAX_HITS, "", 0, 0 });
    m_InfoCommands.push_back({ "Mana", "mana", DIT_MANA, "", 33, 66 });
    m_InfoCommands.push_back({ "Max mana", "manamax", DIT_MAX_MANA, "", 0, 0 });
    m_InfoCommands.push_back({ "Stamina", "stam", DIT_STAM, "", 33, 66 });
    m_InfoCommands.push_back({ "Max stamina", "stammax", DIT_MAX_STAM, "", 0, 0 });
    m_InfoCommands.push_back({ "Maximum carried weight", "maxweight", DIT_MAX_WEIGHT, "", 0, 0 });
    m_InfoCommands.push_back({ "Armor rating", "ar", DIT_ARMOR, "", 0, 0 });
    m_InfoCommands.push_back({ "Physical resistance", "physresist", DIT_ARMOR, "", 0, 0 });
    m_InfoCommands.push_back({ "Followers", "followers", DIT_FOLLOWERS, "", 0, 0 });
    m_InfoCommands.push_back({ "Maximum followers", "followersmax", DIT_FOLLOWERS_MAX, "", 0, 0 });
    m_InfoCommands.push_back({ "Damage range", "damage", DIT_DAMAGE, "", 0, 0 });
    m_InfoCommands.push_back({ "Fire resistance", "fireresist", DIT_FIRE_RESIST, "", 0, 0 });
    m_InfoCommands.push_back({ "Cold resistance", "coldresist", DIT_COLD_RESIST, "", 0, 0 });
    m_InfoCommands.push_back({ "Poison resistance", "poisonresist", DIT_POISON_RESIST, "", 0, 0 });
    m_InfoCommands.push_back({ "Energy resistance", "energyresist", DIT_ENERGY_RESIST, "", 0, 0 });
    m_InfoCommands.push_back({ "Luck", "luck", DIT_LUCK, "", 0, 0 });
    m_InfoCommands.push_back({ "Tithing points", "tithe", DIT_TITHING, "", 0, 0 });
    m_InfoCommands.push_back({ "Stealth steps", "stealthsteps", DIT_STEALTH_STEPS, "", 0, 0 });
    m_InfoCommands.push_back({ "Connection time", "uptime", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Time as criminal", "crimtime", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Buffs and debuffs", "buffsdebuffs", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Gate estimate", "gate", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Time since using a skill", "skill", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Bandage estimate", "bandage", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Small status summary", "statbar", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Medium status summary", "mediumstatbar", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Large status summary", "largestatbar", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Gold collected", "goldtotal", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Minutes tracking gold", "goldtotalmin", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Gold per second", "gps", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Gold per minute", "gpm", DIT_ITEM_COUNTER, "", 0, 0 });
    m_InfoCommands.push_back({ "Gold per hour", "gph", DIT_ITEM_COUNTER, "", 0, 0 });
}
// clang-format on

const DISPLAY_KEY_ITEM CDisplayManager::m_KeysList[DKI_COUNT] = {
    { "", DKI_NONE },
    { "text", DKI_TEXT },
    { "minval", DKI_MIN_VAULE },
    { "midval", DKI_MID_VALUE },
};

void CDisplayManager::GetValues(const DisplayInfo &item, astr_t &value, astr_t &maxValue)
{
    int iVal = 0;
    int iMaxVal = 0;

    switch (item.Type)
    {
        case DIT_NAME:
        {
            value = astr_t(g_SelectedCharName);
            return;
        }
        case DIT_SERVER:
        {
            value = g_ServerList.LastServerName;
            return;
        }
        case DIT_HITS:
        case DIT_MAX_HITS:
        case DIT_HITS_MAX_HITS:
        {
            iVal = g_Player->Hits;
            iMaxVal = g_Player->MaxHits;
            break;
        }
        case DIT_MANA:
        case DIT_MAX_MANA:
        case DIT_MANA_MAX_MANA:
        {
            iVal = g_Player->Mana;
            iMaxVal = g_Player->MaxMana;
            break;
        }
        case DIT_STAM:
        case DIT_MAX_STAM:
        case DIT_STAM_MAX_STAM:
        {
            iVal = g_Player->Stam;
            iMaxVal = g_Player->MaxStam;
            break;
        }
        case DIT_STR:
        {
            iVal = g_Player->Str;
            break;
        }
        case DIT_INT:
        {
            iVal = g_Player->Int;
            break;
        }
        case DIT_DEX:
        {
            iVal = g_Player->Dex;
            break;
        }
        case DIT_WEIGHT:
        case DIT_MAX_WEIGHT:
        case DIT_WEIGHT_MAX_WEIGHT:
        {
            iVal = g_Player->Weight;
            iMaxVal = g_Player->MaxWeight;
            break;
        }
        case DIT_ARMOR:
        {
            iVal = g_Player->Armor;
            break;
        }
        case DIT_GOLD:
        {
            iVal = g_Player->Gold;
            break;
        }
        case DIT_X:
        {
            value = str_from(g_Player->GetX());
            return;
        }
        case DIT_Y:
        {
            value = str_from(g_Player->GetY());
            return;
        }
        case DIT_Z:
        {
            value = str_from(g_Player->GetZ());
            return;
        }
        case DIT_FOLLOWERS:
            iVal = g_Player->Followers;
            break;
        case DIT_FOLLOWERS_MAX:
            iVal = g_Player->MaxFollowers;
            break;
        case DIT_DAMAGE:
            value = str_from(g_Player->MinDamage) + "-" + str_from(g_Player->MaxDamage);
            return;
        case DIT_FIRE_RESIST:
            iVal = g_Player->FireResistance;
            break;
        case DIT_COLD_RESIST:
            iVal = g_Player->ColdResistance;
            break;
        case DIT_POISON_RESIST:
            iVal = g_Player->PoisonResistance;
            break;
        case DIT_ENERGY_RESIST:
            iVal = g_Player->EnergyResistance;
            break;
        case DIT_LUCK:
            iVal = g_Player->Luck;
            break;
        case DIT_TITHING:
            value = str_from(g_Player->TithingPoints);
            return;
        case DIT_STEALTH_STEPS:
            iVal = g_Player->StealthSteps;
            break;
        case DIT_ITEM_COUNTER:
        {
            ItemCounters_Read(item.Command, value);
            return;
        }
        case DIT_NONE:
        {
            break; // plain text token: obj.Text already carries the content
        }
        default:
            return;
    }

    value = str_from(iVal);
    maxValue = str_from(iMaxVal);
}

void CDisplayManager::Redraw(bool enabled)
{
    // Composes the caption text; the caller (the assistant UI tick) applies it
    // to the client window on change.
    astr_t title;
    m_TitleParts.clear();

    if (!enabled || g_World == nullptr || g_Player == nullptr)
    {
        title = "Ultima Online";

        if (g_World == nullptr)
        {
            if (g_ServerList.LastServerName.length() != 0)
            {
                title += " - " + g_ServerList.LastServerName;
            }
        }
        else
        {
            title += " - " + astr_t(g_SelectedCharName) + " (" + g_ServerList.LastServerName + ")";
        }
    }
    else
    {
        uint32_t color = 0;
        for (const DisplayInfo &obj : m_Commands)
        {
            if (obj.Type == DIT_ITEM_COUNTER)
            {
                DisplayCounterInfo info;
                ItemCounters_ReadDisplay(obj.Command, info);

                append_title_literal(obj.Text, m_TitleParts, color);

                if (!info.Value.empty())
                {
                    DisplayTitlePart part;
                    part.Text = std::move(info.Value);
                    if (info.Graphic != 0)
                    {
                        part.Kind = DisplayTitleKind::ItemCounter;
                        part.IconGraphic = info.ShowIcon ? info.Graphic : 0;
                        part.IconHue = info.ShowIcon ? info.Hue : 0;
                        part.Low = info.Low;
                        part.Highlighted = info.Highlighted;
                        part.Color = color != 0
                            ? color
                            : (info.Low ? ItemCounters_ThresholdColor(StatThresholdBand::Low) : 0u);
                    }
                    else
                    {
                        part.Kind = display_kind(obj);
                        part.Color = color;
                    }
                    m_TitleParts.push_back(std::move(part));
                }
                continue;
            }

            astr_t value = "";
            astr_t maxValue = "";

            GetValues(obj, value, maxValue);

            append_title_literal(obj.Text, m_TitleParts, color);

            switch (obj.Type)
            {
                case DIT_MAX_HITS:
                case DIT_MAX_MANA:
                case DIT_MAX_STAM:
                case DIT_MAX_WEIGHT:
                {
                    value = maxValue;
                    break;
                }
                case DIT_HITS_MAX_HITS:
                case DIT_MANA_MAX_MANA:
                case DIT_STAM_MAX_STAM:
                case DIT_WEIGHT_MAX_WEIGHT:
                {
                    if (value != maxValue)
                    {
                        value += "/" + maxValue;
                    }

                    break;
                }
                default:
                {
                    break;
                }
            }
            if (!value.empty())
            {
                DisplayTitlePart part;
                part.Text = std::move(value);
                part.Color = color != 0 ? color : evaluate_stat_color(obj);
                part.Kind = display_kind(obj);
                m_TitleParts.push_back(std::move(part));
            }
        }
        for (const auto &part : m_TitleParts)
        {
            title += part.Text;
        }

        if (title.length() == 0)
        {
            title = "Ultima Online - " + astr_t(g_SelectedCharName) + " (" + g_ServerList.LastServerName + ")";
        }
    }

    m_TitleText = title;
}

DisplayInfo CDisplayManager::GetDisplayItemByName(const astr_t &name) const
{
    DisplayInfo item = g_AssistantConfig.GetCustomDisplayItemByName(name);

    if (item.Type == DIT_NONE)
    {
        for (const DisplayInfo &info : m_InfoCommands)
        {
            if (name == info.Command)
            {
                return info;
            }
        }
    }

    if (item.Name.empty() && item.Type == DIT_NONE)
    {
        item.Type = DIT_ITEM_COUNTER;
        item.Command = name;
    }
    return item;
}

void CDisplayManager::ApplyAttributeToDisplayItem(
    DisplayInfo &obj, const astr_t &name, const astr_t &value)
{
    for (const DISPLAY_KEY_ITEM &key : m_KeysList)
    {
        if (name == key.Name)
        {
            switch (key.Identifier)
            {
                case DKI_TEXT:
                {
                    obj.Text = value;
                    break;
                }
                case DKI_MIN_VAULE:
                {
                    obj.MinValue = (int)CAssistant::RawStringToUInt(str_lower(value));
                    break;
                }
                case DKI_MID_VALUE:
                {
                    obj.MidValue = (int)CAssistant::RawStringToUInt(str_lower(value));
                    break;
                }
                default:
                    break;
            }

            break;
        }
    }
}

void CDisplayManager::RecompileData(const astr_t &text)
{
    g_UpdateDisplayOnStep = false;
    m_Commands.clear();

    CompileData(text);
}

void CDisplayManager::CompileData(const astr_t &text)
{
    // Tokenized line by line, one token list per line; the "{}" quote pair keeps the braces on tag
    // fragments so the tag branch below can recognize them.
    size_t pos = 0;

    while (pos < text.length())
    {
        const size_t eol = text.find('\n', pos);
        const astr_t line = text.substr(pos, (eol == astr_t::npos ? text.length() : eol) - pos);
        pos = (eol == astr_t::npos) ? text.length() : eol + 1;

        vector<astr_t> list = ParseTextTokens(line, "", "", "{}", false, true);

        for (const astr_t &token : list)
        {
            if (token.length() == 0)
            {
                continue;
            }

            astr_t str = token;

            if (str.at(0) == '{')
            {
                vector<astr_t> tags = ParseTextTokens(str, " \t{}=", "", "\"\"''");

                if (!tags.empty())
                {
                    astr_t lowerName = str_lower(*tags.begin());

                    if (lowerName == "group")
                    {
                        if (tags.size() > 1)
                        {
                            CompileData(g_AssistantConfig.GetGroupContent(str_lower(*(tags.begin() + 1))));
                        }

                        continue;
                    }

                    DisplayInfo tagData = GetDisplayItemByName(lowerName);

                    for (size_t t = 1; t < tags.size(); t++)
                    {
                        astr_t tagName = tags[t];

                        ++t;

                        if (t != tags.size())
                        {
                            ApplyAttributeToDisplayItem(tagData, str_lower(tagName), tags[t]);
                        }
                        else
                        {
                            break;
                        }
                    }

                    if ((tagData.Type >= DIT_X && tagData.Type <= DIT_Z) ||
                        tagData.Type == DIT_ITEM_COUNTER)
                    {
                        g_UpdateDisplayOnStep = true;
                    }

                    m_Commands.push_back(tagData);
                }
            }
            else
            {
                DisplayInfo tag; // a DIT_NONE tag renders its Text as-is
                tag.Text = str;
                m_Commands.push_back(tag);
            }
        }
    }
}

} // namespace xuoassist
