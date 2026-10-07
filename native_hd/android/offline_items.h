// User-authorized local free replacements for the retired billing UI.
// GPL-2.0-or-later. Original resource files and reward tables remain unchanged.
#pragma once
#include <map>
#include <string>

// The recovered original table has 13 entries, including four story/revive
// entries that are absent from the shortcut shop's six-item list.
inline constexpr uint32_t kOfflineItemCount=13;

inline const std::map<uint32_t,std::u16string>& offlineItemTexts() {
  static const std::map<uint32_t,std::u16string> texts={
    {3350,u"确定领取："}, {3351,u"领取成功。"}, {3352,u"领取失败。"},
    {3353,u"本地免费"}, {3354,u""}, {3355,u"免费"},
    {3356,u"当前内容已领取"}, {3357,u"已领取："},
    {4001,u"移动加快，不遇敌。本地免费，赠3万金。"},
    {4003,u"经验值4倍。本地免费，赠3万金。"},
    {4005,u"敌人掉落金钱4倍。本地免费，赠3万金。"},
    {4007,u"等级提升10级。本地免费，赠3万金。"},
    {4009,u"最强三神兽。本地免费，赠3万金。"},
    {4011,u"女娲石3个，青龙丹3个，白虎丹3个。本地免费，赠3万金。"},
    {4013,u"游戏刚刚开始，精彩还在继续，大周兵败，三大神兽不知所踪，强力队友女娲的加入。本地免费开启后续所有剧情。"},
    {4015,u"消灭所有敌人。本地免费，赠3万金。"},
    {4017,u"获得10万金钱。本地免费，赠3万金。"},
    {4019,u"原地满状态复活。本地免费，赠常志丹10个。"},
    {4021,u"神器天剑，本地免费。"},
    {4023,u"神器虎魄，本地免费。"},
    {4025,u"九仪天尊剑，本地免费。"}
  };
  return texts;
}
